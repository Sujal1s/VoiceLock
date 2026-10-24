import argparse
import json
import time
from pathlib import Path
from .settings import Settings, ROOT, atomic_json

def main():
    p=argparse.ArgumentParser(description='VoiceLock CPU audio worker')
    sub=p.add_subparsers(dest='cmd',required=True)
    sub.add_parser('setup'); sub.add_parser('devices'); sub.add_parser('hardware')
    server=sub.add_parser('serve'); server.add_argument('--pipe',required=True)
    b=sub.add_parser('calibrate'); b.add_argument('--reference',required=True); b.add_argument('--seconds',type=int,default=12)
    f=sub.add_parser('isolate'); f.add_argument('--reference',required=True); f.add_argument('--input',required=True); f.add_argument('--output',required=True)
    f.add_argument('--model',choices=['int8','int8-full'],default='int8-full'); f.add_argument('--threads',type=int,default=2); f.add_argument('--chunk-ms',type=int,choices=[40,80],default=80)
    f.add_argument('--target'); f.add_argument('--background-only',action='store_true')
    live=sub.add_parser('live'); live.add_argument('--seconds',type=int,default=600); live.add_argument('--report',default='live-report.jsonl')
    rec=sub.add_parser('record-output'); rec.add_argument('--device',required=True); rec.add_argument('--seconds',type=int,default=30); rec.add_argument('--output',required=True)
    args=p.parse_args()
    if args.cmd=='serve':
        from .ipc import serve
        serve(args.pipe); return
    if args.cmd=='setup':
        from .model import setup_models
        result=setup_models()
    elif args.cmd=='devices':
        from .audio import devices
        result=devices()
    elif args.cmd=='hardware':
        from .benchmark import hardware
        result=hardware()
    elif args.cmd=='calibrate':
        from .benchmark import calibrate
        result=calibrate(args.reference,args.seconds)
    elif args.cmd=='isolate':
        from voxlock import load_audio
        from .benchmark import run_audio,separation_metrics
        x=load_audio(args.input)
        y,result=run_audio(Settings(model=args.model,threads=args.threads,chunk_ms=args.chunk_ms),load_audio(args.reference),x,args.output)
        result.update(separation_metrics(x,y,load_audio(args.target) if args.target else None,args.background_only))
        atomic_json(Path(args.output).with_suffix('.json'),result)
    elif args.cmd=='record-output':
        import sounddevice as sd, soundfile as sf
        from .audio import resolve
        device=resolve(args.device,'input')
        # Explicit recording command; never started by diagnostics.
        with sd.InputStream(device=device['id'],samplerate=48000,channels=1,dtype='float32') as stream, sf.SoundFile(args.output,'w',48000,1,subtype='FLOAT') as f:
            for _ in range(args.seconds*10):
                x,overflow=stream.read(4800)
                if overflow: raise RuntimeError('Capture overflow; recording invalid')
                f.write(x)
        result={'output':args.output}
    else:
        from .audio import Engine
        engine=Engine(Settings.load())
        try:
            engine.start()
            with open(args.report,'w') as log:
                for _ in range(args.seconds):
                    time.sleep(1)
                    row=engine.snapshot(); row['time']=time.time()
                    log.write(json.dumps(row)+'\n'); log.flush()
                    if not row['running']: raise RuntimeError(row['error'])
            result={'report':args.report}
        finally: engine.stop()
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
