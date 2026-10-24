import os
import numpy as np
import pytest
from voxlock import load_audio
from engine.model import load_model
from engine.settings import Settings,ROOT

@pytest.mark.skipif(not (ROOT/'models/manifest.json').exists(),reason='Official models not downloaded')
def test_real_stream_persistent_state_and_chunk_equivalence():
    reference=load_audio('data/fixtures/reference.wav')
    audio=load_audio('data/fixtures/overlap.wav')[:16000*3]
    model=load_model(Settings())
    results=[]
    for size in (640,1280):
        stream=model.stream(reference)
        chunks=[stream.push(audio[i:i+size]) for i in range(0,len(audio),size)]
        chunks.append(stream.flush());y=np.concatenate(chunks)[:len(audio)]
        assert len(y)==len(audio) and np.isfinite(y).all()
        assert stream.n_in==len(audio)
        assert np.any(stream.h)
        results.append(y)
    # Quantized dynamic kernels can differ numerically with frame batch size.
    assert np.sqrt(np.mean((results[0]-results[1])**2)) < .03
