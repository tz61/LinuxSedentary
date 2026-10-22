#!/usr/bin/env python3
"""Synthesizes the notification sounds in data/sounds/.

Soft bell-like chimes built from a few decaying partials, so the app ships
its own sounds with no licensing questions. Run it again after changing a
recipe:

    python3 tools/make_sounds.py
"""

import math
import os
import struct
import wave

RATE = 22050  # plenty for chimes; keeps the embedded files small
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "sounds")

# Equal-tempered note frequencies (Hz).
NOTE = {
    "C5": 523.25, "D5": 587.33, "E5": 659.25, "G5": 783.99, "A5": 880.00,
    "C6": 1046.50, "E6": 1318.51, "G4": 392.00, "C4": 261.63,
}

# (frequency ratio, relative amplitude, decay per second): a struck bell/marimba.
PARTIALS = [(1.0, 1.0, 3.2), (2.0, 0.32, 5.5), (3.0, 0.12, 8.0), (4.16, 0.05, 11.0)]


def render(notes, length, peak):
    """notes: (start seconds, note name, gain). Returns 16-bit PCM frames."""
    n = int(length * RATE)
    buf = [0.0] * n
    attack = int(0.004 * RATE)
    for start, name, gain in notes:
        f0 = NOTE[name]
        first = int(start * RATE)
        for i in range(first, n):
            t = (i - first) / RATE
            env = min(1.0, (i - first) / attack) if attack else 1.0
            s = 0.0
            for ratio, amp, decay in PARTIALS:
                s += amp * math.exp(-decay * t) * math.sin(2 * math.pi * f0 * ratio * t)
            buf[i] += gain * env * s
    # Fade the tail to silence to avoid a click, then normalize.
    fade = int(0.04 * RATE)
    for i in range(fade):
        buf[n - 1 - i] *= i / fade
    top = max(abs(x) for x in buf) or 1.0
    scale = peak * 32767 / top
    return b"".join(struct.pack("<h", int(round(x * scale))) for x in buf)


SOUNDS = {
    # The work countdown starts again: a soft low fourth, after the break-over chime.
    "countdown": dict(notes=[(0.0, "G4", 0.8), (0.14, "C5", 0.9)], length=1.1, peak=0.55),
    # One minute left: a single soft note.
    "prewarn": dict(notes=[(0.0, "E5", 1.0)], length=0.9, peak=0.45),
    # Break due: an attention-catching rising pair.
    "reminder": dict(notes=[(0.0, "E5", 1.0), (0.22, "C6", 0.9), (0.44, "E5", 0.5), (0.66, "C6", 0.45)],
                     length=1.6, peak=0.75),
    # Break starts: a bright upward arpeggio.
    "break-start": dict(notes=[(0.0, "C5", 0.9), (0.10, "E5", 0.9), (0.20, "G5", 0.9), (0.30, "C6", 1.0)],
                        length=1.4, peak=0.7),
    # Break over / welcome back: a resolving chord.
    "break-over": dict(notes=[(0.0, "G5", 0.8), (0.16, "C5", 0.7), (0.16, "E5", 0.6), (0.16, "C6", 0.9)],
                       length=1.6, peak=0.7),
}


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, recipe in SOUNDS.items():
        path = os.path.join(OUT_DIR, name + ".wav")
        with wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(render(**recipe))
        print("wrote", os.path.relpath(path))


if __name__ == "__main__":
    main()
