#!/usr/bin/env python3
"""Offline DSP toolkit used to bake the game's voice and SFX assets.

Everything here works on float32 numpy arrays in [-1, 1]. Mono signals are
shape (n,), stereo are shape (n, 2). Most effects accept either.

The design goal is not fidelity, it is dread: these are the processes that turn
a flat text-to-speech read into something that sounds like it is coming from
inside a wall.
"""
import os
import subprocess
import wave

import numpy as np
from scipy import signal as sig

SR = 44100
RNG = np.random.default_rng(0xDEAD)


# --------------------------------------------------------------------- basics

def to_mono(x):
    return x.mean(axis=1) if x.ndim == 2 else x


def to_stereo(x):
    if x.ndim == 2:
        return x
    return np.stack([x, x], axis=1)


def norm(x, peak=0.95):
    m = float(np.max(np.abs(x))) if x.size else 0.0
    return x if m < 1e-9 else (x * (peak / m)).astype(np.float32)


def pad(x, before=0.0, after=0.0, sr=SR):
    """Pad with silence, in seconds."""
    b, a = int(before * sr), int(after * sr)
    if x.ndim == 2:
        return np.concatenate([np.zeros((b, 2), np.float32), x, np.zeros((a, 2), np.float32)])
    return np.concatenate([np.zeros(b, np.float32), x, np.zeros(a, np.float32)])


def mix(*layers):
    """Sum signals of unequal length, zero-extending the short ones."""
    layers = [l for l in layers if l is not None and l.size]
    if not layers:
        return np.zeros(0, np.float32)
    stereo = any(l.ndim == 2 for l in layers)
    n = max(len(l) for l in layers)
    out = np.zeros((n, 2), np.float32) if stereo else np.zeros(n, np.float32)
    for l in layers:
        if stereo:
            l = to_stereo(l)
        out[:len(l)] += l
    return out


def fade(x, fin=0.01, fout=0.05, sr=SR):
    y = x.copy()
    n = len(y)
    ni, no = min(int(fin * sr), n), min(int(fout * sr), n)
    if ni:
        env = np.linspace(0, 1, ni, dtype=np.float32)
        y[:ni] *= env[:, None] if y.ndim == 2 else env
    if no:
        env = np.linspace(1, 0, no, dtype=np.float32)
        y[-no:] *= env[:, None] if y.ndim == 2 else env
    return y


def gain(x, db):
    return (x * (10.0 ** (db / 20.0))).astype(np.float32)


# -------------------------------------------------------------------- filters

def _apply_sos(x, sos):
    if x.ndim == 2:
        return np.stack([sig.sosfiltfilt(sos, x[:, c]) for c in range(2)], axis=1).astype(np.float32)
    return sig.sosfiltfilt(sos, x).astype(np.float32)


def lowpass(x, hz, order=4, sr=SR):
    hz = min(hz, sr * 0.49)
    return _apply_sos(x, sig.butter(order, hz, "lowpass", fs=sr, output="sos"))


def highpass(x, hz, order=4, sr=SR):
    return _apply_sos(x, sig.butter(order, max(hz, 10), "highpass", fs=sr, output="sos"))


def bandpass(x, lo, hi, order=4, sr=SR):
    hi = min(hi, sr * 0.49)
    return _apply_sos(x, sig.butter(order, [max(lo, 10), hi], "bandpass", fs=sr, output="sos"))


def peaking(x, hz, q, db, sr=SR):
    """Single resonant bell — used to fake formants and give voices a body."""
    A = 10 ** (db / 40.0)
    w0 = 2 * np.pi * hz / sr
    alpha = np.sin(w0) / (2 * q)
    b = [1 + alpha * A, -2 * np.cos(w0), 1 - alpha * A]
    a = [1 + alpha / A, -2 * np.cos(w0), 1 - alpha / A]
    if x.ndim == 2:
        return np.stack([sig.lfilter(b, a, x[:, c]) for c in range(2)], axis=1).astype(np.float32)
    return sig.lfilter(b, a, x).astype(np.float32)


# ------------------------------------------------------------------ nonlinear

def soft_clip(x, drive=3.0):
    return np.tanh(x * drive).astype(np.float32) / np.tanh(drive)


def bitcrush(x, bits=6, rate_div=1):
    y = x.copy()
    if rate_div > 1:
        idx = (np.arange(len(y)) // rate_div) * rate_div
        y = y[idx]
    levels = 2 ** bits
    return (np.round(y * levels) / levels).astype(np.float32)


def ringmod(x, hz, depth=0.5, sr=SR):
    t = np.arange(len(x), dtype=np.float32) / sr
    m = (1 - depth) + depth * np.sin(2 * np.pi * hz * t).astype(np.float32)
    return (x * (m[:, None] if x.ndim == 2 else m)).astype(np.float32)


def tremolo(x, hz, depth=0.5, sr=SR):
    t = np.arange(len(x), dtype=np.float32) / sr
    m = 1 - depth * (0.5 + 0.5 * np.sin(2 * np.pi * hz * t)).astype(np.float32)
    return (x * (m[:, None] if x.ndim == 2 else m)).astype(np.float32)


# ------------------------------------------------------------- time & pitch

def resample_ratio(x, ratio):
    """Varispeed: pitch and duration move together, like a slowed tape."""
    n = max(1, int(round(len(x) / ratio)))
    src = np.linspace(0, len(x) - 1, n)
    if x.ndim == 2:
        return np.stack([np.interp(src, np.arange(len(x)), x[:, c]) for c in range(2)],
                        axis=1).astype(np.float32)
    return np.interp(src, np.arange(len(x)), x).astype(np.float32)


def pitch_shift(x, semitones, sr=SR, grain=0.055, overlap=4):
    """Granular pitch shift that preserves duration.

    Crude compared to a phase vocoder, and the artefacts it leaves — a faint
    metallic shimmer — are wanted here rather than tolerated.
    """
    if abs(semitones) < 0.01:
        return x
    ratio = 2.0 ** (semitones / 12.0)
    mono_in = x.ndim == 1
    xs = to_stereo(x)
    g = int(grain * sr)
    hop = g // overlap
    win = np.hanning(g).astype(np.float32)
    out = np.zeros((len(xs) + g * 2, 2), np.float32)
    wsum = np.zeros(len(out), np.float32) + 1e-9

    for out_pos in range(0, len(xs) - g, hop):
        # Read a grain at the shifted rate, write it back at the original rate.
        read = np.linspace(out_pos, out_pos + g * ratio, g, endpoint=False)
        read = np.clip(read, 0, len(xs) - 1)
        base = np.arange(len(xs))
        grain_data = np.stack(
            [np.interp(read, base, xs[:, c]) for c in range(2)], axis=1
        ).astype(np.float32)
        grain_data *= win[:, None]
        out[out_pos:out_pos + g] += grain_data
        wsum[out_pos:out_pos + g] += win

    out = (out / wsum[:, None]).astype(np.float32)[:len(xs)]
    return to_mono(out) if mono_in else out


def formant_shift(x, semitones, sr=SR):
    """Move formants without moving pitch: shift up, resample back down.

    Pushing formants down makes a voice sound like it belongs to something far
    larger than a person.
    """
    r = 2.0 ** (semitones / 12.0)
    y = resample_ratio(x, 1.0 / r)
    y = pitch_shift(y, -semitones, sr=sr)
    n = len(x)
    if len(y) < n:
        y = np.concatenate([y, np.zeros((n - len(y),) + y.shape[1:], np.float32)])
    return y[:n]


# --------------------------------------------------------------------- space

_IR_CACHE = {}


def impulse_response(seconds=2.0, decay=3.5, lo=180, hi=7000, predelay=0.0,
                     stereo=True, sr=SR):
    """Synthetic room: filtered noise under an exponential decay.

    Cached: generating one costs a bandpass over ~90k samples and the same few
    rooms get reused across hundreds of lines.
    """
    key = (seconds, decay, lo, hi, predelay, stereo, sr)
    if key in _IR_CACHE:
        return _IR_CACHE[key]
    n = int(seconds * sr)
    t = np.arange(n, dtype=np.float32) / sr
    env = np.exp(-decay * t).astype(np.float32)
    ch = 2 if stereo else 1
    ir = RNG.normal(0, 1, (n, ch)).astype(np.float32) * env[:, None]
    ir = bandpass(ir if stereo else ir[:, 0], lo, hi, sr=sr)
    # Early reflections give the tail a sense of walls rather than fog.
    ir = np.asarray(ir, np.float32)
    for delay_ms, amp in ((11, 0.5), (23, 0.38), (37, 0.3), (53, 0.22), (79, 0.16)):
        d = int(sr * delay_ms / 1000)
        if d < n:
            ir[d:] += ir[:n - d] * amp
    if predelay > 0:
        p = int(predelay * sr)
        ir = np.concatenate([np.zeros((p,) + ir.shape[1:], np.float32), ir])
    out = norm(ir, 0.6)
    _IR_CACHE[key] = out
    return out


def convolve(x, ir):
    """Wet-only convolution. Caller decides the blend."""
    xs = to_stereo(x)
    irs = to_stereo(ir) if ir.ndim == 1 else ir
    wet = np.stack(
        [sig.fftconvolve(xs[:, c], irs[:, c])[:len(xs) + len(irs) - 1] for c in range(2)],
        axis=1,
    ).astype(np.float32)
    return wet


def reverb(x, amount=0.35, seconds=2.0, decay=3.5, lo=180, hi=7000, predelay=0.0, sr=SR):
    ir = impulse_response(seconds, decay, lo, hi, predelay, sr=sr)
    wet = norm(convolve(x, ir), 0.9)
    dry = to_stereo(x)
    n = len(wet)
    out = np.zeros((n, 2), np.float32)
    out[:len(dry)] += dry * (1 - amount)
    out += wet * amount
    return out.astype(np.float32)


def reverse_reverb(x, amount=0.6, seconds=1.6, decay=3.0, sr=SR):
    """Sound swells *into* existence. The single most unsettling reverb trick."""
    rev = x[::-1].copy()
    wet = reverb(rev, amount=1.0, seconds=seconds, decay=decay, sr=sr)
    wet = wet[::-1].copy()
    dry = to_stereo(x)
    # Align so the swell lands on the word rather than after it.
    n = max(len(wet), len(dry))
    out = np.zeros((n, 2), np.float32)
    out[n - len(wet):] += norm(wet, 0.8) * amount
    out[n - len(dry):] += dry
    return out.astype(np.float32)


def delay(x, time=0.28, feedback=0.35, mixamt=0.3, sr=SR):
    xs = to_stereo(x)
    d = int(time * sr)
    tail = int(d * 6)
    out = np.concatenate([xs, np.zeros((tail, 2), np.float32)])
    buf = out.copy()
    fb = feedback
    pos = d
    while fb > 0.02 and pos < len(out):
        out[pos:] += buf[:len(out) - pos] * fb * mixamt
        fb *= feedback
        pos += d
    return out.astype(np.float32)


def widen(x, ms=18, sr=SR):
    """Haas spread. Turns a mono whisper into something on both sides of you."""
    xs = to_stereo(x)
    d = int(sr * ms / 1000)
    out = np.zeros((len(xs) + d, 2), np.float32)
    out[:len(xs), 0] += xs[:, 0]
    out[d:, 1] += xs[:, 1]
    return out


def pan(x, p):
    """p = -1 hard left .. +1 hard right, constant power."""
    m = to_mono(x)
    a = (p + 1) * np.pi / 4
    return np.stack([m * np.cos(a), m * np.sin(a)], axis=1).astype(np.float32)


# ------------------------------------------------------------------- texture

def noise(seconds, kind="white", sr=SR):
    n = int(seconds * sr)
    w = RNG.normal(0, 1, n).astype(np.float32)
    if kind == "white":
        return norm(w, 0.9)
    # Pink/brown by integrating in the frequency domain.
    spec = np.fft.rfft(w)
    f = np.fft.rfftfreq(n, 1 / sr)
    f[0] = f[1] if len(f) > 1 else 1.0
    exp = 0.5 if kind == "pink" else 1.0
    spec /= f ** exp
    return norm(np.fft.irfft(spec, n).astype(np.float32), 0.9)


def radio(x, noise_db=-26, drive=2.5, drops=True, sr=SR):
    """Bandlimit to a speaker cone, add carrier hiss and signal dropouts."""
    y = bandpass(x, 340, 3200, order=6, sr=sr)
    y = peaking(y, 1900, 2.0, 6.0, sr=sr)
    y = soft_clip(y, drive)
    hiss = bandpass(noise(len(y) / sr + 0.05, "white", sr), 900, 6000, sr=sr)
    hiss = gain(to_mono(hiss)[:len(y)], noise_db)
    y = to_mono(y)
    y = y + np.pad(hiss, (0, len(y) - len(hiss)))
    if drops:
        env = np.ones(len(y), np.float32)
        for _ in range(RNG.integers(1, 4)):
            start = RNG.integers(0, max(1, len(y) - 1))
            dur = int(RNG.uniform(0.012, 0.05) * sr)
            env[start:start + dur] *= RNG.uniform(0.05, 0.3)
        y *= env
    return norm(y, 0.9)


def stutter(x, slices=6, prob=0.4, sr=SR):
    """Randomly repeat short slices — a signal that keeps snagging."""
    n = len(x)
    seg = n // max(1, slices)
    out = []
    for i in range(slices):
        s = x[i * seg:(i + 1) * seg]
        out.append(s)
        if RNG.random() < prob and len(s):
            rep = s[:max(1, len(s) // 3)]
            out.append(rep)
            out.append(rep)
    return np.concatenate(out).astype(np.float32) if out else x


def wow_flutter(x, depth=0.004, hz=0.7, sr=SR):
    """Tape speed instability. Makes anything sound found rather than made."""
    n = len(x)
    t = np.arange(n, dtype=np.float32) / sr
    mod = depth * (np.sin(2 * np.pi * hz * t) + 0.4 * np.sin(2 * np.pi * hz * 3.7 * t))
    idx = np.clip(np.arange(n) + mod * sr, 0, n - 1)
    base = np.arange(n)
    if x.ndim == 2:
        return np.stack([np.interp(idx, base, x[:, c]) for c in range(2)], axis=1).astype(np.float32)
    return np.interp(idx, base, x).astype(np.float32)


# ------------------------------------------------------------------ synthesis

def half_sine(n, shape=1.0):
    """Sine-arch envelope. linspace(0, pi) overshoots pi by a float epsilon, so
    the raw sine can go very slightly negative — raising that to a fractional
    power yields NaN, which silently destroys the whole signal. Clip first."""
    arch = np.clip(np.sin(np.linspace(0, np.pi, n, dtype=np.float32)), 0.0, 1.0)
    return (arch ** shape).astype(np.float32)


def adsr(n, a=0.01, d=0.1, s=0.7, r=0.3, sr=SR):
    na, nd = int(a * sr), int(d * sr)
    nr = int(r * sr)
    ns = max(0, n - na - nd - nr)
    return np.concatenate([
        np.linspace(0, 1, na, dtype=np.float32),
        np.linspace(1, s, nd, dtype=np.float32),
        np.full(ns, s, np.float32),
        np.linspace(s, 0, nr, dtype=np.float32),
    ])[:n]


def osc(freq, seconds, kind="sine", sr=SR, phase=0.0):
    n = int(seconds * sr)
    t = np.arange(n, dtype=np.float32) / sr
    f = np.asarray(freq, np.float32)
    ph = 2 * np.pi * (np.cumsum(np.full(n, f, np.float32)) / sr if f.ndim == 0
                      else np.cumsum(f[:n]) / sr) + phase
    if kind == "sine":
        return np.sin(ph).astype(np.float32)
    if kind == "saw":
        return (2 * ((ph / (2 * np.pi)) % 1.0) - 1).astype(np.float32)
    if kind == "square":
        return np.sign(np.sin(ph)).astype(np.float32)
    if kind == "tri":
        return (2 * np.abs(2 * ((ph / (2 * np.pi)) % 1.0) - 1) - 1).astype(np.float32)
    raise ValueError(kind)


def sweep(f0, f1, seconds, kind="sine", sr=SR, log=True):
    n = int(seconds * sr)
    if log:
        f = np.geomspace(max(f0, 1e-3), max(f1, 1e-3), n).astype(np.float32)
    else:
        f = np.linspace(f0, f1, n).astype(np.float32)
    return osc(f, seconds, kind, sr)


# --------------------------------------------------------------------- output

def write_wav(path, x, sr=SR):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    y = np.clip(x, -1.0, 1.0)
    data = (y * 32767).astype("<i2")
    ch = 2 if y.ndim == 2 else 1
    with wave.open(path, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(data.tobytes())


def read_wav(path):
    with wave.open(path, "rb") as w:
        ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    assert sw == 2, f"expected 16-bit wav, got {sw * 8}-bit"
    x = np.frombuffer(raw, "<i2").astype(np.float32) / 32768.0
    if ch == 2:
        x = x.reshape(-1, 2)
    if sr != SR:
        x = resample_ratio(x, sr / SR)
    return x


def encode_ogg(x, out_path, sr=SR, quality=3, mono=None):
    """Write an OGG Vorbis file, going through a temp WAV for ffmpeg."""
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    if mono is True:
        x = to_mono(x)
    elif mono is False:
        x = to_stereo(x)
    tmp = out_path + ".tmp.wav"
    write_wav(tmp, norm(x, 0.94), sr)
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-i", tmp,
           "-c:a", "libvorbis", "-qscale:a", str(quality), out_path]
    subprocess.run(cmd, check=True)
    os.remove(tmp)
    return out_path
