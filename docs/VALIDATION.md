# Validation record

Date: 2026-10-22, Asia/Calcutta. This is a working implementation and portable engineering build, **not a completed live/gaming acceptance release**.

## Environment verified

AMD Ryzen 5 5600H, 6 physical cores / 12 logical processors; Radeon RX 5500M plus integrated Radeon; 15.38 GiB OS-visible RAM. Python 3.11.17, .NET SDK 8.0.426, ONNX Runtime 1.31.0. Hardware was detected through Windows registry and psutil; no GPU inference was used.

WASAPI enumeration found the IMMORTAL IM-300 physical microphone, AMD microphone array and Steam endpoints. **No VB-CABLE endpoint was installed.** No user microphone recording was made, no voice identity was installed, and no audio was sent to physical speakers during this build. The release has a genuine first-run enrollment flow for the user.

## Executed

- .NET Release compilation: successful, zero warnings/errors.
- Python 3.11 test suite: **15 passed in 12.98 seconds** (`reports/tests.xml`). Coverage: core buffering, resampling, settings, enrollment rejection, adaptive hysteresis, failure mute, injected inference failure, metrics and real-model persistent streaming tests.
- Both official model variants downloaded and checked against official SHA-256 hashes.
- Native current-user named-pipe request/response, rejected unknown operation, status and shutdown tested for source worker and packaged worker. Sandbox restrictions prevented the first pipe attempt; the normal Windows process test passed.
- Desktop launched, connected to its worker, enumerated actual devices, rendered screenshots and exited automatically. Screenshots were inspected and low-contrast controls/incorrect selected-device text were corrected. This smoke test does not exercise every interactive control.
- Eight controlled recorded-audio cases processed through the real model and written to float WAVs. All outputs have expected lengths, finite samples and zero clipping. Audio intelligibility has not been subjectively certified.

## 16-configuration calibration

See `reports/calibration-ryzen5600h.json`. Each configuration processed 20 seconds of repeated public enrollment speech. This is synthetic throughput testing during an active desktop development session; some compilation/packaging overlapped the run. It is **not an idle-only, game or isolation-quality benchmark**.

| Model | Threads | Chunk ms | p95 ms | p95 RTF | Deadline misses |
|---|---:|---:|---:|---:|---:|
| int8-full | 1 | 80 | 73.02 | 0.913 | 2 |
| int8-full | 2 | 80 | 81.49 | 1.019 | 15 |
| int8-full | 3 | 80 | 60.19 | 0.752 | 0 |
| int8-full | 4 | 80 | 57.29 | 0.716 | 1 |
| int8-full | 1 | 40 | 63.52 | 1.588 | 500 |
| int8-full | 2 | 40 | 47.91 | 1.198 | 500 |
| int8-full | 3 | 40 | 31.60 | 0.790 | 3 |
| int8-full | 4 | 40 | 29.75 | 0.744 | 3 |
| int8 | 1 | 80 | 75.47 | 0.943 | 3 |
| int8 | 2 | 80 | 68.14 | 0.852 | 5 |
| int8 | 3 | 80 | 52.12 | 0.651 | 0 |
| int8 | 4 | 80 | 48.18 | 0.602 | 0 |
| int8 | 1 | 40 | 64.40 | 1.610 | 500 |
| int8 | 2 | 40 | 57.81 | 1.445 | 458 |
| int8 | 3 | 40 | 55.24 | 1.381 | 242 |
| int8 | 4 | 40 | 48.59 | 1.215 | 99 |

The short-run selector chose int8-full / 80 ms / 3 threads for both Maximum Isolation and Gaming Optimized. No Low Latency profile qualified. Identical profile configurations are intentional when the same candidate wins both criteria; Gaming Mode still changes monitoring behavior.

## Longer candidate recheck

`reports/recheck-60s.json` processes 60 seconds of repeated overlap per configuration, with no concurrent VoiceLock compilation/packaging. Other desktop workload was not controlled.

| int8-full / 80 ms | 2 threads | 3 threads |
|---|---:|---:|
| Aggregate RTF | 0.831 | 0.663 |
| p50 inference ms | 62.37 | 51.51 |
| p95 inference ms | 91.62 | 60.45 |
| p99 inference ms | 132.96 | 76.77 |
| Inference deadline misses | 88 | 8 |
| Normalized engine CPU % | 13.29 | 17.32 |
| Sampled peak RSS MiB | 178.84 | 187.14 |
| RSS growth during run MiB | 16.13 | 13.64 |
| Estimated model-side delay ms | 195.62 | 164.45 |

The 3-thread candidate is faster but **does not pass the stricter sustained zero-miss criterion**. Short-run profiles remain provisional. The estimated delay excludes actual device/downstream-app delay and exceeds the ideal 150 ms target. RSS growth includes retained benchmark output arrays and warmup; these runs cannot establish a leak or prove leak freedom. Inference deadline misses are not measured audio-device dropouts.

The packaged executable also successfully processed the 11.84-second overlap fixture (`reports/release-inference.json`, `reports/release-overlap.wav`): aggregate RTF 0.738, p95 68.58 ms / RTF 0.857, four deadline misses, zero clipped output samples. This additional run reinforces that timing varies and sustained performance is not yet established.

## Controlled separation evidence

`reports/audio/summary.json` and adjacent WAVs retain every output, including residual background speech. `data/fixtures/provenance.json` identifies the public source audio and transformations. These use a held-out segment of one target speaker and one competing speaker, not the user's voice.

| Fixture | Measured result |
|---|---|
| Background only | 17.82 dB output energy reduction |
| Equal-level overlap | SI-SDR 10.51 dB; improvement 12.02 dB; target projection gain 0.988 |
| Quiet competing speaker | SI-SDR 16.67 dB; improvement 6.08 dB |
| Loud competing speaker | SI-SDR 6.32 dB; improvement 17.54 dB; target gain 0.849 |
| Synthetic burst noise | SI-SDR 23.58 dB; improvement 6.66 dB |
| Target alone | SI-SDR 33.69 dB; target gain 1.077 |
| 15-second pause and resume | SI-SDR 34.27 dB; target gain 1.077 |
| Silence | Finite output; zero clipped samples |

Clean-input SI-SDR improvement is undefined and reported as null, since the baseline has theoretically infinite SI-SDR. Background energy reduction is a proxy, not a transcript-based leakage score. Distance cases only alter levels; burst noise is synthetic. The loud-background case demonstrates a remaining target-preservation tradeoff. No perfect separation claim is made.

## Remaining blockers and limitations

1. Install VB-CABLE and enroll the user's own voice. Verify actual microphone → WASAPI → cable → Discord/Valorant/OBS. This milestone is implemented but **not proven end-to-end** here.
2. Run 10-minute audible glitch checks, actual loopback latency and 30-minute stability. Queue drift, device disconnect recovery and sustained RSS require real-device runs.
3. Execute Valorant and Unity tests, with a supported frame-time source before reporting FPS impact. Gaming performance remains unmeasured.
4. Validate multiple real competing speakers, shouting, room acoustics and subjective intelligibility.
5. Adaptive control reduces monitoring, suggests previously calibrated alternatives and mutes on buffer failure. Automatic benchmark-aware model migration, automatic device reconnection and worker relaunch are not implemented; deliberate restart is required.
6. Python callbacks, the GIL and upstream intermediate allocations remain latency risks. A native worker may be necessary if the laptop cannot meet sustained targets with this implementation.
7. The portable build is unsigned; no installer, driver or signing certificate is included. Review bundled dependency notices before public redistribution.

