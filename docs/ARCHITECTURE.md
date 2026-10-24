# Architecture and engineering limits

`VoiceLock.Desktop` owns controls, tray and low-frequency metrics display. `engine` owns all PCM, enrollment, device streams and inference. The WPF client launches a single worker on a unique named pipe. Version 1 uses UTF-8 newline JSON request/response envelopes. Requests are limited to 64 KiB, no PCM is sent, remote clients are rejected, and the pipe DACL permits only the launching user. A broken connection stops the worker and audio. Python and C++ workers can implement the same protocol.

Requests: `hello`, `devices`, `settings` (complete Settings object), `setup`, `enroll` (explicit `consent`), `forget`, `calibrate`, `profile` (name), `start`, `status`, `stop`. Responses are `{ok:true,result:...}` or `{ok:false,error:...}`. Only one outstanding request is permitted. Model download and calibration stop live audio first. The current UI waits for these tasks; there is no cancel/progress-per-model feature yet.

## Audio

Physical WASAPI microphone → preallocated bounded capture ring → inference worker → SoXR stateful conversion to 16 kHz mono → 40/80 ms accumulator → persistent VoxLock stream → SoXR stateful conversion to playback rate → gain/peak limiter → bounded output ring → WASAPI playback callback. The model's variable output length is preserved. No raw input is ever written to the output ring.

Capture/render callbacks use nonblocking ring-lock attempts, preallocated storage and no disk/model/network operations. Callback notification and Python's GIL still impose scheduling overhead: this is not a hard-real-time native implementation. VoxLock's own streaming implementation allocates intermediate arrays; VoiceLock does not claim zero-allocation inference. Capture is opened as mono using WASAPI channel negotiation; unsupported device formats fail explicitly rather than guessing indices.

Each ring is capped at 160 ms. Overflow mutes and requires a deliberate restart instead of playing old queued speech or resetting recurrent state invisibly. Initial silence before inference output is tracked separately from underruns. Callback stalls exceeding two seconds trigger failure. There is no automatic hot-plug restart: reconnect, refresh, start. Queue bounds prevent unbounded delay, but a queue at its limit already exceeds the ideal delay target.

## Hardware-aware execution

Default: int8-full, 2 intra-op threads, 1 inter-op, ORT sequential, CPUExecutionProvider only, thread spinning disabled. No CUDA, DirectML, GPU sessions, realtime priority or core affinity. One selected model is loaded; calibration releases each session before the next configuration.

Calibration measures both quantizations × 1–4 threads × 40/80 ms. Qualification requires p95 and aggregate RTF <0.8 with zero per-chunk deadline misses over that workload. Reports include cold-start chunks. Passing this short synthetic benchmark is provisional; gaming may invalidate it. Profiles:

- Maximum Isolation: prefer int8-full among passing configurations, then lowest p95 RTF. This encodes the requested preference, not a universal quality proof.
- Gaming Optimized: prefer int8-full, then lowest measured normalized CPU usage; reduce UI polling to two seconds and suppress diagnostic writes.
- Low Latency: only available for a passing 40 ms configuration; choose lowest estimated model delay.

The adaptive controller uses three overload observations, ten healthy observations and a 30-second transition cooldown. It reduces monitoring to three seconds and suppresses disk diagnostics. It deliberately does not switch model/chunk state during a stream. A buffer overflow mutes; the user can apply another calibrated profile and restart. Automatic benchmark-aware model migration is not implemented.

CPU% is process utilization normalized over 12 logical processors, including worker threads. RAM is RSS; available memory is system available RAM. Inference percentiles use the last 1,000 chunks. Queue depth is a cross-thread approximate snapshot. Latency is an estimate: chunk accumulation + upstream 24 ms algorithmic delay + p95 inference + queue residency + SoXR delay + PortAudio-reported device latencies. It is not acoustic mouth-to-Discord latency and can conservatively double-count pipeline overlap.

No inference/model processing handles are recreated per frame. The adapter uses upstream `VoxLock.stream(reference)` and `Stream.push/flush`; it creates ORT sessions itself because upstream does not expose all required scheduling/provider settings. Upstream `live()` is never used because its enrollment phase passes audio through unchanged.

Upstream source inspected: arbazsiddiqui/voxlock commit `08ed37cbb79f449ca6b499bc23fb14138b207bfd`, package 1.1.1. Model revision is pinned to `a50f4e1d109301d4b6f61dec57ebb1a61e7d8f03`. Downloaded bytes must match official LFS SHA-256 values; every session load rechecks local hashes. Inference never downloads missing files implicitly.
