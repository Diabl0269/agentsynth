# Level-matched A/B distance between AgentSynth renders and a reference synth's renders, worst case first.
# Input: the folder written by the ReferenceCompareRender tests (AGENTSYNTH_REFERENCE_COMPARE_DIR), which holds
# agentsynth_<case>.wav and reference_<case>.wav pairs. Usage: python3 match.py <folder>
import sys, glob, os, numpy as np
from scipy.io import wavfile
from scipy.signal import stft, fftconvolve
D=sys.argv[1]
def db(v): return 20*np.log10(np.maximum(v,1e-12))
def rms(x): return np.sqrt(np.mean(x**2))
EDGES=[20,80,160,320,640,1280,2560,5120,10240,20000]
def bands(y,sr):
    f,t,Z=stft(y,sr,nperseg=4096,noverlap=3072); P=(np.abs(Z)**2).mean(axis=1)
    return np.array([P[(f>=a)&(f<b)].sum() for a,b in zip(EDGES,EDGES[1:])])
rows=[]
for p in sorted(glob.glob(f"{D}/agentsynth_*.wav")):
    c=os.path.basename(p)[11:-4]; q=f"{D}/reference_{c}.wav"
    if not os.path.exists(q): continue
    sr,a=wavfile.read(p); _,b=wavfile.read(q); a=a.astype(float); b=b.astype(float)
    if a.ndim==1: a=np.c_[a,a]
    if b.ndim==1: b=np.c_[b,b]
    n=min(len(a),len(b)); a,b=a[:n],b[:n]
    ma,mb=a.mean(1),b.mean(1)
    if rms(ma)<1e-7 or rms(mb)<1e-7: continue
    lvl=db(rms(a))-db(rms(b))
    g=rms(b)/rms(a); a2=a*g; ma2=ma*g
    # alignment, then null depth
    k=int(.05*sr); xc=fftconvolve(mb[:sr*2],ma2[:sr*2][::-1]); lag=xc.argmax()-(min(sr*2,n)-1); lag=int(np.clip(lag,-k,k))
    ra=np.roll(ma2,lag); nul=db(rms(mb-ra))-db(rms(mb))
    Ba,Bb=bands(ma2,sr),bands(mb,sr); bd=10*np.log10((Ba+1e-20)/(Bb+1e-20))
    sa=(a2[:,0]-a2[:,1])/2; sb=(b[:,0]-b[:,1])/2
    wa=10*np.log10((bands(sa,sr)+1e-20)/(Ba+1e-20)); wb=10*np.log10((bands(sb,sr)+1e-20)/(Bb+1e-20))
    wd=wa-wb; live=Bb>Bb.max()*1e-4
    rows.append((c,lvl,lag/sr*1000,nul,np.abs(bd[live]).max() if live.any() else 0,np.round(bd,1),np.abs(np.clip(wd,-40,40)[live]).max() if live.any() else 0))
rows.sort(key=lambda r:-max(r[4],r[6]))
print("bands(Hz):",EDGES)
print(f"{'case':30s} {'level':>6s} {'lag ms':>7s} {'null':>6s} {'tone':>5s} {'width':>6s}  tone diff per band (level-matched, ours-reference)")
for r in rows: print(f"{r[0]:30s} {r[1]:6.1f} {r[2]:7.2f} {r[3]:6.1f} {r[4]:5.1f} {r[6]:6.1f}  {r[5]}")
print("level: median",np.median([r[1] for r in rows]).round(1),"range",min(r[1] for r in rows).round(1),max(r[1] for r in rows).round(1),"pairs",len(rows))
