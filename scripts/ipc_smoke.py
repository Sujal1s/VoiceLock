"""Launch real worker; verify named-pipe framing, errors and disconnect shutdown."""
import json
import subprocess
import sys
import time
import uuid
import win32file
import win32pipe
import pywintypes

def run(executable=None):
    name='VoiceLock-test-'+uuid.uuid4().hex
    command=([executable] if executable else [sys.executable,'-m','engine'])+['serve','--pipe',name]
    process=subprocess.Popen(command,creationflags=subprocess.CREATE_NO_WINDOW)
    handle=None
    try:
        for _ in range(100):
            try:
                handle=win32file.CreateFile('\\\\.\\pipe\\'+name,0xC0000000,0,None,3,0,None);break
            except pywintypes.error:time.sleep(.1)
        assert handle is not None,'Worker pipe never became available'
        def call(op):
            win32file.WriteFile(handle,(json.dumps({'op':op})+'\n').encode())
            response=b''
            while not response.endswith(b'\n'):response+=win32file.ReadFile(handle,65536)[1]
            return json.loads(response)
        hello=call('hello');assert hello['ok'] and hello['result']['protocol']==1
        assert call('devices')['ok']
        assert not call('invalid')['ok']
        assert call('status')['result']['running'] is False
        assert call('stop')['ok']
        print(json.dumps({'pipe_test':'passed','hello':hello['result']},indent=2))
        win32file.CloseHandle(handle);handle=None
        process.wait(timeout=10)
    finally:
        if handle:win32file.CloseHandle(handle)
        if process.poll() is None:process.kill()

if __name__=='__main__':run(sys.argv[1] if len(sys.argv)>1 else None)
