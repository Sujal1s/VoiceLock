"""Measured streaming throughput and reference-aware evaluation, no invented scores."""
from dataclasses import asdict, replace
import gc
import platform
import time
import numpy as np
import psutil
import soundfile as sf
from voxlock import load_audio
from .model import load_model, validate_enrollment
from .settings import ROOT, Settings, atomic_json

def hardware():
    cpu = platform.processor()
    gpu = []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as k:
            cpu = winreg.QueryValueEx(k, 'ProcessorNameString')[0].strip()
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SYSTEM\CurrentControlSet\Control\Video') as root:
            for i in range(winreg.QueryInfoKey(root)[0]):
                try:
                    with winreg.OpenKey(root, winreg.EnumKey(root,i)+r'\0000') as k:
                        name = winreg.QueryValueEx(k, 'DriverDesc')[0]
                        if name not in gpu: gpu.append(name)
                except OSError: pass
    except OSError: pass
    return dict(cpu=cpu, logical_processors=psutil.cpu_count(), physical_cores=psutil.cpu_count(logical=False),
                ram_gb=psutil.virtual_memory().total/2**30, available_ram_gb=psutil.virtual_memory().available/2**30,
                gpu=gpu, python=platform.python_version(), os=platform.platform())

def run_audio(settings, reference, audio, output=None):
    model = load_model(settings)
    stream = model.stream(validate_enrollment(reference))
    times, chunks = [], []
    count = settings.chunk_ms*16
    process = psutil.Process()
    cpu0 = process.cpu_times()
    rss0 = process.memory_info().rss
    start_all = time.perf_counter()
    peak_rss = rss0
    for i in range(0,len(audio),count):
        start = time.perf_counter()
        y = stream.push(audio[i:i+count])
        times.append((time.perf_counter()-start)*1000)
        chunks.append(y)
        if len(times)%100 == 0: peak_rss = max(peak_rss, process.memory_info().rss)
    chunks.append(stream.flush())
    elapsed = time.perf_counter()-start_all
    y = np.concatenate(chunks)[:len(audio)]
    if len(y) != len(audio) or not np.isfinite(y).all(): raise RuntimeError('Invalid streaming output')
    if output:
        from pathlib import Path
        Path(output).parent.mkdir(parents=True, exist_ok=True)
        sf.write(output, y, 16000, subtype='FLOAT')
    cpu1 = process.cpu_times()
    stats = dict(settings=asdict(settings), seconds=len(audio)/16000, wall_seconds=elapsed,
                 rtf=elapsed/(len(audio)/16000), p50_ms=float(np.percentile(times,50)),
                 p95_ms=float(np.percentile(times,95)), p99_ms=float(np.percentile(times,99)),
                 inference_deadline_misses=sum(t > settings.chunk_ms for t in times),
                 rtf_p95=float(np.percentile(times,95)/settings.chunk_ms),
                 estimated_model_latency_ms=settings.chunk_ms+24+float(np.percentile(times,95)),
                 actual_loopback_latency_ms=None, audio_dropouts=None,
                 output_clipped_samples=int(np.sum(np.abs(y)>=1)),
                 cpu_percent=(cpu1.user+cpu1.system-cpu0.user-cpu0.system)/elapsed/psutil.cpu_count()*100,
                 peak_ram_mb=peak_rss/2**20, ram_growth_mb=(process.memory_info().rss-rss0)/2**20)
    del stream, model
    gc.collect()
    return y, stats

def separation_metrics(mix, y, target=None, background_only=False):
    result = {}
    if background_only:
        result['background_energy_reduction_db'] = float(10*np.log10((np.mean(mix**2)+1e-12)/(np.mean(y**2)+1e-12)))
    if target is not None:
        n = min(len(target),len(y)); target=target[:n]; y=y[:n]
        def si_sdr(ref, estimate):
            ref=ref-ref.mean(); estimate=estimate-estimate.mean()
            projection = np.dot(estimate,ref)/(np.dot(ref,ref)+1e-12)*ref
            return float(10*np.log10((np.sum(projection**2)+1e-12)/(np.sum((estimate-projection)**2)+1e-12)))
        result['si_sdr_db'] = si_sdr(target,y)
        # A clean input has infinite theoretical SI-SDR; epsilon-derived deltas
        # would be misleading. Preserve its output SI-SDR, but no improvement.
        result['si_sdr_improvement_db'] = None if np.allclose(target,mix[:n],atol=1e-8) else result['si_sdr_db']-si_sdr(target,mix[:n])
        result['target_projection_gain'] = float(np.dot(y,target)/(np.dot(target,target)+1e-12))
    return result

def calibrate(reference_path, seconds=12):
    reference = validate_enrollment(load_audio(reference_path))
    audio = np.tile(reference, int(np.ceil(seconds*16000/len(reference))))[:int(seconds*16000)]
    results = []
    for model in ('int8-full','int8'):
        for chunk in (80,40):
            for threads in (1,2,3,4):
                settings=Settings(model=model,threads=threads,chunk_ms=chunk)
                _, result=run_audio(settings,reference,audio)
                # Synthetic calibration qualifies throughput, not isolation quality or gaming.
                result['qualified']=result['rtf_p95']<.8 and result['rtf']<.8 and result['inference_deadline_misses']==0
                results.append(result)
                atomic_json(ROOT/'calibration-progress.json',dict(completed=len(results),total=16,last=result))
    qualified=[r for r in results if r['qualified']]
    profiles={}
    if qualified:
        isolation=min(qualified,key=lambda r:(r['settings']['model']!='int8-full',r['rtf_p95']))
        gaming=min(qualified,key=lambda r:(r['settings']['model']!='int8-full',r['cpu_percent']))
        low=[r for r in qualified if r['settings']['chunk_ms']==40]
        profiles={'Maximum Isolation':isolation['settings'], 'Gaming Optimized':gaming['settings']}
        if low: profiles['Low Latency']=min(low,key=lambda r:r['estimated_model_latency_ms'])['settings']
    report=dict(hardware=hardware(), workload='Synthetic: repeated clean reference; not game FPS or quality validation',
                seconds_per_configuration=seconds, results=results, profiles=profiles,
                warning='No configuration passed. Try again without competing workload.' if not qualified else '')
    atomic_json(ROOT/'calibration.json',report)
    return report
