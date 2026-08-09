#!/usr/bin/env python3
"""Record the cast with Piper neural TTS, then perform the result.

Piper gives a natural read; the character comes from what happens afterwards.
Ruth is close and dry so she is the one human thing in the building. Control is
a speaker in a helmet. The Matron is pitched down with her own breath layered
under her, and arrives through the room rather than at you. Tom is small, far
away, and behind something.

Also emits game/vo_manifest.h so the engine can show subtitles and hold them for
the spoken length rather than the file length.

Run: python3 tools/gen_voice.py [id ...]
"""
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import audio_dsp as A  # noqa: E402
from matron_script import all_lines  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
OUT = os.path.join(ROOT, "android", "app", "src", "main", "assets", "audio", "vo")
MANIFEST = os.path.join(ROOT, "android", "app", "src", "main", "cpp", "game",
                        "vo_manifest.h")
VOICES = os.path.join(os.path.dirname(__file__), "voices")

# character -> (model, length_scale, noise_scale)
# length_scale > 1 slows the read down, which is most of "gravitas".
MODEL = {
    "ruth":    ("en_GB-alba-medium", 1.02, 0.60),
    "control": ("en_GB-alan-medium", 0.98, 0.55),
    "matron":  ("en_GB-jenny_dioco-medium", 1.30, 0.70),
    "tom":     ("en_GB-cori-high", 1.06, 0.65),
    "tape":    ("en_US-ryan-high", 1.00, 0.55),
}

_loaded = {}


def synth(text, character):
    from piper import PiperVoice, SynthesisConfig
    model, length, noise = MODEL[character]
    if model not in _loaded:
        _loaded[model] = PiperVoice.load(os.path.join(VOICES, model + ".onnx"))
    voice = _loaded[model]

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        path = f.name
    try:
        cfg = SynthesisConfig(length_scale=length, noise_scale=noise,
                              noise_w_scale=0.8)
        with wave.open(path, "wb") as w:
            voice.synthesize_wav(text, w, syn_config=cfg)
        return A.read_wav(path)
    finally:
        os.unlink(path)


# ------------------------------------------------------------------ chains

def chain_ruth(text, _shout):
    """Close, dry, a little chesty. She is the anchor; do not process her much."""
    dry = synth(text, "ruth")
    y = A.peaking(dry, 190, 0.9, 3.0)
    y = A.peaking(y, 3200, 1.2, -2.0)
    y = A.lowpass(y, 9500)
    y = A.reverb(y, amount=0.13, seconds=0.6, decay=8.0, lo=220, hi=6000)
    return A.norm(A.fade(y, 0.005, 0.09), 0.90)


def chain_control(text, _shout):
    """A radio inside a helmet: bandlimited, compressed, occasionally dropping."""
    dry = synth(text, "control")
    y = A.radio(dry, noise_db=-30, drive=2.4)
    y = A.wow_flutter(y, depth=0.0012, hz=1.3)
    y = A.reverb(y, amount=0.10, seconds=0.5, decay=9.0, lo=350, hi=3400)
    return A.norm(A.fade(y, 0.004, 0.07), 0.84)


def chain_matron(text, shout):
    """Pitched down with her own breath under it, arriving through the ward.

    Kept intelligible on purpose. A monster you can understand is worse than one
    you cannot, because then it is saying something to you.
    """
    dry = synth(text, "matron")
    body = A.pitch_shift(dry, -3.6)
    body = A.formant_shift(body, -2.0)
    body = A.peaking(body, 130, 0.8, 4.0)

    sub = A.pitch_shift(dry, -10.0)
    sub = A.lowpass(sub, 700)
    sub = A.gain(sub, -13)

    breath = A.highpass(dry, 2200)
    breath = A.gain(breath, -17)

    y = A.mix(body, sub, breath)
    y = A.soft_clip(y, 1.7 if not shout else 3.4)
    if shout:
        y = A.gain(y, 3)
        y = A.soft_clip(y, 1.9)
        y = A.reverb(y, amount=0.30, seconds=2.0, decay=3.2, lo=70, hi=6500)
        return A.norm(A.fade(y, 0.001, 0.22), 0.99)

    y = A.reverb(y, amount=0.34, seconds=2.6, decay=2.2, lo=80, hi=5200, predelay=0.012)
    y = A.widen(y, 16)
    return A.norm(A.fade(y, 0.012, 0.30), 0.93)


def chain_tom(text, _shout):
    """Eleven, exhausted, and behind a wall."""
    dry = synth(text, "tom")
    y = A.pitch_shift(dry, 2.6)
    y = A.highpass(y, 260)
    y = A.lowpass(y, 6200)                      # muffled by whatever he is under
    y = A.peaking(y, 900, 1.4, -3.0)
    y = A.reverb(y, amount=0.40, seconds=2.4, decay=2.6, lo=260, hi=6000)
    y = A.widen(y, 12)
    return A.norm(A.fade(y, 0.01, 0.26), 0.86)


def chain_tape(text, _shout):
    """A 1994 dictaphone that has been in a flooded basement since."""
    dry = synth(text, "tape")
    y = A.bandpass(dry, 300, 4200, order=6)
    y = A.wow_flutter(y, depth=0.0042, hz=0.6)
    y = A.soft_clip(y, 2.0)
    hiss = A.bandpass(A.noise(len(y) / A.SR + 0.05, "white"), 1500, 7000)
    y = A.mix(A.to_mono(y), A.gain(A.to_mono(hiss)[:len(y)], -30))
    y = A.reverb(y, amount=0.12, seconds=0.7, decay=7.0, lo=300, hi=4000)
    return A.norm(A.fade(y, 0.02, 0.16), 0.82)


CHAINS = {
    "ruth": chain_ruth, "control": chain_control, "matron": chain_matron,
    "tom": chain_tom, "tape": chain_tape,
}
MONO = {"ruth": True, "control": True, "matron": False, "tom": False, "tape": True}
QUALITY = {"ruth": 3, "control": 2, "matron": 3, "tom": 3, "tape": 2}


def speech_end(y, floor_db=-38, sr=A.SR):
    """Seconds until the last word, ignoring the reverb tail — subtitles are
    held for this, not for the file length."""
    m = A.to_mono(y)
    win = max(1, int(0.02 * sr))
    trimmed = m[:len(m) - len(m) % win]
    if not trimmed.size:
        return round(len(m) / sr, 3)
    rms = np.sqrt((trimmed.reshape(-1, win) ** 2).mean(axis=1) + 1e-12)
    above = np.nonzero(rms > rms.max() * (10 ** (floor_db / 20.0)))[0]
    if not above.size:
        return round(len(m) / sr, 3)
    return round(float((above[-1] + 1) * win) / sr, 3)


def cpp_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    os.makedirs(OUT, exist_ok=True)
    only = set(sys.argv[1:])
    lines = all_lines()
    manifest = []
    total = 0

    for n, (line_id, character, text, shout) in enumerate(lines, 1):
        if only and line_id not in only:
            continue
        y = CHAINS[character](text, shout)
        path = os.path.join(OUT, f"{line_id}.ogg")
        A.encode_ogg(y, path, sr=A.SR, quality=QUALITY[character],
                     mono=MONO[character])
        # Everything is resampled to the engine's mix rate at build time so the
        # runtime never has to.
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", path,
                        "-ar", "22050", "-c:a", "libvorbis",
                        "-qscale:a", str(QUALITY[character]), path + ".tmp.ogg"],
                       check=True)
        os.replace(path + ".tmp.ogg", path)

        size = os.path.getsize(path)
        total += size
        manifest.append((line_id, character, text, speech_end(y)))
        print(f"[{n:3}/{len(lines)}] {line_id:6} {character:8} "
              f"{len(y)/A.SR:5.2f}s {size/1024:6.1f} KB  {text[:44]}")

    if not only:
        with open(MANIFEST, "w") as f:
            f.write("// Generated by tools/gen_voice.py - do not edit by hand.\n")
            f.write("#pragma once\n\nnamespace hl {\nnamespace story {\n\n")
            f.write("struct VoiceLine { const char* id; const char* who;\n"
                    "                   const char* text; float seconds; };\n\n")
            f.write("inline const VoiceLine VO_LINES[] = {\n")
            for line_id, character, text, dur in manifest:
                f.write(f'    {{"{line_id}", "{character}", '
                        f'"{cpp_escape(text)}", {dur:.2f}f}},\n')
            f.write("};\n")
            f.write(f"inline constexpr int VO_COUNT = {len(manifest)};\n\n")
            f.write("}  // namespace story\n}  // namespace hl\n")
        print(f"\n{len(manifest)} lines, {total/1024/1024:.2f} MB -> {MANIFEST}")


if __name__ == "__main__":
    main()
