# VoiceLock native audio worker

This build replaces the Python audio/inference process with **C++20**, native WASAPI, ONNX Runtime CPU and libsoxr. The desktop UI remains C# WPF. Python is not required at runtime. The previous Python release is preserved separately.

Open `VoiceLock.Desktop.exe` in this folder. Close the old VoiceLock first so two workers do not feed the cable simultaneously. Your existing `%LOCALAPPDATA%\VoiceLock\settings.json`, models and enrollment are reused; no voice recording is uploaded or automatically replaced. Choose the physical microphone and CABLE Input, then Start. Downstream apps use CABLE Output as their microphone.

## Capture-overrun fix

The Microphone tab includes **Install VB-CABLE**. It downloads VB-Audio's official Pack45 ZIP over HTTPS, verifies a pinned SHA-256, extracts all driver files, stops isolation and launches the x64 installer with the Windows administrator prompt. Click **Install Driver** in the vendor window, then restart Windows and refresh devices. Existing CABLE Input/Output endpoints disable installation to avoid an unnecessary reinstall. Cancellation and download errors are shown in the app. The driver is downloaded on demand, not bundled or silently installed; its donationware terms remain VB-Audio's. The **VB-CABLE website** button opens the official site. A changed upstream archive is rejected until VoiceLock's pin is reviewed and updated.

The old Python ring returned the same failure for a busy mutex and a full queue. The callback interpreted either as an unrecoverable overrun. The native worker has separate capture, inference and render threads and single-producer/single-consumer atomic queues, with no mutex or Python GIL in the audio path. Model/session creation and enrollment complete before audio startup; the device watchdog starts with the actual streams.

Transient capture overflow, device discontinuity or excessive capture backlog now briefly mutes playback, discards old input and processed output with a render-thread handshake, resets SoXR and recurrent state, then resumes the same enrolled speaker using the already-loaded model/embedding. Playback-only backlog is trimmed by its consumer without resetting the model. The UI displays recoveries and trims. Capture/render threads use the supported Windows MMCSS Audio task; inference keeps normal process scheduling. It never plays the raw microphone. Repeated capture stalls no longer terminate isolation: the worker continues resetting bounded queues and automatically resumes the enrolled speaker when processing catches up. More than five recoveries in ten seconds displays an overload warning instead of requiring a manual restart. Genuine device or model failures still stop output.

Queue capacity is 240 ms. Capture backlog recovery is triggered at 160 ms; playback has a separate threshold that accommodates the selected cushion plus an inference burst and packet jitter (200 ms with the default settings). This is bounded buffering, not a promise of 150 ms total delay. Recovery and playback trims can create a brief gap and lose some speech. Actual hardware disconnects still need reconnection and restart. Independent device clocks can still cause occasional recovery over long runs.

Native calibration results use `calibration-native.json`, separate from Python measurements. Run first-run calibration again for the native engine. The tested configuration for this laptop is **80 ms / 4 threads**, with an **80 ms playback cushion**. Advanced settings allow a 40, 80 or 120 ms cushion: a larger value can reduce playback underruns but adds delay. C++ eliminates Python synchronization problems but does not make the neural network free or guarantee real-time performance under every gaming workload.

## Build and test

Requires Visual Studio 2022 Desktop development with C++, Windows SDK, CMake and .NET 8 SDK. Run `scripts/build-native.ps1` from the source repository. The script accepts explicit `-CMake`, `-Dotnet` and `-OrtDll` paths; the default uses the workspace tools already installed. Official ONNX Runtime CPU 1.31.0 is tested, using backward-compatible 1.23.2 C API headers. SoXR source is pinned to `945b592b70470e29f917f4de89b4281fbbd540c0`; two build-system fixes are applied for workspace paths containing spaces. JSON is nlohmann 3.11.3.

From PowerShell:

```powershell
.\scripts\build-native.ps1
$env:VOICELOCK_DATA="$PWD\data" # optional development cache
.\native\build\Release\VoiceLock.AudioEngine.exe selftest
.\native\build\Release\VoiceLock.AudioEngine.exe devices
.\native\build\Release\VoiceLock.AudioEngine.exe isolate --reference data/fixtures/reference.wav --input data/fixtures/overlap.wav --output reports/native-overlap.wav --threads 3
```

With the real user data folder (omit `VOICELOCK_DATA` or set it explicitly), `live --seconds 60 --report native-live.jsonl` tests the actual selected microphone/cable without saving audio. `--inject-recovery-at 10` explicitly requests one recovery for testing; adding `--stall-ms 400` instead stalls the inference thread for 400 ms to force a real queue overrun. It does not inject into a game or other process. `calibrate --reference clean-voice.wav --seconds 15` runs the CPU matrix. `setup` downloads pinned official model files with hard-coded SHA-256 verification; cached valid models require no network.

The worker is a separate executable to preserve crash isolation and the existing current-user-only named-pipe protocol. See the source repository's native validation report for actual measured results and limitations. Older Python validation results do not describe this worker.
