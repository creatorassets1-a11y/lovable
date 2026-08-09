#!/usr/bin/env python3
"""Bake every spoken line into a processed OGG.

espeak-ng gives us a dry, flat read. That read is then put through a per-character
chain that decides *where* the voice is coming from — a speaker grille, the far end
of a corridor, or somewhere behind the wall you are standing next to.

Run: python3 tools/gen_voice.py
Out: android/app/src/main/assets/game/audio/vo/<id>.ogg  (+ manifest entry)
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import audio_dsp as A  # noqa: E402
from script import all_lines  # noqa: E402

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "android", "app", "src",
                       "main", "assets", "game", "audio", "vo")

# espeak-ng synthesis settings per character: (voice, words-per-minute, pitch 0-99, amplitude)
SPEAK = {
    "radio":   ("en-us+announcer", 158, 42, 180),
    "adam":    ("en-us+m3", 148, 30, 175),
    "ellie":   ("en-us+f5", 122, 88, 180),
    "mara":    ("en-us+f2", 98, 12, 190),
    "whisper": ("en-us+whisper", 108, 55, 200),
}


def speak(text, voice, wpm, pitch, amp, gap=8):
    """Render one dry TTS take to a float array."""
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        path = f.name
    try:
        subprocess.run(
            ["espeak-ng", "-v", voice, "-s", str(wpm), "-p", str(pitch),
             "-a", str(amp), "-g", str(gap), "-w", path, text],
            check=True, capture_output=True,
        )
        return A.read_wav(path)
    finally:
        os.unlink(path)


def take(character, text, **over):
    v, s, p, a = SPEAK[character]
    return speak(text, over.get("voice", v), over.get("wpm", s),
                 over.get("pitch", p), over.get("amp", a))


# ----------------------------------------------------------------- chains

def chain_radio(text, _shout):
    """Small speaker in a wet hallway: bandlimited, hissing, slightly unstable."""
    dry = take("radio", text)
    y = A.radio(dry, noise_db=-27, drive=2.6)
    y = A.wow_flutter(y, depth=0.0016, hz=0.9)
    y = A.reverb(y, amount=0.16, seconds=1.1, decay=6.0, lo=300, hi=4000)
    return A.norm(A.fade(y, 0.005, 0.08), 0.85)


def chain_adam(text, _shout):
    """Interior monologue. Kept dry and close so he stays the human anchor."""
    dry = take("adam", text)
    y = A.pitch_shift(dry, -1.2)
    y = A.peaking(y, 180, 0.9, 3.5)      # chest
    y = A.peaking(y, 2600, 1.4, -3.0)    # take the plastic off the top
    y = A.lowpass(y, 7200)
    y = A.reverb(y, amount=0.17, seconds=0.75, decay=7.0, lo=200, hi=5000)
    return A.norm(A.fade(y, 0.006, 0.10), 0.88)


def chain_ellie(text, _shout):
    """A child at the far end of a long wet corridor, plus her own breath.

    The breath layer is a whispered take of the same words sitting just under
    the voice — the ear reads it as her being much closer than she sounds.
    """
    dry = take("ellie", text)
    voice = A.pitch_shift(dry, 3.2)
    voice = A.highpass(voice, 230)
    voice = A.peaking(voice, 3400, 1.6, 4.0)

    breath = take("whisper", text, wpm=118, pitch=80)
    breath = A.pitch_shift(breath, 3.2)
    breath = A.highpass(breath, 900)
    breath = A.gain(breath, -15)

    n = max(len(voice), len(breath))
    body = A.mix(voice, breath)[:n]
    y = A.reverb(body, amount=0.44, seconds=2.8, decay=2.2, lo=240, hi=6500, predelay=0.02)
    y = A.widen(y, 14)
    return A.norm(A.fade(y, 0.01, 0.25), 0.82)


def chain_mara(text, shout):
    """Three stacked takes: the voice, a sub an octave under, a whisper on top.

    Reverse reverb makes each line arrive fractionally before it is spoken, and
    a slow ring modulation keeps it from ever sounding like a person.
    """
    dry = take("mara", text)

    body = A.pitch_shift(dry, -5.5)
    body = A.formant_shift(body, -3.5)
    body = A.peaking(body, 110, 0.8, 5.0)

    sub = A.pitch_shift(dry, -12.0)
    sub = A.lowpass(sub, 900)
    sub = A.gain(sub, -7)

    hiss = take("whisper", text, wpm=92, pitch=20)
    hiss = A.highpass(hiss, 1600)
    hiss = A.gain(hiss, -13)

    y = A.mix(body, sub, hiss)
    y = A.soft_clip(y, 2.6 if not shout else 5.0)
    y = A.ringmod(y, 43 if not shout else 29, depth=0.16)

    if shout:
        # Shouts are jump-scare payloads: they must land on frame one, so no
        # reverse-reverb swell to telegraph them. Just impact and a short tail.
        y = A.bitcrush(y, bits=7)
        y = A.gain(y, 3)
        y = A.soft_clip(y, 2.0)
        y = A.reverb(y, amount=0.30, seconds=1.8, decay=3.4, lo=60, hi=6000)
        y = A.widen(y, 12)
        return A.norm(A.fade(y, 0.001, 0.25), 0.98)

    y = A.reverse_reverb(y, amount=0.55, seconds=1.5, decay=3.2)
    y = A.reverb(y, amount=0.42, seconds=3.2, decay=1.7, lo=60, hi=5200, predelay=0.015)
    y = A.widen(y, 22)
    return A.norm(A.fade(y, 0.02, 0.35), 0.92)


def chain_whisper(text, _shout):
    """Three detuned, offset, hard-panned takes so it never localises."""
    layers = []
    for idx, (semis, position, offset_s) in enumerate(
            ((0.0, -0.85, 0.00), (-1.5, 0.85, 0.05), (2.0, 0.05, 0.11))):
        t = take("whisper", text, wpm=104 + idx * 7, pitch=48 + idx * 9)
        t = A.pitch_shift(t, semis)
        t = A.highpass(t, 620)
        t = A.pan(t, position)
        layers.append(A.pad(t, before=offset_s))
    y = A.mix(*layers)
    y = A.reverb(y, amount=0.58, seconds=2.6, decay=2.6, lo=500, hi=9000)
    y = A.gain(y, -4)
    return A.norm(A.fade(y, 0.05, 0.4), 0.7)


CHAINS = {
    "radio": chain_radio,
    "adam": chain_adam,
    "ellie": chain_ellie,
    "mara": chain_mara,
    "whisper": chain_whisper,
}

# Voices that carry no stereo information are shipped mono: the engine pans them
# positionally at runtime anyway, and it halves the file size.
MONO = {"radio": True, "adam": True, "ellie": False, "mara": False, "whisper": False}
QUALITY = {"radio": 1, "adam": 2, "ellie": 2, "mara": 3, "whisper": 1}


def speech_end(y, floor_db=-38, sr=A.SR):
    """Seconds until the last of the actual words, ignoring the reverb tail.

    Subtitles are held for this, not for the file length — a three second tail
    would otherwise leave text on screen long after the line has finished.
    """
    m = A.to_mono(y)
    win = max(1, int(0.02 * sr))
    trimmed = m[:len(m) - len(m) % win]
    if not trimmed.size:
        return round(len(m) / sr, 3)
    rms = np.sqrt((trimmed.reshape(-1, win) ** 2).mean(axis=1) + 1e-12)
    thresh = rms.max() * (10 ** (floor_db / 20.0))
    above = np.nonzero(rms > thresh)[0]
    if not above.size:
        return round(len(m) / sr, 3)
    return round(float((above[-1] + 1) * win) / sr, 3)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    manifest = {}
    lines = all_lines()

    for n, (line_id, character, text, shout) in enumerate(lines, 1):
        y = CHAINS[character](text, shout)
        out = os.path.join(OUT_DIR, f"{line_id}.ogg")
        A.encode_ogg(y, out, quality=QUALITY[character], mono=MONO[character])
        manifest[line_id] = {
            "c": character,
            "t": text,
            "d": round(len(y) / A.SR, 3),
            "s": speech_end(y),
        }
        print(f"[{n:3}/{len(lines)}] {line_id:6} {character:8} "
              f"{len(y)/A.SR:5.2f}s  {os.path.getsize(out)/1024:6.1f} KB")

    man_path = os.path.join(OUT_DIR, "..", "..", "js", "vo-manifest.js")
    with open(man_path, "w") as f:
        f.write("// Generated by tools/gen_voice.py — do not edit by hand.\n")
        f.write("export const VO = ")
        json.dump(manifest, f, indent=0, separators=(",", ":"))
        f.write(";\n")

    total = sum(os.path.getsize(os.path.join(OUT_DIR, f))
                for f in os.listdir(OUT_DIR) if f.endswith(".ogg"))
    print(f"\n{len(manifest)} lines, {total/1024/1024:.2f} MB total")


if __name__ == "__main__":
    main()
