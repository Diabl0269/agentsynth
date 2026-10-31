# Per-voice frequencies and start phases of a held unison saw stack, for both synths' renders of
# ReferenceCompareRender.RenderUnisonDetuneMap / RenderUnisonLikeForLike. Usage: python3 unison.py <folder> <case>...
# [reference-prefix, set with REF=...]. Frequencies come from the sustain-window spectrum around the fundamental
# (parabolic peak interpolation); start phases from a least-squares fit of the fundamental of each voice over the first
# 0.4 s, back-extrapolated to the first non-zero sample of the render.
import sys, os, numpy as np
from scipy.io import wavfile
from scipy.signal import get_window, find_peaks

D = sys.argv[1]
CASES = sys.argv[2:]
REF = os.environ.get("REF", "reference")
F0 = 440.0
ON = 0.5


def load(who, case):
    sr, x = wavfile.read(f"{D}/{who}_{case}.wav")
    return sr, x.astype(float)


def voices(x, sr, lo=1.0, hi=10.3):
    y = x[int(lo * sr): int(hi * sr)]
    w = get_window("blackmanharris", len(y))
    f = np.fft.rfftfreq(len(y), 1 / sr)
    band = (f > 380) & (f < 500)
    P = (np.abs(np.fft.rfft(y[:, 0] * w)) ** 2 + np.abs(np.fft.rfft(y[:, 1] * w)) ** 2)
    pk, _ = find_peaks(P[band], height=P[band].max() * 1e-3, distance=int(0.35 / (f[1] - f[0])))
    idx = np.nonzero(band)[0][pk]
    out = []
    for i in idx:
        a, b, c = np.log(P[i - 1: i + 2] + 1e-30)
        d = 0.5 * (a - c) / (a - 2 * b + c)
        out.append(f[i] + d * (f[1] - f[0]))
    return np.array(out)


def phases(x, sr, freqs, ch):
    nz = np.nonzero(np.abs(x[:, 0]) + np.abs(x[:, 1]) > 1e-6)[0]
    start = nz[nz > int(ON * sr) - 200][0]
    a, b = start + int(0.02 * sr), start + int(0.82 * sr)
    t = np.arange(a, b) / sr
    A = np.concatenate([np.c_[np.sin(2 * np.pi * f * t), np.cos(2 * np.pi * f * t)] for f in freqs], axis=1)
    c = np.linalg.lstsq(A, x[a:b, ch], rcond=None)[0]
    t0 = start / sr
    res = []
    for k, f in enumerate(freqs):
        s, co = c[2 * k], c[2 * k + 1]
        amp = np.hypot(s, co)
        ph = (np.degrees(np.arctan2(co, s)) - 360.0 * f * ((a / sr) - t0)) % 360.0
        res.append((amp, ph))
    return res, start / sr * 1000 - ON * 1000


for case in CASES:
    for who in ("agentsynth", REF):
        p = f"{D}/{who}_{case}.wav"
        if not os.path.exists(p):
            continue
        sr, x = load(who, case)
        fr = voices(x, sr)
        cents = 1200 * np.log2(fr / F0)
        print(f"{case:12s} {who:11s} n={len(fr)} cents {np.round(cents, 1).tolist()}")
        if os.environ.get("PHASES"):
            for ch, name in ((0, "L"), (1, "R")):
                ph, lat = phases(x, sr, fr, ch)
                print(f"      {name} start+{lat:.2f} ms amp/phase(deg at first sample):",
                      [(round(a, 3), round(p_)) for a, p_ in ph])
