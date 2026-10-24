import threading
import time
import json
from collections import deque
import numpy as np
import psutil
import sounddevice as sd
import soundfile as sf
import soxr
from .buffers import Ring, Accumulator, Adaptive
from .model import load_model, validate_enrollment
from .settings import ROOT

def devices():
    hosts = sd.query_hostapis()
    result = []
    for index, d in enumerate(sd.query_devices()):
        if 'WASAPI' not in hosts[d['hostapi']]['name']: continue
        result.append(dict(id=index, name=d['name'], input=d['max_input_channels'], output=d['max_output_channels'], rate=int(d['default_samplerate'])))
    return result

def resolve(name, kind):
    found = [d for d in devices() if d['name'] == name and d[kind] > 0]
    if len(found) != 1: raise ValueError(f'Select an unambiguous connected WASAPI {kind} device: {name}')
    return found[0]

def enroll(settings, consent):
    if not consent: raise ValueError('Local voice reference storage requires consent')
    d = resolve(settings.input_device, 'input')
    audio = sd.rec(int(3*settings.input_rate), samplerate=settings.input_rate, channels=1, dtype='float32', device=d['id'], blocking=True)
    x = soxr.resample(audio[:,0], settings.input_rate, 16000).astype(np.float32)
    validate_enrollment(x)
    ROOT.mkdir(parents=True, exist_ok=True)
    temp = ROOT / 'enrollment.tmp.wav'
    sf.write(temp, x, 16000, subtype='FLOAT')
    temp.replace(ROOT / 'enrollment.wav')
    return {'seconds': len(x)/16000, 'peak': float(np.max(np.abs(x)))}

class Engine:
    def __init__(self, settings):
        self.s = settings.validate()
        self.stop_event = threading.Event()
        self.wake = threading.Event()
        self.error = ''
        self.running = False
        self.overruns = self.underruns = self.status_errors = 0
        self.times = deque(maxlen=1000)
        self.input_level = self.output_level = 0.
        self.last_capture = self.last_output = time.monotonic()
        self.process = psutil.Process()
        self.process.cpu_percent()
        self.adaptive = Adaptive()
        self.streams = []
        self.thread = None
        self.output_ready = False
        self.startup_silence_blocks = 0
        self.ever_rendered = False
        self.calibrated_candidates = []
        try:
            report=json.loads((ROOT/'calibration.json').read_text())
            self.calibrated_candidates=[r for r in report['results'] if r['qualified']]
        except (OSError,ValueError,KeyError): pass

    def start(self):
        inp, out = resolve(self.s.input_device, 'input'), resolve(self.s.output_device, 'output')
        self.capture = Ring(int(self.s.input_rate*.16))
        self.playback = Ring(int(self.s.output_rate*.16))
        self.model = load_model(self.s)
        reference, rate = sf.read(ROOT / 'enrollment.wav', dtype='float32')
        if rate != 16000: raise ValueError('Enrollment rate must be 16 kHz')
        self.reference = validate_enrollment(reference)
        self.model_stream = self.model.stream(self.reference)
        self.to_model = soxr.ResampleStream(self.s.input_rate, 16000, 1, dtype='float32', quality='LQ')
        self.to_output = soxr.ResampleStream(16000, self.s.output_rate, 1, dtype='float32', quality='LQ')
        self.acc = Accumulator(self.s.chunk_ms*16)
        self.out_channels = min(2, out['output'])
        sd.check_input_settings(device=inp['id'], channels=1, samplerate=self.s.input_rate, dtype='float32')
        sd.check_output_settings(device=out['id'], channels=self.out_channels, samplerate=self.s.output_rate, dtype='float32')
        try:
            self.streams.append(sd.InputStream(device=inp['id'], channels=1, samplerate=self.s.input_rate, blocksize=0, dtype='float32', latency='low', callback=self._capture))
            self.streams.append(sd.OutputStream(device=out['id'], channels=self.out_channels, samplerate=self.s.output_rate, blocksize=0, dtype='float32', latency='low', callback=self._render))
            self.device_latency = sum(s.latency for s in self.streams)*1000
            self.running = True
            self.thread = threading.Thread(target=self._infer, name='VoiceLock inference', daemon=True)
            self.thread.start()
            for s in self.streams: s.start()
        except Exception:
            self.stop()
            raise

    def fail(self, message):
        self.error = message
        self.running = False  # output callback immediately produces silence
        self.stop_event.set()
        self.wake.set()

    def _capture(self, data, frames, timing, status):
        self.last_capture = time.monotonic()
        if status: self.status_errors += 1
        if self.running:
            if not self.capture.put(data):
                self.overruns += 1
                self.fail('Capture overrun: output muted. Stop and restart with a calibrated profile.')
            self.wake.set()

    def _render(self, out, frames, timing, status):
        out.fill(0)
        self.last_output = time.monotonic()
        if status: self.status_errors += 1
        if not self.running: return
        if not self.output_ready:
            # A small jitter cushion; never wait on the callback thread.
            if self.playback.count < int(self.s.output_rate*.02):
                if not self.ever_rendered: self.startup_silence_blocks += 1
                return
            self.output_ready = True
        n = self.playback.take_into(out[:, :1])
        self.ever_rendered = True
        if self.out_channels > 1: out[:,1] = out[:,0]
        if n < frames:
            self.underruns += 1
            self.output_ready = False
        if not self.running: out.fill(0)

    def _infer(self):
        scratch = np.empty((int(self.s.input_rate*.02), 1), np.float32)
        try:
            while not self.stop_event.is_set():
                self.wake.wait(.05)
                self.wake.clear()
                if time.monotonic()-min(self.last_capture,self.last_output) > 2:
                    raise RuntimeError('Audio device stopped responding. Reconnect, refresh devices and restart.')
                while self.capture.count and not self.stop_event.is_set():
                    n = self.capture.take_into(scratch)
                    if not n: break
                    x = scratch[:n,0]
                    self.input_level = float(np.sqrt(np.mean(x*x)))
                    for chunk in self.acc.push(self.to_model.resample_chunk(x)):
                        start = time.perf_counter()
                        y = self.model_stream.push(chunk)
                        self.times.append((time.perf_counter()-start)*1000)
                        if not np.isfinite(y).all(): raise RuntimeError('Model returned non-finite audio')
                        y = self.to_output.resample_chunk(y)
                        np.multiply(y, self.s.gain, out=y)
                        np.clip(y, -.98, .98, out=y)
                        if len(y): self.output_level = float(np.sqrt(np.mean(y*y)))
                        if not self.playback.put(y[:,None]):
                            self.overruns += 1
                            raise RuntimeError('Output queue exceeded 160 ms; muted to prevent stale audio')
        except Exception as e: self.fail(str(e))

    def snapshot(self):
        t = np.array(self.times)
        p95 = float(np.percentile(t,95)) if len(t) else 0
        queue_ms = self.capture.count/self.s.input_rate*1000+self.playback.count/self.s.output_rate*1000
        economy = self.adaptive.update(p95/self.s.chunk_ms, queue_ms, time.monotonic())
        suggested = None
        if economy and self.calibrated_candidates:
            options=[r for r in self.calibrated_candidates if
                (r['settings']['model'],r['settings']['threads'],r['settings']['chunk_ms']) !=
                (self.s.model,self.s.threads,self.s.chunk_ms)]
            if options:
                best=min(options,key=lambda r:(r['settings']['model']!=self.s.model,r['rtf_p95']))
                suggested={k:best['settings'][k] for k in ('model','threads','chunk_ms')}
        return dict(running=self.running, error=self.error, model=self.s.model, threads=self.s.threads,
                    input_rate=self.s.input_rate, output_rate=self.s.output_rate, chunk_ms=self.s.chunk_ms,
                    input_level=self.input_level, output_level=self.output_level,
                    inference_mean_ms=float(t.mean()) if len(t) else 0, inference_p95_ms=p95,
                    rtf_p95=p95/self.s.chunk_ms, queue_ms=queue_ms,
                    estimated_latency_ms=self.s.chunk_ms+24+queue_ms+p95+self.device_latency+
                        self.to_model.delay()/16000*1000+self.to_output.delay()/self.s.output_rate*1000,
                    cpu_percent=self.process.cpu_percent()/psutil.cpu_count(), ram_mb=self.process.memory_info().rss/2**20,
                    available_ram_mb=psutil.virtual_memory().available/2**20,
                    overruns=self.overruns, underruns=self.underruns, device_errors=self.status_errors,
                    startup_silence_blocks=self.startup_silence_blocks,
                    suggested_configuration=suggested,
                    economy=economy, warning=('Overload: reduced monitoring. Stop and apply benchmarked '+str(suggested) if suggested else 'Overload: reduced monitoring. Recalibrate; no safe alternate is available.') if economy else '')

    def stop(self):
        self.running = False
        self.stop_event.set()
        self.wake.set()
        for s in self.streams:
            try: s.abort(); s.close()
            except Exception: pass
        self.streams.clear()
        if self.thread: self.thread.join()  # never dispose a session still executing
        self.model_stream = self.model = None
