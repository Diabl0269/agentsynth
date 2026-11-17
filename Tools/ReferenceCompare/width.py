# Stereo width and amplitude-beating of held unison saw stacks, for both synths' renders of
# ReferenceCompareRender.RenderUnisonWidthOurs (agentsynth_w<pitch>_r<run>.wav) and the other synth rendered offline to
# <REF>_w<pitch>_r<run>.wav. Usage: python3 width.py <folder> [pitch...]   (REF=reference prefix, default "reference")
# Per file, over the settled window 1..7 s: side/mid power ratio (dB) per band, L/R correlation per band, and the beating
# depth of the stack (envelope of the band-limited mid signal: (p95-p5)/(p95+p5) of the 5 ms-smoothed Hilbert envelope).
# Numbers are medians over the runs of one pitch, since every run restarts the voices at random phases.
import sys, os, numpy as np
from scipy.io import wavfile
from scipy.signal import hilbert, butter, sosfilt, sosfiltfilt

D = sys.argv[1]
PITCHES = [int(p) for p in sys.argv[2:]] or [36, 57, 72]
REF = os.environ.get("REF", "reference")
BANDS = [(60, 500), (500, 2000), (2000, 8000), (8000, 18000)]


def bp(x, sr, lo, hi):
    return sosfiltfilt(butter(4, [lo, hi], btype="bandpass", fs=sr, output="sos"), x)


def measure(path):
    sr, x = wavfile.read(path)
    x = x.astype(float)
    if x.ndim == 1:
        x = np.c_[x, x]
    x = x[int(1.0 * sr): int(7.0 * sr)]
    L, R = x[:, 0], x[:, 1]
    out = {"rms_db": 10 * np.log10(np.mean(L**2 + R**2) / 2 + 1e-30)}
    for lo, hi in BANDS:
        l, r = bp(L, sr, lo, hi), bp(R, sr, lo, hi)
        m, s = (l + r) / 2, (l - r) / 2
        out[f"sm_{lo}"] = 10 * np.log10((np.mean(s**2) + 1e-30) / (np.mean(m**2) + 1e-30))
        out[f"corr_{lo}"] = np.corrcoef(l, r)[0, 1]
        for name, sig in (("mid", m), ("L", l)):
            env = np.abs(hilbert(sig))
            k = int(0.005 * sr)
            env = np.convolve(env, np.ones(k) / k, mode="same")[k:-k]
            p5, p95 = np.percentile(env, [5, 95])
            out[f"beat_{name}_{lo}"] = (p95 - p5) / (p95 + p5)
    out["corr_all"] = np.corrcoef(L, R)[0, 1]
    return out


def med(who, pitch):
    rows = []
    for r in range(8):
        p = f"{D}/{who}_w{pitch}_r{r}.wav"
        if os.path.exists(p):
            rows.append(measure(p))
    return {k: float(np.median([row[k] for row in rows])) for k in rows[0]} if rows else None


if __name__ == "__main__":
    for pitch in PITCHES:
        a, b = med("agentsynth", pitch), med(REF, pitch)
        print(f"--- MIDI {pitch}")
        for k in (b or a):
            print(f"{k:14s} ours {a[k] if a else float('nan'):9.3f}   ref {b[k] if b else float('nan'):9.3f}")
