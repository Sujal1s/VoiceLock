"""Run controlled fixtures, retaining all output WAVs including leakage."""
import argparse
from pathlib import Path
from voxlock import load_audio
from engine.benchmark import run_audio,separation_metrics,hardware
from engine.settings import Settings,atomic_json

def main():
    p=argparse.ArgumentParser();p.add_argument('--fixtures',default='data/fixtures');p.add_argument('--output',default='reports/audio');p.add_argument('--long',action='store_true');a=p.parse_args()
    root=Path(a.fixtures);out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
    reference=load_audio(root/'reference.wav');results=[]
    for path in root.glob('*.wav'):
        if path.name=='reference.wav' or '.target.' in path.name or (path.stem=='long_speech' and not a.long):continue
        x=load_audio(path)
        y,stats=run_audio(Settings(),reference,x,out/path.name)
        target=load_audio(path.with_name(path.stem+'.target.wav'))
        stats.update(separation_metrics(x,y,target if target.any() else None,path.stem=='background'))
        stats['case']=path.stem;results.append(stats)
        atomic_json(out/path.with_suffix('.json').name,stats)
    atomic_json(out/'summary.json',dict(hardware=hardware(),results=results))

if __name__=='__main__':main()
