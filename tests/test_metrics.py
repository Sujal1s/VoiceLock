import numpy as np
from engine.benchmark import separation_metrics

def test_clean_reference_improvement_is_undefined():
    x=np.sin(np.arange(1000)).astype(np.float32)
    result=separation_metrics(x,x*.9,x)
    assert result['si_sdr_improvement_db'] is None
    assert abs(result['target_projection_gain']-.9)<1e-5

def test_background_energy_metric():
    x=np.ones(1000,np.float32)
    result=separation_metrics(x,x*.1,background_only=True)
    assert abs(result['background_energy_reduction_db']-20)<.001
