# VoiceLock for Windows

**Current build: native C++ audio worker.** Use `dist/VoiceLock-Native/VoiceLock.Desktop.exe` and [native setup/overrun-fix documentation](docs/NATIVE.md). Build it with `scripts/build-native.ps1`. This replaces the Python real-time worker, preserves existing enrollment/settings, and adds lock-free queues and automatic transient-overrun recovery. The WPF interface remains C#.

The remainder of this page documents the earlier Python MVP, retained for reference and comparison.

A native .NET 8 WPF desktop utility with a separate Python 3.11 WASAPI audio worker. It uses **real VoxLock enrolled-speaker extraction**, ONNX Runtime CPU sessions and persistent SoXR resamplers. No microphone audio goes through the UI or a cloud service.

This is an engineering MVP, not a certified gaming release. See [validation](docs/VALIDATION.md) for executed checks, measurements and pending acceptance tests. Separation is imperfect. Retained evaluation WAVs include leakage and distortion; do not interpret quantization labels as guaranteed quality rankings.

## Run the release

1. Open `dist/VoiceLock/VoiceLock.Desktop.exe`. Keep its `worker` folder alongside it. This is a self-contained x64 portable release; no separate .NET/Python installation is required.
2. Install **VB-CABLE** yourself from <https://vb-audio.com/Cable/> and reboot if its installer requests it. VoiceLock does not install or redistribute the driver.
3. In **First-run setup**, download/verify models. This is the only operation requiring internet. Both official quantized variants occupy roughly 110 MB together (shared enrollment model).
4. Select your physical microphone. Select **CABLE Input** as the playback endpoint. Avoid selecting speakers: that can cause acoustic feedback.
5. Consent to local enrollment storage, then record three seconds of clean speech in a quiet room. Rerecord if clipped, too quiet or contaminated by another voice. The energy check cannot prove that a recording contains exactly one speaker.
6. Run CPU calibration. It saves passing configurations and applies Gaming Optimized. If none pass, the UI will say so; manual settings remain available without a performance guarantee.
7. Start isolation. In Discord, Valorant or OBS, choose **CABLE Output** as the microphone. Disable duplicate noise suppression experimentally if it harms intelligibility. Do not use CABLE Output as VoiceLock's physical input.

Minimize to tray to keep processing. Closing exits and mutes. Changing settings stops audio; press Start to apply fresh model/resampler state. After a disconnect, reconnect hardware, refresh devices and restart. No automatic raw-microphone fallback exists.

Default storage is `%LOCALAPPDATA%\VoiceLock`. Set `VOICELOCK_DATA` before launch to relocate it. Enrollment is local `enrollment.wav`, deleted using the UI. Models and calibration can remain after deleting enrollment. Diagnostic logs contain metrics, never PCM, and rotate at 1 MB; Gaming Mode and overload disable these logs.

## Build from source

Install Python 3.11 x64 and .NET 8 SDK. From this folder in PowerShell:

```powershell
py -3.11 -m venv .venv311
.\.venv311\Scripts\python.exe -m pip install -r requirements-lock-win-py311.txt
.\scripts\build.ps1 -Dotnet dotnet
```

The checked-out development environment also has a workspace-local SDK at `.tools/dotnet/dotnet.exe`. `scripts/build.ps1` uses it by default. A network connection is needed for initial dependency restore. No GPU inference, system power-plan change, CPU affinity or elevated process priority is used.

## Run engine without the UI

```powershell
$env:VOICELOCK_DATA="$PWD\data"
.\.venv311\Scripts\python.exe -m engine setup
.\.venv311\Scripts\python.exe -m engine devices
.\.venv311\Scripts\python.exe -m engine hardware
.\.venv311\Scripts\python.exe -m engine isolate --reference clean-voice.wav --input mixture.wav --output isolated.wav
.\.venv311\Scripts\python.exe -m engine calibrate --reference clean-voice.wav --seconds 20
# Configure devices and enroll using the UI, then close it before live CLI operation:
.\.venv311\Scripts\python.exe -m engine live --seconds 600 --report idle-desktop.jsonl
.\.venv311\Scripts\python.exe -m engine record-output --device "CABLE Output (VB-Audio Virtual Cable)" --seconds 30 --output virtual-mic.wav
```

Use the exact WASAPI device names reported on your machine, not the illustrative name above. Offline file processing writes float WAVs without the live limiter so output clipping remains measurable. The live path applies gain and a hard peak limiter at 0.98; this limiter is not speaker isolation.

## Tests and evaluation

```powershell
$env:VOICELOCK_DATA="$PWD\data"
.\.venv311\Scripts\python.exe -m scripts.fixtures --target target.wav --background other-speaker.wav
.\.venv311\Scripts\python.exe -m pytest -q --basetemp data/test-temp
.\.venv311\Scripts\python.exe -m scripts.ipc_smoke
.\.venv311\Scripts\python.exe -m scripts.evaluate
```

Real-model integration tests require downloaded models and generated fixtures. Unit tests require neither internet nor devices. See [architecture](docs/ARCHITECTURE.md), [acceptance procedures](docs/ACCEPTANCE.md), [troubleshooting](docs/TROUBLESHOOTING.md), and [dependency notices](THIRD-PARTY-NOTICES.md).
