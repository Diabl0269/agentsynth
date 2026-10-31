# Envelope shapes of AgentSynth and a reference synth from the ReferenceCompareRender.RenderEnvelopeShapes renders:
# the demodulated amplitude of a sustained sine through the attack, decay and release, as the time each stage takes to
# cross 10/25/50/75/90/99 % of its travel. Usage: python3 envelope.py <folder> [reference-prefix, default reference]
import sys, os, numpy as np
from scipy.io import wavfile
from scipy.signal import hilbert

D = sys.argv[1]
REF = sys.argv[2] if len(sys.argv) > 2 else "reference"
SR = 48000
LEVELS = (0.1, 0.25, 0.5, 0.75, 0.9, 0.99)
# case prefix -> (note on s, note off s); the number is the swept time in tenths of a millisecond
SPANS = {"envatk": (0.3, 1.3), "envrel": (0.3, 1.0), "envdec": (0.3, 2.3), "envdsl": (0.3, 2.3)}


def load(path):
    sr, x = wavfile.read(path)
    return np.abs(hilbert(x[:, 0].astype(float)))


def crossing(env, start, rising, level):
    seg = env[start:]
    hit = np.nonzero(seg >= level if rising else seg <= level)[0]
    return hit[0] / SR * 1000.0 if len(hit) else float("nan")


def stage_times(env, start, rising, lo, hi):
    return [crossing(env, start, rising, lo + (hi - lo) * f) for f in LEVELS]


def report(name):
    kind = name[:6]
    on, off = SPANS[kind]
    out = {}
    for who in ("agentsynth", REF):
        p = f"{D}/{who}_{name}.wav"
        if not os.path.exists(p):
            return
        env = load(p)
        # onset: first sample over 0.5 % of the note's peak, so a fixed offset between the synths is not read as shape
        peak = env[int(on * SR): int(off * SR)].max()
        onset = int(on * SR) + np.nonzero(env[int(on * SR):] > 0.005 * peak)[0][0]
        if kind == "envatk":
            top = env[onset + int(0.5 * SR): int(off * SR) - 100].mean()
            out[who] = (onset / SR * 1000.0 - on * 1000.0, stage_times(env, onset, True, 0.0, top), top)
        elif kind == "envrel":
            top = env[int(off * SR) - int(0.1 * SR): int(off * SR) - 100].mean()
            s = int(off * SR) - 1
            out[who] = (onset / SR * 1000.0 - on * 1000.0, stage_times(env, s, False, top, 0.0), top)
        else:
            top = env[onset: onset + int(0.02 * SR)].max()
            sus = env[int(off * SR) - int(0.2 * SR): int(off * SR) - 100].mean()
            s = onset + int(0.002 * SR)
            out[who] = (onset / SR * 1000.0 - on * 1000.0, stage_times(env, s, False, top, sus), sus / top)
    fmt = lambda v: " ".join(f"{t:8.2f}" for t in v)
    print(f"{name}  (ms to reach {', '.join(str(int(l * 100)) for l in LEVELS)} % of the stage's travel)")
    for who, (lat, times, top) in out.items():
        extra = f"sustain/peak {top:.3f}" if kind in ("envdec", "envdsl") else f"level {top:.0f}"
        print(f"   {who:11s} onset+{lat:6.2f} ms  {fmt(times)}   {extra}")


names = sorted({os.path.basename(f)[11:-4] for f in os.listdir(D) if f.startswith("agentsynth_env") and f[11:17] in SPANS})
order = {"envatk": 0, "envrel": 1, "envdec": 2, "envdsl": 3}
for n in sorted(names, key=lambda n: (order[n[:6]], int(n.split("_")[1]))):
    report(n)
