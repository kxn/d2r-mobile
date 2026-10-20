# One real native material, original ARM64 game

This is a single reversible validation experiment, offline by default. It is not a production resource release and does not change the source overlay's `deploy_allowed=false` / `game_acceptance=false`. Its input is one real 135,168-byte firepit material, not a complete native texture pack.

The controller reuses the verified shared-window CPU transaction's Device, directory inventories, stopped-process CAS directory renames, PID ownership and original lifecycle save-2 path. CPU captures and whole/window two-phase execution were removed. No runtime or original game binary is edited.

Offline:

```
python3 scripts/dev/native-one-game-v1/run-game.py --report /home/dl/d2r-mobile/.local/logs/native-one-game-bb4ac54-q-v1/result.json
python3 scripts/dev/native-one-game-v1/offline-check.py --report /home/dl/d2r-mobile/.local/logs/native-one-game-bb4ac54-offline-check-v1.json
```

Execution requires the exact root-reviewed AA/ASTC/window original-NVN physical GPU fixture receipt (SHA `755c634376a6bdebde5168bbf0b7cc3e1694c8398bd2f8940a7a51790f8dd9ca`) and the independently frozen official LLDB helper sourceReady. Supply both path/SHA pairs using `--actual-fixture-root-review`, `--actual-fixture-root-review-sha256`, `--debugger-source-ready`, `--debugger-source-ready-sha256`, then explicitly add `--execute`. There is no implicit execution or retry.

The debugger helper is invoked as `python3 run-debugger.py FIRST_OWNED_PID OUTPUT`. It must use protocol `native-bind-transfer-fragment-v2`: real native binding entry, typed B2T view/mip/ROI, successful TRANSFER use and successful FRAGMENT use for the same image serial/recipe. It must detach and report `native_binding_accepted=true`, `transfer_and_fragment_proven=true` and `owned_debugger_cleanup_complete=true` in `OUTPUT/debugger-result.json` for native-material acceptance. Missing an early breakpoint is inconclusive, not a runtime failure. A cold installed-table log is insufficient.

Exactly five resource delta leaves are packed explicitly, plus the single table, original-NSO-built module bundle, app-local AA driver and owned LLDB server. No raw RomFS directory is recursively copied. Driver, modules, overlay, cache, temporary files and the table directory are restored after the original lifecycle confirms two fresh save commits and the original PID has stopped. A dead game is never restarted for saving. Unexpected PIDs/ownership or changed backups fail closed and preserve the journal.

The single test uses explicit shared memory, a 16 MiB Buffer window, fixed 0.75 world scale, output 2750x1260 and the existing native 60 Hz render target. It does not change frame-pacing policy, collect custom timing, claim noncoherent-memory hardware coverage, or prove FPS, heat, total texture memory savings or 6 GB playability.

Installation follows the accepted fixture's single Popen/installer-ack loop. Candidate installation and restoration each get one owned client and a unique device/host XML directory. It only acknowledges the exact D2R Mobile package-installer risk checkbox and continue button while that client remains live. A fresh XML must show the checkbox checked before continue. No installation retry or verification setting changes occur. If the client times out or its final installed APK hash cannot be established, the controller preserves its transaction and backups instead of racing an unresolved install with restoration. The first v1 installation-tool failure remains a separate historical result, not a native runtime failure.


Version 3 preserves the original v2 failed result and corrects its logging-channel postcondition. The normal game redirects only stderr to native.log, unlike fixture_output_open which redirects both stdout and stderr. The authenticated table, the frozen validator-success LOG (after active-driver ELF, three UUIDs and pack/producer/digest checks), and the real LLDB recipe are independently matched byte for byte; a missing stdout JSON is never treated as identity mismatch. This does not weaken identity validation.

After the original HUD launcher returns, state and a PNG are pulled and hashed before provider/native postconditions. A second state/PNG follows the first three joystick targets from the previously validated five-point camp route, only when the current game coordinates are near that historical starting point. Each point uses the existing walk-to tool with its 15-second bound; no game state is written. Screenshots get unique token paths and finally retries their pull/hash/CAS removal on failed postconditions. Missing FRAGMENT evidence remains inconclusive. The LLDB capture and screenshots are functional evidence, not profiling or FPS measurements. Capturing images permits later visual review; the controller itself cannot claim visual approval.
