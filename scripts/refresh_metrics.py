"""Recompute reference metrics from retained WAV evidence, without inference."""
import json
from pathlib import Path
from voxlock import load_audio
from engine.benchmark import separation_metrics
from engine.settings import atomic_json
summary=json.loads(Path('reports/audio/summary.json').read_text())
for r in summary['results']:
    name=r['case'];x=load_audio('data/fixtures/'+name+'.wav');y=load_audio('reports/audio/'+name+'.wav');target=load_audio('data/fixtures/'+name+'.target.wav')
    r.update(separation_metrics(x,y,target if target.any() else None,name=='background'))
    atomic_json('reports/audio/'+name+'.json',r)
atomic_json('reports/audio/summary.json',summary)
