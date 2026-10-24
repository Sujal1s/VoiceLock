# Native worker validation — 2026-10-23

## Scope of the fix

The audio/inference worker is now C++20, using WASAPI, lock-free SPSC queues, libsoxr and ONNX Runtime CPU. The desktop remains C# WPF. No Python interpreter ships in the native worker folder. The native release is separate from the old Python build: `dist/VoiceLock-Native/VoiceLock.Desktop.exe`.

Root cause confirmed by code inspection: the Python ring returned `False` on both mutex contention and actual capacity exhaustion. Its capture callback treated either as fatal and latched mute permanently. Native queues have no such mutex. Transient capture stalls now discard stale data and reset processing with a render-thread handshake; playback-only backlog is trimmed without resetting the speaker model. Actual model/device failures still fail closed. The later automatic-recovery update removes the repeated-overload shutdown threshold; see AUTO-RECOVERY.md. The historical runs below describe the earlier threshold behavior.

Audio threads use the supported Windows MMCSS Audio task; the model uses ordinary CPU scheduling, sequential ORT, one inter-op thread, no spinning and no GPU. Your existing voice reference, models and device selections were preserved. The selected thread count was changed from two to four, with the old settings backed up as `%LOCALAPPDATA%\VoiceLock\settings.before-native.json`.

## Executed checks

- C++ Release compiled with MSVC 19.38; WPF Release published self-contained for Windows x64. Native dependency DLLs and app-local MSVC runtime copied alongside the worker.
- Native core self-test passed: wraparound, bounded queue rejection, two-thread transfer of 200,000 ordered samples, consumer discard/trim, enrollment silence rejection, settings validation and streaming resampler continuity.
- Resampling a one-second 48 kHz tone in irregular 337-sample blocks matched whole-file resampling with **maximum error 0.0** and exactly 16,000 output samples.
- Real-model reset test passed: reset and replay of the 189,440-sample fixture gave **maximum difference 0.0**, without rebuilding the ONNX sessions or changing enrollment.
- Native 3-thread overlap output matched the retained Python 3-thread output **sample-for-sample** (maximum and RMS difference 0.0). This verifies the port's recurrent state, reflection padding and overlap-add implementation against the previous implementation.
- Native named-pipe test passed for hello, actual device enumeration, error response, status, stop and shutdown on disconnection.
- Native desktop launch and screenshot checked; it recognizes existing enrollment and the IMMORTAL microphone / CABLE Input route.

## Offline reference run

`reports/native-overlap.json`: int8-full, 80 ms, 3 threads, 11.84-second controlled overlap. Aggregate RTF **0.587**, p95 inference **52.02 ms**, p95 RTF **0.650**, zero inference deadline misses, zero clipped samples, sampled worker RSS **146.6 MiB**. This is not a concurrent gaming or live-device result.

## Actual microphone / VB-CABLE tests

These tests used the user's existing local enrollment and the actual selected WASAPI microphone and CABLE Input. They saved **only performance counters**, not microphone or cable audio. No game or application memory was inspected. The desktop workload was not controlled; these are not controlled FPS comparisons or subjective listening tests.

1. Initial 3-thread native prototype, before final scheduling/playback recovery corrections: recovered from the requested reset but later muted after repeated overload at 38 seconds. Preserved in `reports/native-live-60s.jsonl`; this failure is not counted as a passing final test.
2. Updated 4-thread engine, 40 ms playback cushion, 90 seconds: forced a real **400 ms inference stall** at second 11. It produced 16 capture overruns, automatically recovered once and finished running, unmuted, with no error. There were 22 playback underrun events and 3 playback trims during the run. Final p95 RTF was 0.812; average normalized CPU usage about 12.1%. Evidence: `reports/native-live-90s-4threads.jsonl`.
3. Same 4-thread engine, 40 ms cushion, clean 120-second run with no injected fault: finished running, unmuted, with **zero capture overruns**, 28 playback underrun events and one recovery. Final p95 RTF was 0.816, average normalized CPU about 12.1%. Evidence: `reports/native-clean-120s.jsonl`. This exposed remaining playback jitter and led to the configurable cushion, defaulting to 80 ms.
4. Intermediate 80 ms cushion run, before matching the playback trim threshold to the cushion: 60 seconds with a deliberate 400 ms inference stall. Finished unmuted with one recovery, 17 capture overruns, 22 playback underruns and 15 trims. Retained in `reports/native-final-80ms-cushion.jsonl`.
5. Final buffering configuration, 80 ms cushion and 200 ms playback trim threshold: completed 60 seconds without an injected fault, running and unmuted, with no error, **zero capture overruns and zero device errors**. There were **13 playback underruns, six automatic recoveries and four playback trims**. Final p95 inference was 87.23 ms (RTF 1.090), worker RSS 148.2 MiB. Estimated latency at the final sample was 366 ms; it was not acoustically measured. Evidence: `reports/native-release-60s.jsonl` and `reports/native-release-60s-result.json`. This is a recovery/stability result, not a clean-audio pass: processing occasionally exceeded its time budget under this desktop workload.

These tests demonstrate that the former capture-overrun latch has been replaced with automatic recovery. They do **not** establish glitch-free audio, under-150 ms mouth-to-app latency, low gaming FPS impact or 30-minute stability. Playback underruns can be audible. The larger cushion trades delay for resilience; latency remains an estimate, not an acoustic measurement. If overload persists, recalibrate during the intended workload, try the alternative model, or reduce competing CPU load. Hardware disconnection requires reconnecting and restarting.

Source, scripts, test WAVs and reports remain in the workspace. The release does not contain the user's enrollment or microphone recordings. A native worker is not a claim that the neural network always fits the laptop's available compute budget.
