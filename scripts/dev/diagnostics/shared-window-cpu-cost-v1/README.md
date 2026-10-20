# Shared-window CPU cost: source-only reproducibility backup

These are byte-identical copies of the privately versioned existing tool
orchestrators and output organizers used for the eddb4ca whole0/window16MiB
comparison. No APK, game module, shader/resource pack, signing key, capture or
profile data is included here. Hardcoded private paths identify inputs that
must be supplied and verified locally; they are not redistributed.

`run-game.py` is offline by default. Actual execution requires an exclusive
lease for the exact phone, verifies original baseline/private source inputs,
uses original joystick/save2/stop, and restores APK/modules/driver/temp/cache.
The capture helper uses official Simpleperf99Hz/stat and optionally the
existing `capture-android-scheduler.py` Perfetto helper, never a custom sampler.
`approve-install.py` only handles the already-authorized normal D2R installer
checkbox/continue for that same transaction. Analysis uses NDK ReportLib and
LLVM with the exact frozen ELF; cache building deliberately never invokes its
ADB/source-pull method. SQL is executed by official Perfetto Trace Processor.

Stat, sample and scheduler windows are distinct. Inclusive sample groups
are not additive, observed fence wall is not pure GPU time, global counter
brackets are not exact8s frame counts, and late PSO/variant changes constrain
any whole-frame/thermal claim. This backup does not change game/runtime code.
