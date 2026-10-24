# Repeated overload recovery fix

The old engine stopped permanently after more than five capture recoveries in ten seconds. That threshold now displays a warning and continues the existing recovery flow. Each recovery briefly mutes output, discards stale bounded audio queues, resets the recurrent model/resamplers using the existing enrollment and resumes isolated output automatically. It never switches to the raw microphone.

This prevents the specific sustained-overload shutdown. It does not guarantee uninterrupted speech: actual CPU stalls can still cause gaps or lost words. Device disconnection, unresponsive hardware and model failures remain separate errors that can require intervention. No automatic device reconnect is claimed.

Validation on the selected IMMORTAL microphone and VB-CABLE: 30 seconds, 4 threads, 80 ms chunks, 80 ms cushion. Injected eight 400 ms inference stalls one second apart, exceeding the old shutdown threshold. All 30 snapshots remained running without error; final state was unmuted with eight recoveries, 134 capture overruns caused by the injected stalls, nine playback underruns and zero device errors. Final p95 inference 63.37 ms. Only counters were recorded, no microphone audio. Core queue/resampler tests also passed.

Evidence: reports/repeated-overload-recovery.jsonl and reports/repeated-overload-recovery-final.json. Recheck retained counters with scripts/check_repeated_recovery.py.

Close the older app and open dist/VoiceLock-Recovery/VoiceLock.Desktop.exe. This release includes the redesigned interface and reuses existing settings/enrollment. A one-time app switch is necessary because Windows cannot replace a worker executable while it is running.
