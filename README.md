# VoiceLock for Windows

Offline target-speaker isolation with a **C++20 audio engine** and a **C# / .NET 8 WPF desktop interface**. Enroll your voice, select a physical microphone, and send isolated audio to a virtual cable for your calling or recording app.

## Download

Download the Windows x64 portable ZIP from [Releases](https://github.com/Sujal1s/VoiceLock/releases). Extract the entire ZIP and run `VoiceLock.Desktop.exe` inside its folder. Keep the worker and runtime files alongside it. Python is not needed to run the release.

## Setup

1. Install VB-CABLE using the **Install VB-CABLE** button if missing. The app downloads and verifies the official vendor package, then opens its administrator installer. Install the driver and restart Windows.
2. Select your physical microphone as input and **CABLE Input** as processed playback.
3. In **Voice Setup**, download the models and record a clean three-second voice reference with consent.
4. Calibrate in **Presets**, or start with a manual configuration. The tested laptop used four CPU threads, 80 ms chunks and an 80 ms playback cushion; other hardware may need different settings.
5. Start isolation. Select **CABLE Output** as the microphone in Discord, your game or OBS.

Enrollment and models stay in `%LOCALAPPDATA%\VoiceLock`. Inference runs locally. Models and the optional driver download require internet; live audio is not uploaded. Minimize to keep running in the tray; closing exits.

## Current behavior and limitations

- Native WASAPI capture/render threads, bounded atomic queues, persistent ONNX Runtime CPU inference and SoXR resampling.
- Repeated processing stalls recover automatically instead of permanently muting after a recovery-count threshold. The worker never falls back to raw microphone output.
- Dark dashboard with device routing, VB-CABLE setup, segmented meters, measured performance values, calibration and advanced settings.
- This is an experimental release. CPU overload can still cause audio gaps, lost words and elevated latency. Device/model errors can still require intervention. The displayed latency is an estimate, not an acoustic measurement. Automatic gain control is not implemented; gain is manual.
- No claim of glitch-free gaming, under-150 ms delay, perfect speaker isolation or long-session stability.

See [automatic recovery validation](docs/AUTO-RECOVERY.md), [native validation](docs/NATIVE-VALIDATION.md), [interface notes](docs/UI-REDESIGN.md) and [third-party notices](THIRD-PARTY-NOTICES.md). Raw local test reports, recordings, model caches and machine-specific settings are excluded from this repository. Documentation references to `reports/` describe retained local evidence, not bundled files.

## Build

Requires Windows x64, Visual Studio 2022 Community with Desktop development with C++ and Windows SDK, CMake, Git, .NET 8 SDK and an official ONNX Runtime CPU x64 DLL (tested 1.31.0). See [native build details](docs/NATIVE.md).

The current build script uses workspace dependency paths. Before running it, provide the .NET SDK under `.tools/dotnet`, the VoxLock upstream license sources under `vendor/voxlock`, and the ONNX Runtime DLL with `-OrtDll`; CMake may be supplied on PATH or with `-CMake`.

```powershell
git clone https://github.com/arbazsiddiqui/voxlock.git vendor/voxlock
.\scripts\build-native.ps1 -CMake cmake -Dotnet .\.tools\dotnet\dotnet.exe -OrtDll C:\path\to\onnxruntime.dll
.\native\build\Release\VoiceLock.AudioEngine.exe selftest
```

The script downloads pinned native headers and SoXR source and builds `dist/VoiceLock-Native`. The earlier Python implementation and evaluation tools remain for reference; see [legacy documentation](docs/LEGACY-PYTHON.md). The portable release is the easiest way to try the app.

Third-party components retain their own licenses. Model weights and the VB-CABLE driver are downloaded separately and are not included in the release.
