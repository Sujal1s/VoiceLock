import threading
import time
import numpy as np
from engine.audio import Engine
from engine.buffers import Ring,Accumulator
from engine.settings import Settings

def test_failure_never_passes_raw_audio():
    e=Engine(Settings());e.capture=Ring(10);e.playback=Ring(10);e.out_channels=2
    e.playback.put(np.ones((10,1),np.float32))
    e.running=True;e.fail('model failed')
    out=np.ones((10,2),np.float32)
    e._render(out,10,None,False)
    assert not out.any()
    assert e.error=='model failed'

def test_capture_overflow_mutes():
    e=Engine(Settings());e.capture=Ring(1);e.running=True
    e._capture(np.ones((2,1),np.float32),2,None,False)
    assert not e.running and e.overruns==1

def test_variable_output_and_inference_exception():
    class Resampler:
        def resample_chunk(self,x):return x
    class Stream:
        calls=0
        def push(self,x):
            self.calls+=1
            if self.calls==2:raise RuntimeError('injected failure')
            return np.full(896,.2,np.float32)
    e=Engine(Settings(input_rate=16000,output_rate=16000))
    e.capture=Ring(4000);e.playback=Ring(4000);e.acc=Accumulator(1280)
    e.to_model=e.to_output=Resampler();e.model_stream=Stream();e.running=True
    e.capture.put(np.ones((2560,1),np.float32));e.wake.set()
    t=threading.Thread(target=e._infer);t.start();t.join(3)
    assert not t.is_alive() and e.error=='injected failure'
    assert e.playback.count==896
    out=np.ones((500,2),np.float32);e.out_channels=2;e._render(out,500,None,False)
    assert not out.any()
