# VoiceLock interface update

The dark dashboard follows the supplied visual reference: sidebar navigation, rounded device/model/preview cards, mint controls, segmented level meters, a performance strip and a live console. The existing native C++ worker and audio processing are unchanged.

Open `dist/VoiceLock-Redesign/VoiceLock.Desktop.exe`. Close the older VoiceLock first. Keep the executable beside its dependency files and worker folder. Existing settings, model cache and enrollment are reused.

Audio Routes and Noise Control highlight the corresponding dashboard controls. Presets opens calibration in setup; calibrated profiles are selected on the dashboard. Voice Setup retains model download, enrollment, deletion and calibration. Advanced retains CPU threads, chunk size, rates, playback cushion, manual gain and diagnostics. The reference's automatic gain toggle is intentionally represented by a working manual gain adjustment because this engine does not implement automatic gain control.

Performance values are real worker snapshots, not the sample numbers in the reference image. Stopped values show dashes; live level meters use the existing measured RMS levels. The preview is visual, not an audio monitoring loop. Clear clears the displayed live console, which updates on the next status poll. It does not delete diagnostic files.

Validation: self-contained Windows x64 publish succeeded. Desktop smoke connected to the actual native worker and loaded existing enrollment and device selections. Setup/advanced navigation assertions passed. Screenshots were reviewed at 1440×880 and 1100×720; the dashboard scrolls at smaller sizes. Segmented meter rendering was checked with synthetic 65% and 40% values, without recording audio. The audio engine was not changed or re-benchmarked for this UI update; previous latency and underrun limitations still apply.

Evidence: `reports/native-redesign-final.png`, associated `.advanced.png`, `.setup.png`, `.compact.png`, `.meter-test.png` and `.checks.txt` files.
