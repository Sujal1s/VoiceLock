"""Repeat the two candidate 80-ms configurations with a longer workload."""
import numpy as np
from voxlock import load_audio
from engine.settings import Settings,atomic_json
from engine.benchmark import run_audio,hardware
reference=load_audio('data/fixtures/reference.wav')
source=load_audio('data/fixtures/overlap.wav')
audio=np.tile(source,int(np.ceil(60*16000/len(source))))[:60*16000]
results=[]
for threads in (2,3):
    _,stats=run_audio(Settings(threads=threads),reference,audio)
    results.append(stats)
atomic_json('reports/recheck-60s.json',dict(hardware=hardware(),workload='60 seconds repeated overlap, each configuration, no concurrent VoiceLock build',results=results))
