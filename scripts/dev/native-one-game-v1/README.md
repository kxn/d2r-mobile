# One real native material, original ARM64 game

This is a single reversible validation experiment, offline by default. It is not a production resource release and does not change the source overlay's `deploy_allowed=false` / `game_acceptance=false`. Its input is one real 135,168-byte firepit material, not a complete native texture pack.

The controller reuses the verified shared-window CPU transaction's Device, directory inventories, stopped-process CAS directory renames, PID ownership and original lifecycle save-2 path. CPU captures and whole/window two-phase execution were removed. No runtime or original game binary is edited.

Offline:

```
python3 scripts/dev/native-one-game-v1/run-game.py --report /home/dl/d2r-mobile/.local/logs/native-one-game-bb4ac54-q-v1/result.json
python3 scripts/dev/native-one-game-v1/offline-check.py --report /home/dl/d2r-mobile/.local/logs/native-one-game-bb4ac54-offline-check-v1.json
```

Execution requires the exact root-reviewed AA/ASTC/window original-NVN physical GPU fixture receipt (SHA `755c634376a6bdebde5168bbf0b7cc3e1694c8398bd2f8940a7a51790f8dd9ca`) and the independently frozen official LLDB helper sourceReady. Supply both path/SHA pairs using `--actual-fixture-root-review`, `--actual-fixture-root-review-sha256`, `--debugger-source-ready`, `--debugger-source-ready-sha256`, then explicitly add `--execute`. There is no implicit execution or retry.

The debugger helper is invoked as `python3 run-debugger.py FIRST_OWNED_PID OUTPUT`. It must capture real native binding entry, followed by successful token use and matching image serial/recipe. It must detach and report `native_binding_accepted=true` and `owned_debugger_cleanup_complete=true` in `OUTPUT/debugger-result.json` for native-material acceptance. Missing an early breakpoint is inconclusive, not a runtime failure. A cold installed-table log is insufficient.

Exactly five resource delta leaves are packed explicitly, plus the single table, original-NSO-built module bundle, app-local AA driver and owned LLDB server. No raw RomFS directory is recursively copied. Driver, modules, overlay, cache, temporary files and the table directory are restored after the original lifecycle confirms two fresh save commits and the original PID has stopped. A dead game is never restarted for saving. Unexpected PIDs/ownership or changed backups fail closed and preserve the journal.

The single test uses explicit shared memory, a 16 MiB Buffer window, fixed 0.75 world scale, output 2750x1260 and the existing native 60 Hz render target. It does not change frame-pacing policy, collect custom timing, claim noncoherent-memory hardware coverage, or prove FPS, heat, total texture memory savings or 6 GB playability.

Installation follows the accepted fixture's single Popen/installer-ack loop. Candidate installation and restoration each get one owned client and a unique device/host XML directory. It only acknowledges the exact D2R Mobile package-installer risk checkbox and continue button while that client remains live. A fresh XML must show the checkbox checked before continue. No installation retry or verification setting changes occur. If the client times out or its final installed APK hash cannot be established, the controller preserves its transaction and backups instead of racing an unresolved install with restoration. The first v1 installation-tool failure remains a separate historical result, not a native runtime failure.
