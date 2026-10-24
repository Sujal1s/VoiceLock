import json
import numpy as np
import pytest
import soxr
from engine.buffers import Ring,Accumulator,Adaptive
from engine.settings import Settings
from engine.model import validate_enrollment

def test_ring_wrap_and_bounded():
    r=Ring(7);x=np.arange(5,dtype=np.float32)[:,None]
    assert r.put(x)
    assert not r.put(x)
    out=np.empty((3,1),np.float32)
    assert r.take_into(out)==3
    np.testing.assert_array_equal(out[:,0],[0,1,2])
    assert r.put(x)
    out=np.empty((7,1),np.float32);assert r.take_into(out)==7
    np.testing.assert_array_equal(out[:,0],[3,4,0,1,2,3,4])

def test_callback_never_waits_for_ring_lock():
    r=Ring(10)
    with r.lock:
        assert not r.put(np.zeros((1,1),np.float32))
        assert r.take_into(np.empty((1,1),np.float32))==0

def test_accumulator_variable_chunks():
    a=Accumulator(640);x=np.arange(3201,dtype=np.float32)
    out=[a.push(part).reshape(-1) for part in np.array_split(x,23)]
    np.testing.assert_array_equal(np.concatenate(out),x[:3200])
    np.testing.assert_array_equal(a.rest,x[3200:])

@pytest.mark.parametrize('rates',[(48000,16000),(16000,48000),(44100,16000)])
def test_stream_resampler_matches_whole(rates):
    a,b=rates;x=np.sin(np.arange(a)*2*np.pi*440/a).astype(np.float32)
    stream=soxr.ResampleStream(a,b,1,dtype='float32',quality='LQ')
    parts=np.array_split(x,47)
    y=np.concatenate([stream.resample_chunk(p,last=i==len(parts)-1) for i,p in enumerate(parts)])
    expected=soxr.resample(x,a,b,quality='LQ')
    assert len(y)==b
    np.testing.assert_allclose(y,expected,atol=1e-5)

def test_settings_atomic_roundtrip_and_validation(tmp_path):
    s=Settings();s.save(tmp_path/'settings.json')
    assert Settings.load(tmp_path/'settings.json')==s
    for kwargs in ({'threads':0},{'chunk_ms':20},{'model':'fake'},{'gain':float('nan')}):
        with pytest.raises(ValueError):Settings(**kwargs).validate()

def test_enrollment_rejects_silence_clipping_short():
    for x in (np.zeros(48000),np.ones(48000),np.zeros(100)):
        with pytest.raises(ValueError):validate_enrollment(x)
    validate_enrollment(.1*np.sin(np.arange(48000)))

def test_adaptive_hysteresis_cooldown():
    a=Adaptive()
    assert not a.update(1,0,0)
    assert not a.update(1,0,1)
    assert a.update(1,0,2)
    for t in range(3,20):assert a.update(.2,0,t)
    assert not a.update(.2,0,33)
