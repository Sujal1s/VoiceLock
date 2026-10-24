# Troubleshooting

- **No CABLE Input:** install VB-CABLE from its official site, restart Windows if requested, then Refresh devices. VoiceLock cannot create a virtual microphone without a driver. It never installs a driver silently.
- **Missing models / manifest / integrity mismatch:** stop audio and click Download / verify models while online. Do not hand-edit hashes. Check disk space and access to huggingface.co. Offline operation needs all selected files and the manifest.
- **Enrollment rejected:** lower gain if clipped; otherwise speak continuously for three seconds at a normal mic distance in a quiet room. The enrollment check is energy-based; it cannot verify identity or reject every contaminated reference. Rerecord if isolation keeps the wrong person.
- **Unsupported sample rate/channels:** use device-default 48 kHz or 44.1 kHz, ensure another app is not using exclusive access, and retry. Device names must match exactly and be unambiguous. Renamed/duplicate endpoints require reselection.
- **Overload / muted / buffer full:** stop, reduce competing workload and recalibrate. Try a passing 80 ms configuration. More threads may be worse on this laptop. Restart deliberately; raw audio is never used as fallback.
- **Disconnected device:** reconnect hardware, Refresh devices and Start. A process restart also re-enumerates PortAudio endpoints. Hot-plug enumeration may require restarting VoiceLock because PortAudio can cache the endpoint list.
- **Worker disconnected:** output is closed/muted when the worker dies. Exit and relaunch VoiceLock; a lost worker is not automatically relaunched by this MVP.
- **No audio in Discord/Valorant:** choose CABLE Output as that app's microphone, CABLE Input as VoiceLock playback, and a physical mic as input. Check app input sensitivity and Windows microphone privacy permission.
- **Feedback:** stop immediately and select CABLE Input instead of physical speakers. Do not enable Windows Listen to this device on a looped route.
- **Long delay:** inspect both queue depth and estimated latency, remove extra buffering in downstream apps, and qualify 40 ms through calibration. Real loopback measurements take priority over estimates.
- **Metrics remain stale during setup/calibration:** these commands intentionally stop audio and occupy the single IPC request channel. Wait for completion. Close exits the worker if it becomes stuck.

Reports and enrollment are in `%LOCALAPPDATA%\VoiceLock`, unless `VOICELOCK_DATA` is set. A workspace testing cache is separate from the release's first-run folder; benchmark test references are never installed as the user's identity.
