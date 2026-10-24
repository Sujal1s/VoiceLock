# Reproducible acceptance plan

Do not mark a case passed without retaining the input/output recordings (with consent), configuration, hardware, report and observations. Use a separate clean enrollment utterance from the target test utterance. Do not upload private speech.

## Objective fixtures

`python -m scripts.fixtures --target target.wav --background background.wav` creates target-only, background-only, equal-level overlap, quieter/louder background, synthetic burst noise, silence, a 15-second pause/resume and long repeated speech. The first three seconds enroll the target; the rest form the test. SHA-256 source provenance is saved. Gain changes are only distance proxies, and synthetic bursts are not real game recordings. Add actual near/far room recordings and multiple competing speakers for acceptance.

`python -m scripts.evaluate` retains processed WAVs and JSON for every short case; add `--long` for the repeated long recording. p50/p95/p99 and RTF use actual inference timers. Offline deadline misses are not audio-device dropouts; device dropouts and loopback latency remain null in offline reports. Background-only energy reduction is a limited leakage proxy, not word leakage. SI-SDR and SI-SDR improvement use aligned clean targets where present. Inspect output WAVs for intelligibility and missed syllables; numeric metrics alone are insufficient.

## Actual virtual microphone

Install VB-CABLE, enroll your own voice and start VoiceLock. Record its CABLE Output endpoint with `engine record-output` in a second process; it records 48 kHz mono and rejects capture overflow. Retain microphone-side clean/mixed reference recordings separately. For actual latency, record simultaneous physical reference and cable output into two time-aligned DAW tracks, emit a sharp speech onset, and measure the aligned onset/correlation lag. PortAudio and model estimates cannot replace this measurement. Do not interpret offline sample alignment as zero live latency.

## Idle / Valorant / Unity / OBS

1. Baseline: keep the same laptop power source, power plan, game scene/settings, OBS scene and thermal conditions. Do not modify system plans automatically.
2. Run `engine live --seconds 600 --report idle.jsonl` after selecting devices and enrolling. Listen to the actual cable recording. Check zero new underruns/overruns/device errors after startup and no queue growth.
3. Repeat manually during Valorant gameplay and during Unity compilation/Play Mode. Save distinct JSONL reports and clean/overlap voice recordings. The CLI is the optional manual Gaming Benchmark; there is no game injection or memory inspection.
4. Capture frame times with a user-installed supported source such as PresentMon. Retain its version and capture settings. Run matched disabled/enabled trials at least three times; report average FPS and 1% low FPS from frame times, alongside variance. Without frame-time data, report FPS impact as **not measured**.
5. Run `engine live --seconds 1800 --report stability.jsonl`. Compare RSS and queue depth after warmup, midrun and end. Any crash, audible discontinuity, increasing latency or unexplained RSS trend fails acceptance.
6. Apply an optional user-controlled CPU workload. Confirm overload warning/economy mode, bounded buffers and silence on failure. Never select the raw mic in Discord as an automatic workaround.
7. Disconnect/reconnect the physical mic and virtual endpoint; verify output mutes, error is visible, device refresh works and restart succeeds with the same enrollment. Test worker termination, desktop termination, settings changes, long silence/resume and nearby shouting.

The FPS helper `python -m scripts.frame_times disabled.csv enabled.csv --column MsBetweenPresents` computes the same explicitly documented definition for both trials; provide a CSV filtered to the intended game process and valid rendered frames.
