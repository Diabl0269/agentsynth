#!/usr/bin/env python3
"""Writes hum-melody.agsproj/Audio/hum.wav: a synthetic hummed melody (C major scale, 0.5 s a note) as mono 16-bit
22050 Hz, so the Convert to melody scenario has a take whose notes are known. Standard library only."""
import math
import random
import struct
import wave
from pathlib import Path

RATE = 22050
NOTES = [60, 62, 64, 65, 67, 69, 71, 72]
NOTE_SEC = 0.5
VIB_CENTS, VIB_HZ, BREATH = 20.0, 5.5, 0.1
# Each note a little off pitch, by at most 12 cents, in a fixed pattern so neighbours still sit about a semitone apart.
DETUNE_CENTS = [8, -12, 10, -9, 12, -10, 6, -8]


def render():
    rng = random.Random(1234)
    pitch = [n + cents / 100.0 for n, cents in zip(NOTES, DETUNE_CENTS)]
    total = int((len(NOTES) * NOTE_SEC + 0.1) * RATE)
    out = []
    phase = 0.0
    for i in range(total):
        t = i / RATE
        index = int(t / NOTE_SEC)
        amp = 0.0
        midi = 0.0
        if index < len(NOTES):
            into = t - index * NOTE_SEC
            amp = min(1.0, into / 0.03, (NOTE_SEC - into) / 0.03)
            midi = pitch[index]
            if index == 0 and into < 0.08:
                midi -= 1.0 - into / 0.08
            elif index > 0 and into < 0.06:
                midi = pitch[index - 1] + (pitch[index] - pitch[index - 1]) * into / 0.06
        sample = 0.0
        if amp > 0.0:
            midi += VIB_CENTS / 100.0 * math.sin(2 * math.pi * VIB_HZ * t) * min(1.0, max(0.0, (t - 0.3) / 0.3))
            f0 = 440.0 * 2 ** ((midi - 69.0) / 12.0)
            phase += 2 * math.pi * f0 / RATE
            sample = amp * sum(math.sin(h * phase) / h ** 1.6 for h in range(1, 16) if h * f0 < RATE / 2)
            sample += BREATH * rng.gauss(0, 1) * amp * 0.3
        out.append(sample)
    peak = max(abs(v) for v in out) or 1.0
    return [v / peak * 0.7 for v in out]


def main():
    target = Path(__file__).parent / "hum-melody.agsproj" / "Audio" / "hum.wav"
    target.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(target), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(b"".join(struct.pack("<h", int(v * 32767)) for v in render()))


if __name__ == "__main__":
    main()
