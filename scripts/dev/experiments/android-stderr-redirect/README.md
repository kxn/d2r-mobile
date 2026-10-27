# Android stderr redirection candidate

The cold game entry formerly used Bionic `freopen(path,"w",stderr)`. The saved
TILED-G1 crash aborts in that call while fd2 has a nonzero `unique_fd` owner;
Vulkan/table loading has not started. Bionic's implementation closes the old
fd2 before duplicating its newly opened log descriptor. This exposes a reuse
window to other threads opening files. The crash does not identify which
component owned fd2, nor prove the exact intervening thread schedule.

The candidate keeps the standard FILE, locks its stdio operations, checks that
it still refers to valid, unowned fd2, opens a private log descriptor, checks
again and calls atomic `dup2` without a preceding `close(2)`. It closes only its
private descriptor, clears stdio error indicators and keeps the existing
truncate/unbuffered behavior. Unknown fdsan ownership is rejected without
changing its tag or the process fdsan level. Startup still exits90 on failure.

This removes the helper's own close/reuse window. It does not claim protection
against an unrelated component deliberately closing, retagging or replacing
fd2 concurrently: `flockfile` only coordinates stdio users, and public fdsan
does not offer an atomic owner-check-plus-dup operation. If an external closer
makes the helper's `open` return2, startup is rejected and that descriptor is
left alive until process exit rather than opening another close2 window.

Run from the repository root:

```sh
python3 scripts/dev/experiments/android-stderr-redirect/verify.py
```

This runs the actual helper with real Linux descriptors, four concurrent file
openers and4,000 redirections, including truncation, immediate unbuffered
visibility, prior stdio errors, failure preservation, closed descriptors,
unexpected FILEs and descriptor lifetime. It repeats with GCC ASan+UBSan and
compiles an ARM64 Android29 executable with the real Bionic fdsan API. Linux
does not implement fdsan, so that owner test is explicitly skipped there.
Compiler/source/artifact/log hashes and commands are written to
`.local/build/stderr-redirect-v1/receipt.json`. No app APK or device is touched.

The independent Android29 ARM64 executable has also passed on Q735 using real
Bionic tags: seven cases, including nonzero-owner refusal/preservation and the
four-thread/4,000-redirection stress. Binary SHA256 is
`4fdf1569e7fc18e11f8634686a48ca421b4e5802af363d7f543199bc39a67f1b`.
Evidence is `.local/logs/native-bc3-three-arm-game-v1/stderr-bionic-check/`.
Its owned directory was removed; the current game APK was unchanged, fdsan
settings were not modified, and official SDP records stayed paused/unchanged.
The earlier host build receipt remains immutable and describes its own
compiled-not-executed stage; the device result is separate.

A later application candidate still needs repeated cold starts and a readable
native.log; any performance comparison must use the same rebuilt APK for both
arms. These descriptor tests execute no game modules.
