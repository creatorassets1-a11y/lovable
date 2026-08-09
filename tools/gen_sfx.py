#!/usr/bin/env python3
"""Synthesise every non-spoken sound in the game.

Nothing here is sampled — it is all built from oscillators and noise, then
mangled. That keeps the APK small and means each sound can be tuned for exactly
the moment it plays.

Run: python3 tools/gen_sfx.py
Out: android/app/src/main/assets/game/audio/sfx/*.ogg
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import audio_dsp as A  # noqa: E402

SR = A.SR
RNG = np.random.default_rng(1312)
OUT = os.path.join(os.path.dirname(__file__), "..", "android", "app", "src", "main",
                   "assets", "game", "audio", "sfx")


def t_arr(seconds):
    return np.arange(int(seconds * SR), dtype=np.float32) / SR


def smooth_noise(n, hz):
    """Band-limited random control signal, for jitter and drift."""
    raw = RNG.normal(0, 1, n).astype(np.float32)
    return A.lowpass(raw, hz)


def make_loop(x, crossfade=0.35):
    """Fold the tail back over the head so the file loops without a seam."""
    xs = A.to_stereo(x)
    n = int(crossfade * SR)
    if n * 2 >= len(xs):
        return xs
    head, tail = xs[:n].copy(), xs[-n:].copy()
    ramp = np.linspace(0, 1, n, dtype=np.float32)[:, None]
    xs = xs[:-n]
    xs[:n] = head * ramp + tail * (1 - ramp)
    return xs


# --------------------------------------------------------------- the scream

def scream(seconds=1.9, base=380.0, top=1000.0, sex=0.0, chaos=1.0, seed=None):
    """A human throat losing control.

    The parts that sell it are the jitter (a real scream is never pitch-stable),
    the subharmonic growl that appears once the fold hits hard, and the break at
    the end where pitch collapses and turns to rasp.
    """
    rng = np.random.default_rng(seed) if seed is not None else RNG
    n = int(seconds * SR)
    t = np.arange(n, dtype=np.float32) / SR
    frac = t / seconds

    # Pitch: snap up, waver at the top, then break downward.
    attack = np.clip(frac / 0.06, 0, 1) ** 0.5
    collapse = np.clip((frac - 0.72) / 0.28, 0, 1) ** 1.6
    f0 = base + (top - base) * attack - (top - base) * 0.62 * collapse

    vibrato = 1 + 0.035 * np.sin(2 * np.pi * 5.6 * t + rng.uniform(0, 6))
    jitter = 1 + 0.055 * chaos * smooth_noise(n, 22)[:n]
    f0 = np.maximum(f0 * vibrato * jitter, 60).astype(np.float32)

    phase = 2 * np.pi * np.cumsum(f0) / SR

    # Glottal source: harmonic stack with a natural rolloff.
    src = np.zeros(n, np.float32)
    for h in range(1, 26):
        src += (np.sin(phase * h + rng.uniform(0, 6)) / (h ** 1.05)).astype(np.float32)

    # Subharmonic growl fades in as the voice tears.
    growl_amt = (0.12 + 0.45 * np.clip((frac - 0.25) / 0.5, 0, 1)) * chaos
    src += (np.sin(phase * 0.5) * growl_amt).astype(np.float32)

    # Breath/rasp, increasing as the throat gives out.
    rasp = A.bandpass(rng.normal(0, 1, n).astype(np.float32), 1200, 8000)
    rasp *= (0.10 + 0.55 * frac ** 2).astype(np.float32)
    src = src * 0.75 + rasp * 0.5

    # Vowel formants — /a/ drifting toward /e/ as it strains.
    y = src
    for hz, q, db in ((730 - 120 * sex, 5.0, 12.0),
                      (1090 + 260 * sex, 6.0, 9.0),
                      (2440 + 300 * sex, 7.0, 7.0),
                      (3400, 8.0, 4.0)):
        y = A.peaking(y, hz, q, db)
    y = A.highpass(y, 120)

    # Wavefolding: the nonlinearity that turns "loud" into "wrong".
    env = np.clip(frac / 0.04, 0, 1) * np.clip((1 - frac) / 0.22, 0, 1) ** 0.6
    y = y * env
    y = A.norm(y, 1.0)
    y = np.sin(y * (1.6 + 2.2 * chaos)).astype(np.float32)
    y = A.soft_clip(y, 1.8)

    # Chest weight, felt more than heard on a phone speaker.
    sub = (np.sin(2 * np.pi * np.cumsum(f0 * 0.25) / SR) * env * 0.5).astype(np.float32)
    sub = A.lowpass(sub, 220)

    out = A.mix(A.norm(y, 0.9), sub)
    return A.norm(out, 0.99)


def sfx_scream_mara():
    s = scream(2.1, base=300, top=880, sex=0.3, chaos=1.15, seed=7)
    s = A.pitch_shift(s, -3.0)
    s = A.soft_clip(s, 2.2)
    s = A.reverb(s, amount=0.34, seconds=2.6, decay=2.4, lo=70, hi=7000)
    return A.norm(A.fade(s, 0.0008, 0.3), 0.99)


def sfx_scream_mara_2():
    s = scream(1.5, base=420, top=1180, sex=0.5, chaos=1.35, seed=21)
    s = A.pitch_shift(s, -2.0)
    s = A.bitcrush(s, bits=8)
    s = A.reverb(s, amount=0.28, seconds=2.0, decay=3.0, lo=80, hi=8000)
    return A.norm(A.fade(s, 0.0008, 0.25), 0.99)


def sfx_scream_child():
    s = scream(1.35, base=620, top=1650, sex=1.0, chaos=0.75, seed=99)
    s = A.highpass(s, 300)
    s = A.reverb(s, amount=0.46, seconds=3.0, decay=2.0, lo=300, hi=9000)
    return A.norm(A.fade(s, 0.001, 0.35), 0.95)


def sfx_giggle():
    """Child laughter: short scream bursts on a descending pitch ladder."""
    parts = []
    for i in range(6):
        b = 700 - i * 45
        g = scream(0.13, base=b, top=b * 1.45, sex=1.0, chaos=0.35, seed=200 + i)
        parts.append(A.pad(A.norm(g, 0.7), after=RNG.uniform(0.04, 0.09)))
    y = np.concatenate([A.to_stereo(p) for p in parts])
    y = A.highpass(y, 400)
    y = A.reverb(y, amount=0.5, seconds=2.4, decay=2.4, lo=350, hi=9000)
    return A.norm(A.fade(y, 0.005, 0.3), 0.8)


# ------------------------------------------------------------- stings & hits

def sfx_stinger(seed, seconds=2.6, root=98.0):
    """Dissonant string-like cluster: minor second + tritone, bowed hard.

    A minor second beating against itself is about as close to a musical
    definition of "wrong" as exists.
    """
    rng = np.random.default_rng(seed)
    ratios = (1.0, 1.0595, 1.4142, 2.0, 2.1189, 2.8284)
    layers = []
    for r in ratios:
        f = root * r * rng.uniform(0.997, 1.003)
        s = A.osc(f, seconds, "saw")
        s = s * A.adsr(len(s), a=0.004, d=0.5, s=0.35, r=seconds * 0.5)
        s = A.peaking(s, f * 2, 3.0, 5.0)
        layers.append(A.pan(s * rng.uniform(0.5, 1.0), rng.uniform(-0.9, 0.9)))

    hit = A.bandpass(rng.normal(0, 1, int(0.25 * SR)).astype(np.float32), 200, 9000)
    hit *= np.exp(-16 * t_arr(0.25))

    y = A.mix(*layers, A.to_stereo(hit) * 0.8)
    y = A.lowpass(y, 6500)
    y = A.soft_clip(y, 1.6)
    y = A.reverb(y, amount=0.35, seconds=2.8, decay=2.0, lo=60, hi=7000)
    return A.norm(A.fade(y, 0.001, 0.4), 0.95)


def sfx_sub_boom():
    """The impact under a jump scare. Mostly felt, not heard."""
    d = 1.6
    body = A.sweep(120, 32, d, "sine")
    body *= np.exp(-3.2 * t_arr(d))
    # A little harmonic content so phone speakers reproduce *something*.
    harm = A.sweep(240, 64, d, "sine") * np.exp(-5.0 * t_arr(d)) * 0.35
    click = A.bandpass(RNG.normal(0, 1, int(0.05 * SR)).astype(np.float32), 400, 6000)
    click *= np.exp(-40 * t_arr(0.05))
    y = A.mix(body, harm, click * 0.5)
    y = A.soft_clip(y, 1.4)
    return A.norm(A.fade(y, 0.0005, 0.3), 0.99)


def sfx_riser(seconds=4.5):
    """Tension riser: noise sweeping up under a rising detuned cluster."""
    n = int(seconds * SR)
    frac = np.linspace(0, 1, n, dtype=np.float32)
    nz = RNG.normal(0, 1, n).astype(np.float32)
    sos_sweep = np.zeros(n, np.float32)
    # Approximate a moving bandpass by summing a few static bands under envelopes.
    for centre, when in ((400, 0.15), (900, 0.4), (2000, 0.65), (4200, 0.85), (7000, 0.97)):
        band = A.bandpass(nz, centre * 0.7, centre * 1.4)
        band *= np.exp(-((frac - when) ** 2) / (2 * 0.11 ** 2))
        sos_sweep += band
    tone = A.sweep(70, 620, seconds, "saw") * 0.4
    tone += A.sweep(70 * 1.06, 620 * 1.06, seconds, "saw") * 0.35
    tone *= frac ** 2
    y = A.mix(A.norm(sos_sweep, 0.8), A.norm(tone, 0.7))
    y = y * (frac ** 1.4)[:len(y), None] if y.ndim == 2 else y * frac ** 1.4
    y = A.soft_clip(y, 1.3)
    return A.norm(A.fade(y, 0.2, 0.02), 0.9)


# ------------------------------------------------------------------- drones

def drone(seconds, root, partials, filt_hz, motion=0.12, grit=0.0, seed=0):
    rng = np.random.default_rng(seed)
    layers = []
    for p, amp in partials:
        f = root * p
        det = 1 + rng.uniform(-0.004, 0.004)
        a = A.osc(f * det, seconds, "saw" if p < 3 else "tri")
        b = A.osc(f * det * 1.004, seconds, "sine")
        s = (a * 0.6 + b * 0.4) * amp
        # Slow amplitude drift keeps a drone from reading as a held synth note.
        lfo = 1 - motion * (0.5 + 0.5 * np.sin(
            2 * np.pi * rng.uniform(0.03, 0.12) * t_arr(seconds) + rng.uniform(0, 6)))
        layers.append(A.pan(s * lfo, rng.uniform(-0.8, 0.8)))

    air = A.bandpass(rng.normal(0, 1, int(seconds * SR)).astype(np.float32), 200, 3000)
    layers.append(A.to_stereo(air) * 0.06)

    y = A.mix(*layers)
    y = A.lowpass(y, filt_hz)
    if grit:
        y = A.soft_clip(y, 1 + grit * 2.5)
    y = A.reverb(y, amount=0.4, seconds=3.5, decay=1.4, lo=50, hi=5000)
    return A.norm(make_loop(y, 0.6), 0.72)


def sfx_drone_calm():
    return drone(14.0, 48.0, [(1, 0.55), (2, 0.22), (3, 0.10), (4.7, 0.05)],
                 900, motion=0.16, seed=3)


def sfx_drone_dread():
    # Adds the minor second a floor below the root — quietly unbearable.
    return drone(14.0, 44.0, [(1, 0.6), (1.0595, 0.3), (2, 0.2), (3.01, 0.09)],
                 1500, motion=0.25, grit=0.3, seed=5)


def sfx_drone_chaos():
    return drone(12.0, 41.0, [(1, 0.6), (1.0595, 0.4), (1.4142, 0.3),
                              (2.83, 0.15), (5.1, 0.07)],
                 2600, motion=0.4, grit=0.8, seed=11)


def sfx_water_rise():
    """Deep moving water under the floor."""
    n = int(12.0 * SR)
    nz = RNG.normal(0, 1, n).astype(np.float32)
    body = A.lowpass(nz, 380)
    swell = 1 + 0.5 * np.sin(2 * np.pi * 0.07 * t_arr(12.0))
    body *= swell
    lap = A.bandpass(nz, 700, 3200) * (0.12 + 0.1 * np.sin(2 * np.pi * 0.19 * t_arr(12.0)))
    y = A.mix(A.norm(body, 0.8), A.norm(lap, 0.25))
    y = A.reverb(y, amount=0.3, seconds=2.2, decay=2.4, lo=80, hi=4000)
    return A.norm(make_loop(y, 0.7), 0.6)


def sfx_light_buzz():
    """Failing fluorescent tube: 100 Hz hum, harmonics, intermittent arcing."""
    d = 6.0
    n = int(d * SR)
    y = np.zeros(n, np.float32)
    for h, a in ((100, 0.5), (200, 0.3), (300, 0.16), (500, 0.08), (700, 0.04)):
        y += A.osc(h, d, "square")[:n] * a
    y = A.highpass(y, 80)
    arc = A.bandpass(RNG.normal(0, 1, n).astype(np.float32), 2000, 9000)
    gate = (smooth_noise(n, 30) > 0.35).astype(np.float32)
    y += arc * gate * 0.35
    y = A.soft_clip(y, 1.5)
    return A.norm(make_loop(A.to_stereo(y), 0.3), 0.4)


def sfx_whisper_bed():
    """Unintelligible crowd of whispers. Formant-shaped noise, never words."""
    d = 10.0
    n = int(d * SR)
    layers = []
    for i in range(7):
        rng = np.random.default_rng(400 + i)
        nz = rng.normal(0, 1, n).astype(np.float32)
        # Syllable-rate gating gives it speech rhythm without speech content.
        rate = rng.uniform(3.5, 6.5)
        env = np.clip(smooth_noise(n, rate) * 3, 0, 1)
        s = A.bandpass(nz, rng.uniform(900, 1600), rng.uniform(3500, 6500))
        for hz in (rng.uniform(600, 900), rng.uniform(1400, 2200)):
            s = A.peaking(s, hz, 4.0, 8.0)
        layers.append(A.pan(s * env * rng.uniform(0.4, 1.0), rng.uniform(-1, 1)))
    y = A.mix(*layers)
    y = A.highpass(y, 500)
    y = A.reverb(y, amount=0.55, seconds=3.0, decay=2.0, lo=500, hi=9000)
    return A.norm(make_loop(y, 0.8), 0.5)


# ----------------------------------------------------------------- the body

def sfx_heartbeat(bpm=64, beats=8, intensity=0.0):
    """Two-thump heartbeat. Higher intensity = faster, harder, more audible."""
    period = 60.0 / bpm
    n = int(period * beats * SR)
    y = np.zeros(n, np.float32)

    def thump(freq, dur, amp):
        s = A.sweep(freq, freq * 0.45, dur, "sine")
        s *= np.exp(-np.linspace(0, 9, len(s), dtype=np.float32))
        # A touch of tissue slap so it is audible on a phone speaker.
        slap = A.bandpass(RNG.normal(0, 1, len(s)).astype(np.float32), 90, 700)
        slap *= np.exp(-np.linspace(0, 26, len(s), dtype=np.float32)) * (0.15 + 0.3 * intensity)
        return (s + slap) * amp

    lub = thump(74, 0.34, 1.0)
    dub = thump(58, 0.30, 0.72)
    for b in range(beats):
        p = int(b * period * SR)
        for offs, sound in ((0.0, lub), (0.20, dub)):
            i = p + int(offs * SR)
            seg = sound[:max(0, n - i)]
            y[i:i + len(seg)] += seg

    y = A.lowpass(y, 320 + 400 * intensity)
    y = A.norm(y, 0.85)
    return make_loop(A.to_stereo(y), 0.05)


def sfx_breath(panic=0.0, cycles=6):
    """In/out breathing. Panic shortens the cycle and adds voice to the exhale."""
    period = 3.4 - 2.1 * panic
    n = int(period * cycles * SR)
    y = np.zeros(n, np.float32)
    for c in range(cycles):
        base = int(c * period * SR)
        for phase, dur, lo, hi, amp in (
                (0.0, period * 0.34, 500, 4200, 0.9),
                (period * 0.46, period * 0.40, 300, 2600, 0.7)):
            ln = int(dur * SR)
            if base + int(phase * SR) + ln > n:
                continue
            nz = RNG.normal(0, 1, ln).astype(np.float32)
            s = A.bandpass(nz, lo, hi)
            env = A.half_sine(ln, 1.4)
            s *= env * amp
            if panic > 0.4 and phase > 0:
                # A shred of vocal fold on the exhale — the sound of not coping.
                voice = A.osc(RNG.uniform(105, 135), dur, "saw") * env * 0.18 * panic
                s += A.peaking(voice, 800, 3.0, 8.0)
            i = base + int(phase * SR)
            y[i:i + ln] += s
    y = A.highpass(y, 220)
    y = A.reverb(y, amount=0.12, seconds=0.6, decay=8.0, lo=300, hi=6000)
    return A.norm(make_loop(y, 0.25), 0.8)


def footstep(wet, seed):
    """One step. Wet steps get a splash layer and a longer, brighter tail."""
    rng = np.random.default_rng(seed)
    d = 0.34 if wet else 0.16
    n = int(d * SR)
    nz = rng.normal(0, 1, n).astype(np.float32)

    thud = A.lowpass(nz, rng.uniform(150, 260))
    thud *= np.exp(-np.linspace(0, 22, n, dtype=np.float32))

    if wet:
        splash = A.bandpass(nz, 1400, 9000)
        splash *= np.exp(-np.linspace(0, 9, n, dtype=np.float32)) ** 0.7
        drips = A.bandpass(rng.normal(0, 1, n).astype(np.float32), 2500, 7000)
        drips *= (rng.random(n) < 0.0012).astype(np.float32)
        drips = A.lowpass(drips, 6000) * 6
        y = A.mix(A.norm(thud, 0.9), A.norm(splash, 0.55), A.norm(drips, 0.3))
    else:
        scuff = A.bandpass(nz, 900, 5000)
        scuff *= np.exp(-np.linspace(0, 30, n, dtype=np.float32))
        y = A.mix(A.norm(thud, 0.9), A.norm(scuff, 0.30))

    y = A.reverb(y, amount=0.30, seconds=1.4, decay=4.0, lo=150, hi=6000)
    return A.norm(A.fade(y, 0.0005, 0.15), 0.85)


# ------------------------------------------------------------ the building

def sfx_door_creak(seed=1, seconds=2.2):
    """Stick-slip friction: a chain of tiny pitched pops reads as a hinge."""
    rng = np.random.default_rng(seed)
    n = int(seconds * SR)
    y = np.zeros(n, np.float32)
    pos, f = 0, rng.uniform(230, 330)
    while pos < n - 2000:
        dur = int(rng.uniform(0.01, 0.05) * SR)
        seg = A.osc(f, dur / SR, "saw")
        seg *= np.exp(-np.linspace(0, rng.uniform(6, 16), len(seg), dtype=np.float32))
        y[pos:pos + len(seg)] += seg * rng.uniform(0.3, 1.0)
        pos += int(dur * rng.uniform(0.7, 2.4))
        f *= rng.uniform(1.0, 1.035)   # hinges rise in pitch as they swing
    y = A.peaking(y, 1100, 3.0, 8.0)
    y = A.peaking(y, 2700, 4.0, 5.0)
    y = A.bandpass(y, 180, 7000)
    y = A.reverb(y, amount=0.35, seconds=2.0, decay=3.0, lo=150, hi=6000)
    return A.norm(A.fade(y, 0.01, 0.3), 0.8)


def sfx_door_slam():
    n = int(1.2 * SR)
    nz = RNG.normal(0, 1, n).astype(np.float32)
    body = A.lowpass(nz, 190) * np.exp(-np.linspace(0, 14, n, dtype=np.float32))
    crack = A.bandpass(nz, 800, 7000) * np.exp(-np.linspace(0, 60, n, dtype=np.float32))
    boom = A.sweep(90, 45, 0.5, "sine") * np.exp(-np.linspace(0, 8, int(0.5 * SR), dtype=np.float32))
    y = A.mix(A.norm(body, 0.9), A.norm(crack, 0.6), A.norm(boom, 0.7))
    y = A.soft_clip(y, 1.5)
    y = A.reverb(y, amount=0.4, seconds=2.4, decay=2.6, lo=80, hi=6000)
    return A.norm(A.fade(y, 0.0004, 0.3), 0.97)


def sfx_knock(count=3):
    """Knuckles on a hollow door, unevenly spaced. Even spacing sounds mechanical."""
    gaps = [0.0, 0.34, 0.62, 1.05, 1.3][:count]
    n = int((gaps[-1] + 0.9) * SR)
    y = np.zeros(n, np.float32)
    for g in gaps:
        d = int(0.30 * SR)
        nz = RNG.normal(0, 1, d).astype(np.float32)
        hit = A.bandpass(nz, 90, 2400) * np.exp(-np.linspace(0, 24, d, dtype=np.float32))
        res = A.osc(RNG.uniform(115, 145), 0.30, "sine") * np.exp(
            -np.linspace(0, 16, d, dtype=np.float32)) * 0.5
        i = int(g * SR)
        seg = A.norm(hit + res, 0.9)[:n - i]
        y[i:i + len(seg)] += seg
    y = A.reverb(y, amount=0.42, seconds=2.6, decay=2.4, lo=90, hi=5000)
    return A.norm(A.fade(y, 0.0005, 0.35), 0.9)


def sfx_drip(seed=1):
    rng = np.random.default_rng(seed)
    d = 0.5
    n = int(d * SR)
    f0 = rng.uniform(900, 1700)
    # A drip's pitch rises as the cavity it lands in closes.
    f = np.linspace(f0, f0 * 2.4, n).astype(np.float32)
    y = np.sin(2 * np.pi * np.cumsum(f) / SR).astype(np.float32)
    y *= np.exp(-np.linspace(0, 30, n, dtype=np.float32))
    tick = A.bandpass(rng.normal(0, 1, n).astype(np.float32), 2000, 9000)
    tick *= np.exp(-np.linspace(0, 120, n, dtype=np.float32))
    y = A.mix(A.norm(y, 0.8), A.norm(tick, 0.4))
    y = A.reverb(y, amount=0.5, seconds=2.2, decay=3.0, lo=400, hi=9000)
    return A.norm(A.fade(y, 0.0003, 0.3), 0.75)


def sfx_scrape():
    """Fingernails dragged down plaster."""
    d = 1.8
    n = int(d * SR)
    nz = RNG.normal(0, 1, n).astype(np.float32)
    grains = (RNG.random(n) < 0.06).astype(np.float32) * nz
    y = A.bandpass(grains, 1800, 9000) * 8
    y = A.peaking(y, 3200, 5.0, 9.0)
    y = A.peaking(y, 5400, 6.0, 6.0)
    env = A.half_sine(n, 0.7)
    y *= env
    y = A.reverb(y, amount=0.3, seconds=1.8, decay=3.4, lo=800, hi=10000)
    return A.norm(A.fade(y, 0.02, 0.25), 0.7)


def sfx_bone():
    """Neck/knuckle crack. Short, dry, and unpleasant."""
    parts = []
    for i in range(RNG.integers(3, 6)):
        d = int(RNG.uniform(0.012, 0.03) * SR)
        nz = RNG.normal(0, 1, d).astype(np.float32)
        p = A.bandpass(nz, 700, 6000) * np.exp(-np.linspace(0, 40, d, dtype=np.float32))
        parts.append(A.pad(A.norm(p, RNG.uniform(0.5, 1.0)), after=RNG.uniform(0.02, 0.08)))
    y = np.concatenate([A.to_stereo(p) for p in parts])
    y = A.reverb(y, amount=0.3, seconds=1.2, decay=5.0, lo=300, hi=8000)
    return A.norm(A.fade(y, 0.0003, 0.2), 0.85)


def sfx_static_burst():
    d = 0.8
    n = int(d * SR)
    nz = RNG.normal(0, 1, n).astype(np.float32)
    y = A.bandpass(nz, 600, 9000)
    y *= np.clip(smooth_noise(n, 60) * 4, 0, 1)
    y *= np.exp(-np.linspace(0, 5, n, dtype=np.float32))
    y = A.bitcrush(y, bits=5)
    return A.norm(A.fade(A.to_stereo(y), 0.001, 0.15), 0.8)


def sfx_radio_tune():
    """Sweeping the dial: bands of static with ghost carriers between stations."""
    d = 3.0
    n = int(d * SR)
    nz = RNG.normal(0, 1, n).astype(np.float32)
    y = A.bandpass(nz, 500, 6000) * 0.7
    for centre, when, width in ((1.0, 0.25, 0.03), (1.0, 0.55, 0.025), (1.0, 0.82, 0.04)):
        frac = np.linspace(0, 1, n, dtype=np.float32)
        carrier = A.osc(RNG.uniform(600, 2400), d, "sine")
        y += carrier * np.exp(-((frac - when) ** 2) / (2 * width ** 2)) * 0.5 * centre
    y = A.bitcrush(y, bits=6)
    y = A.bandpass(y, 340, 3400)
    return A.norm(A.fade(A.to_stereo(y), 0.02, 0.15), 0.75)


# ------------------------------------------------------------- the lullaby

def sfx_music_box():
    """Ellie's music box.

    Struck-bar timbre (inharmonic partials, instant attack, long decay) playing a
    minor motif that flattens its own fifth on the repeat, then winds down.
    """
    # E minor motif, then the same phrase soured and slowed.
    melody = [
        (76, 0.00, 1.0), (79, 0.40, 0.9), (83, 0.80, 1.0), (81, 1.20, 0.85),
        (79, 1.60, 0.9), (76, 2.00, 1.0), (75, 2.40, 0.8), (76, 2.80, 0.9),
        (76, 3.60, 0.9), (79, 4.02, 0.85), (82, 4.46, 0.95), (81, 4.92, 0.8),
        (78, 5.42, 0.85), (75, 5.98, 0.9), (74, 6.62, 0.7), (71, 7.40, 0.75),
        (69, 8.40, 0.6), (67, 9.60, 0.45),
    ]
    total = 12.0
    n = int(total * SR)
    y = np.zeros(n, np.float32)
    # Ratios of a struck metal bar: nothing here is a whole-number harmonic.
    partials = ((1.0, 1.0), (2.76, 0.32), (5.40, 0.14), (8.93, 0.06), (13.3, 0.03))

    for midi, when, amp in melody:
        f0 = 440.0 * 2 ** ((midi - 69) / 12.0)
        dur = min(2.6, total - when)
        if dur <= 0.05:
            continue
        note = np.zeros(int(dur * SR), np.float32)
        for ratio, pamp in partials:
            f = f0 * ratio
            if f > SR * 0.45:
                continue
            p = A.osc(f, dur, "sine")
            p *= np.exp(-np.linspace(0, 4.5 + ratio * 0.9, len(p), dtype=np.float32))
            note += p * pamp
        # Comb tooth pluck.
        click = A.bandpass(RNG.normal(0, 1, len(note)).astype(np.float32), 2500, 9000)
        click *= np.exp(-np.linspace(0, 90, len(note), dtype=np.float32))
        note = A.norm(note, 0.9) + click * 0.18
        i = int(when * SR)
        seg = note[:max(0, n - i)]
        y[i:i + len(seg)] += seg * amp

    y = A.highpass(y, 260)
    y = A.wow_flutter(y, depth=0.0035, hz=0.55)      # a spring losing its tension
    hiss = A.bandpass(RNG.normal(0, 1, n).astype(np.float32), 2000, 9000)
    y = A.mix(A.norm(y, 0.85), A.gain(hiss, -34))
    y = A.reverb(y, amount=0.4, seconds=3.0, decay=1.8, lo=250, hi=9000)
    return A.norm(A.fade(y, 0.01, 1.2), 0.8)


def sfx_ending():
    """End-card bed: the lullaby motif dissolving into the drone."""
    box = A.resample_ratio(sfx_music_box(), 0.72)
    box = A.gain(box, -5)
    dr = sfx_drone_dread()
    n = max(len(box), len(dr))
    dr = np.resize(A.to_stereo(dr), (n, 2))
    y = A.mix(box, dr * 0.6)
    y = A.lowpass(y, 4200)
    return A.norm(A.fade(y, 1.5, 3.0), 0.75)


# ---------------------------------------------------------------- registry

BANK = {
    # jump scares
    "scream_mara": (sfx_scream_mara, 4, False),
    "scream_mara2": (sfx_scream_mara_2, 4, False),
    "scream_child": (sfx_scream_child, 3, False),
    "giggle": (sfx_giggle, 3, False),
    "sub_boom": (sfx_sub_boom, 4, True),
    "stinger_a": (lambda: sfx_stinger(2, 2.6, 98), 3, False),
    "stinger_b": (lambda: sfx_stinger(17, 2.2, 116), 3, False),
    "stinger_c": (lambda: sfx_stinger(31, 3.0, 73), 3, False),
    "riser": (sfx_riser, 2, False),

    # loops
    "drone_calm": (sfx_drone_calm, 2, False),
    "drone_dread": (sfx_drone_dread, 2, False),
    "drone_chaos": (sfx_drone_chaos, 2, False),
    "water_rise": (sfx_water_rise, 1, False),
    "light_buzz": (sfx_light_buzz, 1, False),
    "whisper_bed": (sfx_whisper_bed, 1, False),
    "heart_slow": (lambda: sfx_heartbeat(58, 8, 0.0), 2, True),
    "heart_fast": (lambda: sfx_heartbeat(122, 12, 1.0), 2, True),
    "breath_calm": (lambda: sfx_breath(0.0, 5), 1, False),
    "breath_panic": (lambda: sfx_breath(1.0, 8), 2, False),

    # world
    "door_creak1": (lambda: sfx_door_creak(1, 2.2), 2, False),
    "door_creak2": (lambda: sfx_door_creak(9, 1.6), 2, False),
    "door_slam": (sfx_door_slam, 3, False),
    "knock": (sfx_knock, 3, False),
    "scrape": (sfx_scrape, 2, False),
    "bone": (sfx_bone, 2, False),
    "static": (sfx_static_burst, 2, False),
    "radio_tune": (sfx_radio_tune, 1, False),
    "music_box": (sfx_music_box, 3, False),
    "ending": (sfx_ending, 2, False),
}

for _i in range(4):
    BANK[f"step_wet{_i + 1}"] = ((lambda k: lambda: footstep(True, 50 + k))(_i), 2, True)
    BANK[f"step_dry{_i + 1}"] = ((lambda k: lambda: footstep(False, 80 + k))(_i), 2, True)
for _i in range(3):
    BANK[f"drip{_i + 1}"] = ((lambda k: lambda: sfx_drip(300 + k))(_i), 2, True)


def main():
    os.makedirs(OUT, exist_ok=True)
    only = set(sys.argv[1:])
    total = 0
    for name, (fn, quality, mono) in BANK.items():
        if only and name not in only:
            continue
        y = fn()
        path = os.path.join(OUT, f"{name}.ogg")
        A.encode_ogg(y, path, quality=quality, mono=mono)
        size = os.path.getsize(path)
        total += size
        print(f"  {name:14} {len(y)/SR:6.2f}s  {size/1024:7.1f} KB")
    print(f"\nsfx total {total/1024/1024:.2f} MB")


if __name__ == "__main__":
    main()
