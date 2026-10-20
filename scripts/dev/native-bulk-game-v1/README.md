# Matched original-game native texture experiment

Offline by default. This controller reuses the accepted original-game Device,
directory inventory/CAS rename, installer acknowledgement and lifecycle-save
helpers. It does not patch an APK, game module, shader, driver or resource.

An explicitly hashed input manifest pins the already built runtime, independently
verified original-NSO bundle, AA driver, current stopped-device baseline, actual
driver fixture receipts and both composed overlays. Both phases use the same
runtime, driver, shader chain, selected ASTC4 textures, 16 MiB Buffer windows,
2750x1260 output and fixed 0.60 world scale. Only the composed texture layout and
native table differ. The original cache, temporary files, modules, overlay and
driver are retained by stopped-process directory renames and restored exactly.
The user's save directory is never backed up, overwritten or reset.

Only the enumerated regular overlay leaves are deployed; never recursively copy
a generated RomFS directory containing original-archive symlinks. Each phase
starts with the same original pipeline-cache seed, if present. Phase results
include the original HUD, normal joystick route, screenshots, existing game
state and a single system `dumpsys meminfo` snapshot. These are functional and
residency evidence, not a sustained thermal/FPS result. Official profiling may
be added only through the existing tools, in a separate window from a debugger.

An optional `counter_snapshot` input has exactly three pinned file records:
`ready`, `config`, and `server`. It uses the frozen `4984509` official LLDB
helper after the normal route, outside any profiling window. The helper reads
four existing global counters once and detaches; its configuration must match
the paired APK/ELF and canonical build record. The exact NDK server is deployed
only into each experiment's isolated temporary directory. No production
runtime counters or per-frame logging are added. A helper failure still runs
the original save/stop path for the owned first PID. Counts describe imported
address ranges and metadata, not resident or unique physical RAM; keep the
system memory snapshot as a separate measurement.

Run with `--inputs PATH --inputs-sha256 SHA --report PATH`. Add `--execute` only
after reviewing the exact immutable manifest and prerequisite receipts. There
is no implicit retry, device discovery, installer settings change, game-state
mutation, hot graphics-policy switch or recursive deletion of unknown files.

## Official malloc profile lane (tools only)

Optional `heap_profile` input object contains exactly `tools_ready` (pinned
read-only capability/tool ledger), `runtime_symbols` (exact candidate ELF),
`driver_symbols` (exact AA185 ELF), and `mode` (`control-only` or `pair`).
All three file pins use `{path,sha256}`. The first actual experiment should use
`control-only` to prove Q startup profiling, without spending a native launch
when the known guest-stack limitation produces a partial profile.

The original `heap_profile` tool is checked against official upstream commit
386d870e04da1a5ca11b6239343b4db518ddea43. Its local copy adds exactly one printed
JSON ownership receipt of the collector PID and trace path it already owns;
`official/ownership.patch` records the entire change. Sampling, configuration,
clocks, SIGINT handling, raw capture, symbolization and cleanup remain official.
No malloc hook, sampler or timer is implemented by this controller.

Each phase arms `no_running` startup profiling only after publishing resources
and installing the APK, while the game PID is absent. It waits for the official
ready line and validates collector UID2000/executable/full command line/trace
path/starttime before the unchanged launcher. At the original after-route
endpoint, it requests the official helper's SIGINT and waits for its remote stop,
raw pull, symbolization and owned trace removal before the existing LLDB counter
snapshot and meminfo. It uses 64KiB sampling, 8MiB shared buffer, 5s dumps and a
180s bound, with `--no-block-client` (the official default would stall malloc).

Tool validity is separate from runtime validity. Official trace_processor
queries and convert-text output must confirm the original PID, startup capture,
at least two dumps and no client error/disconnect/overrun/concurrent rejection,
missing packets or unfinalized profile. The known Mali error2 negative is in the
offline test suite. A bad control profile ends this lane after the owned game
has saved and stopped; it never automatically launches native or retries.
Its distinct `profile_outcome=invalid-or-partial-no-next-phase` exits2 after
successful restoration, not as a runtime crash or original pair acceptance.
A valid control-only profile likewise never claims matched-pair acceptance.

The host helper is never killed to cut off its cleanup. If it does not finish
within20s, only the same UID/cmdline/path/starttime collector may receive one
precise INT, followed by a further5s observation. Unknown/replaced collectors
are not signalled, including indirectly via the original helper's SIGINT handler.
Unconfirmed cleanup is recorded and blocks LLDB/native. It does not prevent the
independent original game lifecycle save2/stop if its first PID still matches
and TracerPid=0. Replacement/unknown game PIDs are never saved/stopped. Original
PSO directories and app/resource restoration still follow the existing journal;
resource restoration does not hide an unresolved collector cleanup error.

Official sampled malloc requests/frees are not RSS, GPU residency, mmap totals
or allocations inside the guest's custom heap. Keep raw and symbolized traces,
health and per-dump tables; do not sum cumulative dumps. Profiling FPS or PSS is
not an unprofiled performance comparison. Actual Q functionality remains to be
proved by the root-owned first control-only run.

A previously uncertain finish does not latch out cleanup: later calls drain any
late official ownership receipt, verify its first exact collector identity, and
may complete its one owned SIGINT flow. The final post-game cleanup retries this
observation even if game ownership failed, while never signalling an unknown
collector. A cleanup-only late recovery never turns a failed profile into a pass.
