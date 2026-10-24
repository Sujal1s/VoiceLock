"""Construct controlled fixtures from two consented/public clean speakers.
Run: python -m scripts.fixtures --target target.wav --background background.wav
Enrollment uses the first 3 seconds; target test speech excludes that segment.
Distance cases are level proxies, not measured room acoustics.
"""
import argparse
from pathlib import Path
import numpy as np
import soundfile as sf
from voxlock import load_audio
from engine.settings import atomic_json
from engine.model import digest

def create(target_path, background_path, output):
    root=Path(output); root.mkdir(parents=True,exist_ok=True)
    target,background=load_audio(target_path),load_audio(background_path)
    if len(target)<80000 or len(background)<32000: raise ValueError('Need >=5 s target and >=2 s background speech')
    target=target/(np.max(np.abs(target))+1e-9)*.45
    background=background/(np.max(np.abs(background))+1e-9)*.45
    reference,target=target[:48000],target[48000:]
    n=min(len(target),len(background)); target=target[:n]; background=background[:n]
    zero=np.zeros(n,np.float32)
    rng=np.random.default_rng(5600)
    noise=rng.normal(0,.025,n).astype(np.float32)
    # Explicitly synthetic broadband bursts; not a recording of a game.
    noise*=((np.arange(n)//1600)%8==0)
    cases={'target':(target,target), 'background':(background,zero),
           'overlap':(target+background,target), 'distant_background':(target+.25*background,target),
           'loud_background':(.5*target+1.5*background,.5*target),
           'synthetic_game_noise':(target+noise,target), 'silence':(zero,zero),
           'pause_resume':(np.concatenate((target,np.zeros(16000*15),target)),np.concatenate((target,np.zeros(16000*15),target))),
           'long_speech':(np.tile(target+background,60),np.tile(target,60))}
    sf.write(root/'reference.wav',reference,16000,subtype='FLOAT')
    for name,(mix,clean) in cases.items():
        sf.write(root/(name+'.wav'),mix,16000,subtype='FLOAT')
        sf.write(root/(name+'.target.wav'),clean,16000,subtype='FLOAT')
    atomic_json(root/'provenance.json',dict(target=str(target_path),background=str(background_path),
        target_sha256=digest(target_path),background_sha256=digest(background_path),
        notes='Held-out target segment; distances are gain proxies; game noise is synthetic; no real room/gaming acceptance implied.',cases=list(cases)))

if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--target',required=True); p.add_argument('--background',required=True);p.add_argument('--output',default='data/fixtures')
    a=p.parse_args(); create(a.target,a.background,a.output)
