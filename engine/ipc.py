"""Version 1: newline JSON over a current-user-only, local Windows named pipe."""
from dataclasses import asdict
import json
import os
import time
from .settings import Settings, ROOT
from .audio import Engine, devices, enroll
from .model import setup_models
from .benchmark import hardware, calibrate

class Service:
    def __init__(self):
        self.settings = Settings.load()
        self.engine = None
        self.last_log = 0.
    def stop(self):
        if self.engine:
            self.engine.stop()
            self.engine = None
    def command(self, request):
        op = request['op']
        if op == 'hello':
            profiles=[]
            try: profiles=list(json.loads((ROOT/'calibration.json').read_text())['profiles'])
            except (OSError,ValueError,KeyError): pass
            return dict(protocol=1, settings=asdict(self.settings), hardware=hardware(), enrolled=(ROOT/'enrollment.wav').exists(), models=(ROOT/'models/manifest.json').exists(), data_dir=str(ROOT),profiles=profiles)
        if op == 'devices': return devices()
        if op == 'status':
            result = self.engine.snapshot() if self.engine else dict(running=False,error='')
            if self.settings.diagnostics and not self.settings.gaming and not result.get('economy') and time.monotonic()-self.last_log >= 5:
                ROOT.mkdir(parents=True,exist_ok=True)
                path=ROOT/'diagnostics.jsonl'
                if path.exists() and path.stat().st_size>1024*1024: path.replace(ROOT/'diagnostics.previous.jsonl')
                with path.open('a') as f: f.write(json.dumps(dict(time=time.time(),**result))+'\n')
                self.last_log=time.monotonic()
            return result
        if op == 'stop': self.stop(); return {'running':False}
        if op == 'settings':
            settings = Settings(**request['settings']).validate()
            self.stop()
            settings.save()
            self.settings = settings
            return asdict(settings)
        if op == 'setup': self.stop(); return setup_models()
        if op == 'enroll': self.stop(); return enroll(self.settings,request.get('consent',False))
        if op == 'forget':
            self.stop()
            (ROOT/'enrollment.wav').unlink(missing_ok=True)
            return {'enrolled':False}
        if op == 'calibrate':
            self.stop()
            report = calibrate(ROOT/'enrollment.wav',request.get('seconds',12))
            if report['profiles']:
                chosen=report['profiles']['Gaming Optimized']
                for key in ('model','threads','chunk_ms'): setattr(self.settings,key,chosen[key])
                self.settings.save()
            return report
        if op == 'profile':
            report=json.loads((ROOT/'calibration.json').read_text())
            chosen=report['profiles'].get(request['name'])
            if not chosen: raise ValueError('Profile has no passing benchmark. Run calibration first.')
            self.stop()
            for key in ('model','threads','chunk_ms'): setattr(self.settings,key,chosen[key])
            self.settings.gaming=request['name']=='Gaming Optimized'
            self.settings.save()
            return asdict(self.settings)
        if op == 'start':
            self.stop()
            engine=Engine(self.settings)
            try: engine.start()
            except Exception: engine.stop(); raise
            self.engine=engine
            return engine.snapshot()
        raise ValueError(f'Unknown operation: {op}')

def serve(name):
    import win32api, win32con, win32file, win32pipe, win32security, pywintypes
    token=win32security.OpenProcessToken(win32api.GetCurrentProcess(),win32con.TOKEN_QUERY)
    sid=win32security.GetTokenInformation(token,win32security.TokenUser)[0]
    acl=win32security.ACL(); acl.AddAccessAllowedAce(win32security.ACL_REVISION,win32con.GENERIC_ALL,sid)
    descriptor=win32security.SECURITY_DESCRIPTOR(); descriptor.SetSecurityDescriptorDacl(1,acl,0)
    security=pywintypes.SECURITY_ATTRIBUTES(); security.SECURITY_DESCRIPTOR=descriptor
    service=Service()
    handle=win32pipe.CreateNamedPipe('\\\\.\\pipe\\'+name,win32pipe.PIPE_ACCESS_DUPLEX,
        win32pipe.PIPE_TYPE_BYTE|win32pipe.PIPE_READMODE_BYTE|win32pipe.PIPE_WAIT|8,1,65536,65536,0,security)
    try:
        win32pipe.ConnectNamedPipe(handle,None)
        pending=b''
        while True:
            _,data=win32file.ReadFile(handle,8192)
            if not data: break
            pending+=data
            if len(pending)>65536: raise ValueError('IPC request too large')
            while b'\n' in pending:
                line,pending=pending.split(b'\n',1)
                try:
                    request=json.loads(line)
                    result={'ok':True,'result':service.command(request)}
                except Exception as e:
                    result={'ok':False,'error':str(e)}
                win32file.WriteFile(handle,(json.dumps(result,allow_nan=False)+'\n').encode())
    finally:
        service.stop()
        win32file.CloseHandle(handle)
