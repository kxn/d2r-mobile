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

## Optional source-only bcbd751 throughput/CPU plan

`performance_tools` pins the copied existing official99Hz/stat helper and the
existing `capture-android-scheduler.py`. Same6217/.60/AA185/16MiB transaction,
no runtime change. The one original3point route finishes first; helper does not
repeat the old5point route. Original bounded PSO/variant readiness then separate
stat8s,99HzDWARF8s,Perfetto sched+frame8s windows all finish BEFORE optional LLDB.
Each tool window records its own target/PSO/variants and public thermal context.
No cross-window sum or ADB/tick bracket as exact frame denominator.

Important observed Q limitation: `--frame-timeline` produces display-wide
actual_frame_timeline_slice (and occasionally Activity TX), but existing Q
captures do not have game SurfaceView actual slices. They DO contain official
SurfaceFlinger per-buffer PresentFenceSignaled events for a unique exact game
SurfaceView. Report those separately as official surface-buffer completion
cadence, not app FrameTimeline or game simulation frames. If the new trace lacks
an unambiguous target SurfaceView or has loss, throughput is inconclusive; do not
substitute all-display slices, overlayFPS, shader_present.selected or guestticks.
Official CPU running within that same Perfetto window can be compared to its
own surface events; stat and sampledCPU are independent windows.

Preparation/plan is offline. Root reviews one exact pair before any execution;
original firstPID/save2/6dir CASrestore/settings/toolcleanup remain unchanged.

### Readiness fix successor

The first c5d51c8 plan was not executed: its readiness journal list was missing
from initialization. The successor only initializes `pre_record_cache_states`.
`test-readiness-fake-device.py` executes the actual helper via runpy with all
real subprocess launches denied: fresh4ticks, PSO/variant reset,30unstable reads,
repeatedtick rejection and repeatedthenfresh pass. Successful tests stop at the
thread-list command before any sampling; they are not fake performance results.

Existing scheduler failure boundary is unchanged: its configured Perfetto session
is duration-bounded8s, but `capture-android-scheduler.py` has no outer finally for
remote trace cleanup. An unsuccessful perfetto command/pull/postcondition can
leave its unique `/data/misc/perfetto-traces/d2r-scheduler-*.pftrace` file and an
incomplete sidecar; the parent still attempts original owned-game save2/stop and
6dir restore. Do not claim scheduler cleanup complete merely because game restore
succeeded. Preserve original capture logs/UUID/path, no automatic retry, and let
the device owner establish stopped tool/session state before another transaction.
This is a documented pre-existing tool boundary, not a new cleanup implementation.
# Finite import window comparison

The input may select `pool_window_bytes` as 1048576 or 16777216 (default).
Launch, official-tool endpoint checks and the offline plan use the same value.
This only selects the existing runtime option; it does not change the runtime,
resource recipes or NSO rewriting. Smaller imports can avoid pinning unrelated
pages but may increase driver objects, unions and descriptor changes. Compare
actual Buffer backing, original-game CPU and memory; do not infer resident RAM
or FPS savings from the chosen window size alone.

The f830 successor preserves the AA path and all existing lifecycle, 1MiB/16MiB,
official tool, PSO readiness and restoration logic. A f830 input must additionally
pin the exact root-reviewed original NVN positive/old-AA table refusal and
fractional-filter receipts. Native composition/table must agree on complete
DMNTBL2 extent/count/checksum/pack/producer and every DMNRES1 checksum plus all
three queried UUIDs for the fixed Q735 device. Old AA bytes cannot be relabeled.
Actual f830 bulk composition is still required before creating executable pair
inputs; passing source checks or finite fixtures does not imply real-game/bulk
visual or performance acceptance. Never reuse an AA native phase with f830.

## Optional official SDP Realtime window (default OFF)

An `official_gpu_tools` input adds only `helper`, `config`, and
`scheduler_helper` `{path, sha256}` pins. The helper must be this tree's
`sdp_realtime.py`; the scheduler remains the existing official Perfetto runner.
The original CPU/stat/record/scheduler windows finish first, then one 12s SDP
Realtime window contains an independent 8s Perfetto Surface/sched capture, all
before LLDB. Omission retains the original flow. No GPU timer or counter reader
is implemented here: the installed Qualcomm2026.9 GUI/service does collection.

Preparation must own one new GUI/Xvfb and every new official remote collector
while the game is stopped; record PID/starttime/executable SHA/UID. Configure the
nine actual Q system metrics, verify the official session's unit descriptions,
and Pause. The prepared retained GUI's actual window PID/geometry and observed
Pause control are checked; do not use this config on a replacement GUI/layout.
The verified keyboard File Save path is `Alt+F`, `S`, selected Name absolute path,
`Return` (Ctrl+S is not a valid shortcut). Each phase saves to a **new** unique
`official-session.sdp`, checks the two original zip members and exact newly
covered timestamps, and retains original `.sdp`, `sdp.db`, version and raw rows.
Before CPU windows and after each GPU window, two count/maxstamp reads of the
owned official DB must remain fixed. These are tool-state guards, not sampling.

Read/Write/TextureBW/Clocks/L1/L2/TP are mandatory with known original definitions,
finite values, no gaps over500ms, and real active rate/TP samples. Missing or
all-idle mandatory rates fail as a tool applicability gap; zero L2 misses remains
valid. Both phases use the same f830 driver/runtime/render options: ordinary
private TILED control versus authenticated native LINEAR. This is not an AA
native-vs-f830 native comparison and no new ordinary-AA baseline is requested.

All raw data includes global compositor/other GPU activity. The 12s counter
bracket is **not** the8s trace. Analysis must crop counter rows to the official
Perfetto clock-snapshot/trace-bounds interval before using that trace's actual
game SurfaceView fence-completion count. Never divide12s bytes by8s frames;
never treat CPU markers as GPU pass time or sum TBDR draw durations. No current
GMEM/per-pass conclusion is supplied. Existing vendor max-frequency fallback
warnings remain. No temperature/sysfs polling, layer injection or shader edit.

Outer `finally` closes only the exact adopted GUI/Xvfb/collector identities,
including failure, independently of original save2/stop/CAS restoration. A
replacement/unknown process is preserved and cleanup failure is explicit. The
existing scheduler helper's failure-cleanup limits remain: a helper still live
after its bound is reported, not silently called terminated. Source-only plan
never starts tools or ADB. Source-pinned current prepared-session metadata is a
single-use handoff, not permission to adopt arbitrary pre-existing tools.

### SDP successor: actual device interval and independent validity

The owned GUI config additionally pins `trace_processor: {path, sha256}`.
Fresh rows are strictly newer than the paused DB maxstamp. Their per-metric
common coverage must contain the complete official 8s capture interval, mapped
by snapshot0 REALTIME/BOOTTIME and the trace end. Backfilled trace-bound history
is retained but excluded from this denominator. The host 12s orchestration
bracket is diagnostic only. Required definitions, finite/nonidle values and
internal-gap checks remain; no offset is guessed and missing values are never
filled with zero. The preserved first capture had valid Turnip counters covering
this official 8s interval; it failed the former host-bracket endpoint guard.

An optional SDP pause/capture failure records `gpu_validity: false` and its
original evidence, disables subsequent SDP windows without restarting the GUI,
and attempts exact owned-tool cleanup. Original CPU/game/memory/save2/restore
continues; cleanup errors remain explicit. Actual game/FATAL and CPU failures
still use the original abort rules. The next pair inputs omit `counter_snapshot`
so no optional LLDB step blocks memory acceptance. No old failed result changes.

The paired launch uses `--external-profile` only to disable custom timing.
Actual performance hints and present worker are reference/inactive, matching
current defaults; their opt-in flags are independent of external profiling.
Do not attribute a frequency or throughput difference to external-profile, and
do not call approximately30FPS the ordinary game's limit.

If an SDK pause check fails and exact cleanup cannot confirm closure, that and
subsequent phases explicitly skip CPU windows (`cpu_validity: false`) while
keeping functional/memory/save2 restoration. Confirmed paused or confirmed
closed tools preserve original CPU collection. No contaminated CPU success is
reported; unknown/replacement collectors remain untouched.

### Owned collector exit/reuse guard

Remote identity reads bracket exe/SHA/UID metadata with two proc stat snapshots.
All shell reads tolerate a disappearing proc file without returning failure.
Absent/zombie at the final snapshot means no executing process; a live partial
snapshot, changed starttime, or reused PID is preserved and reported. A former
nonzero command exit is gone only if a new stat confirms absent/zombie. No
replacement can be claimed from a basename or one stale stat. Original failure
results remain unchanged; cleanup follow-up is separate evidence.

## Native-only 30-minute original-input follow-up

`run-game.py` accepts an optional `sustained_play` object. Both prior control and
native compositions are still fully hashed and authenticated; only native is
published/executed. The exact accepted 6f8188e runtime, f830 driver, 6217 table,
1 MiB windows and paired root acceptance are required. No new game binary,
resources, rendering, sampler, timing probe, SDP GUI or LLDB code is introduced.

Default v2 route repeats the original B(5225,6295) / C(5235,6289) points through
`android-game-walk-to.py`. B→C actually succeeded in v1; reverse C→B remains
pending first actual validation. This is a bounded camp-load route experiment,
not pre-proven whole-route gameplay correctness. Each call retains its before/after position receipt;
a missed target, unexpected area, changed PID or stale world stops the test and
is not silently retried. A navigation failure alone is not a crash or thermal
failure. Root may supply separately proven actual-input area transitions; the
cycle must close. `resume_step` chooses a verified route point for a NEW run;
separated runs cannot be summed into a continuous pass. Town load proves no
combat/field/all-scene stability, and the old Mali thermal result is no Q A/B.

0/10/20/30-minute checkpoints preserve original game state/PSO/target, official
meminfo, overlay screenshot and existing PID-filtered `DmThermal` public-API
logs. No ADB thermal service or sysfs queries are used. Four official Perfetto
8-second windows request scheduler/frequency/app markers and Surface timelines;
actual emitted tracks must be checked before deriving any FPS. Heavy endpoint
observations and logcat reads are outside those windows. Host monotonic deadlines
only bound/schedule the requested duration, not measure frame/GPU performance.
The scheduler wrapper reuses the existing official configuration verbatim and
adds exact UUID-output cleanup in `finally`. Perfetto retains its official
8-second duration, host command bound38s; no claim of a separately verified
collector PID is made on a transport failure.

SIGINT/SIGTERM cooperatively request save/stop AFTER the current bounded child
walk/capture returns, so the input child can release its touch. Alternatively,
create `native/stop-request.json` in the report directory containing exactly
`{"token":"<transaction_token from native/phase.json>","action":"save-stop"}`.
Do not SIGKILL the controller. Any such stop or public-API thermal background
suspend makes `continuous_pass=false`. No automatic resume/restart is attempted.
The original lifecycle helper may briefly foreground the SAME PID solely to
request a fresh save; it immediately backgrounds and confirms commits2 before
force-stop. If it cannot confirm a save, preserve the process/backups and fail.
An already exited process is never restarted to fake saving.

Original APK, six directories (including original PSO/cache), driver, owner/CAS
checks and cleanup are retained. Actual normal-input gameplay/save progress IS
saved and retained; it is not restored from a backup or edited. A complete result
requires uninterrupted route completion plus fresh same-PID save2, stop, full
original-APK/directory restoration and cleanup. The helper's local pass flag
alone is not this transaction-level acceptance.

V1 failed the fourth C→A direct walk after15s at(5221,6293), while original
game remained alive/input-ready, no public-API thermal pause. Three prior walks
were successful; source-only cycle-area closure did not prove that geometric
return path clear. The failed v1 report remains unchanged and its cold8s trace
remains a valid short window, never a30min pass. V2 removes A, retains all fail
gates, and explicitly tests the first reverse rather than claiming it passed.

## Exact26b short native candidate validation

`native_candidate_validation` is a mutually exclusive mode after actual6f
paired qualification. It requires exact26b canonical/raw root review and the
original f830/native6217/.60/2750x1260/1MiB resources. Both matched compositions
remain authenticated; `_execution_phases` selects only native. This stage is
explicitly `native-original-game-short-candidate-validation-complete`, never a
new matched pair or sustained-thermal result. Input defaults DRAFT; root reviews
and publishes a separate authorized input after the running6f test finishes.

The original route orchestrator receives B→C→B three normal walk-to points,
which avoid the v1 C→A obstruction. Every point must complete, otherwise fail
and retain the existing lifecycle/CAS cleanup. No larger navigation or gameplay
logic is introduced. The exact source-pinned CPU helper runs independent8s
simpleperf stat,99Hz8s record and official8s Perfetto, followed by system
meminfo/screenshot. Existing actual tool coverage/target/PSO checks stay intact.
No ordinary rerun, SDP, LLDB, custom timer or second30min run is performed.
Actual normal gameplay progress is saved; original APK and six directories are
restored by original save2/stop/owner/hash/CAS checks.


## Exact80a successor short candidate gate

This tool-only successor retains aa7 run-game, CPU helper, launcher, lifecycle,
resource proofs, route and cleanup byte for byte. Only candidate_gate changes
the pinned runtime to80a and binds the actual root canonical/raw review plus
the successful26b short transaction and its root review. The exact6f matched
qualification remains mandatory. Both control/native compositions stay
authenticated; execute native only, B→C→B and the same three official8s windows.
The26b stability flag was false, so it qualifies function/cleanup only and is
not a matched performance/thermal baseline. Original shared window1MiB,
f830,6217,.60,2750×1260 and6dir CAS restore are unchanged. DRAFT false and
no device operation during preparation; root publishes a separate ROOT input.
This does not enable ADPF, SDP, LLDB or any additional timing/sampling path.


### ed2c139 私有图像预算候选短验证

本工具后继只将严格 candidate gate 更新为已核的 ed2c139 canonical/raw、实际80a
短验证和已有6f真 paired 证据。仍只执行 native 一阶段、原三点路线和现成官方
CPU工具，保持原 six-dir CAS、fresh save2/停止/原包恢复，不加LLDB/SDP或测量器。
现成 game-state 的 private_image_cache 字段及官方 meminfo 用于区分私有总分配、
CPU可重建子集及真正预算释放；累计 allocation 释放不等于同量PSS减少。512MiB
仍是待实际工作集审查的参数，不先认定默认最优、6GB可玩或有净涨帧。


### 773d530 guest-only 后继

严格 gate 仅接纳773d530的独立canonical/raw/root review；旧ed/9bbb保持HOLD。
预算维护只来自HLE原guest入口，内部reopen不淘汰已由实际正负控和root源审闭合。
仍只做一次原游戏native短验证、原路线及现成官方CPU/Perfetto/meminfo；首轮重点
读取既有private_image_cache字段，不据短测认定512MiB最佳或6GB通过。


### d5ecbcd 独立 descriptor history 槽位提示候选

本后继只改 candidate gate、其离线负控与本段文档。原482435f事务、CPUhelper、
launcher、资源/路线和恢复逐字节保留；runtime使用独立80a后继d5，未混773预算。
门要求root实际canonical/raw receipt、六SO/APK和三原始NSO/28产物、独立verify-only，
并保留实际80a短验证与6f paired资格。仍只执行native6217/f830/sharedON/.60fixed/
2750×1260/1MiB、三点路线和独立8s官方stat/99Hz record/Perfetto，freshsave2/CAS恢复。

候选只减少部分stage-last miss后的history hash；16项提示384B/context。每次资源
解析、upload/hazard/native use/dynamicoffset与原last-key拷贝仍执行。原stage-last hit
不维护提示。短测只提供当前实际工作量/频率/CPU热点、功能和恢复证据；80a历史短测
不是匹配A/B，不能用父函数inclusive比例或历史historyhits冒充可省CPU/FPS/热收益。
execution_ready保持false；root审源码后另建ROOT输入，不覆旧工具、旧报告或资源。


### 10c5369 首遍计划写入候选短验证

本 tools-only 后继保持 e66 原事务、launcher、CPU helper、三点路线与 save2/CAS
恢复不变；仅严格 candidate/artifact gate 指向已冻结10c@36bb836及其root审查，
仍保留 actual80a 与6f paired资格。输入额外保留 actuald5 官方结果和773 Q预算
资格，便于比较现成样本。仍只native6217/f830/sharedON/.60fixed/2750×1260/1MiB，
独立8s stat、99Hz record与Perfetto，不加SDP/LLDB/ADPF或任何测量器。

10c自身仅聚合16B planned_op中的4B forward写入，完整逆扫覆盖其余字段；相对
actuald5，此源码还合入773预算与主线文档，不能把两次短测称sole10c单变量。
已有773 Q短测CPU可重建118.99MiB、512MiB预算未触发且累计释放0，只能界定
当时工作集，不能保证新run绝不触发；本次读取原有预算计账确认。短测频率、
PSO/variants、路线视角与工作量需实报；未测候选收益，不能把写指令减少直接
称CPU/FPS/热改善。DRAFT execution_ready=false，root另建ROOT授权输入。


### 633ea75 native retirement 通知门短验证

此 tools-only 后继基于实际通过的0e1c3a3，仅更新严格candidate/artifact gate、对应
离线负控和此文档；原3walk、独立8s stat/99Hz record/Perfetto、CPUhelper、save2
与6dir CAS恢复全部逐字节保留。仍验证native6217/f830/sharedON/.60fixed/
2750×1260/1MiB；不增加SDP/LLDB/ADPF或运行时测量器。actual80a/6f资格门保留，
另pin真实10c短测、633源审/完整canonical与freshraw28 rootreceipt。

633相对exact10c仅增加每Pool8B功能通知计数：无release时collect不扫native链；
release登记在ref--前，store2最后访问，真实ready2摘除才减。原索引/资源锁/fence/
销毁与Poolclose次序不变。10c已有773预算，本次也保留并读既有字段，不混VMA。
采样13/633仅定位历史扫描，不等于全可省；实际PC/工作量/频率/PSO及预算释放要
按当前官方窗口记录，不能把历史短测称匹配A/B或先宣称净CPU/FPS/热收益。
DRAFT execution_ready=false，root另建唯一ROOT输入授权；不覆盖旧工具/报告。


Exact846 logical descriptor CPU candidate successor: only candidate qualification gate/tests and this note differ from the frozen633 tools. Runtime/canonical/fresh raw and source audit are pinned to the actual root receipts. Native6217, f830, fixed0.60 at2750x1260, 1MiB windows and original three walk points are unchanged. Official stat,99Hz user-callstack and Perfetto remain separate8s windows. No new counter/timer, GPU layer, LLDB, ADPF or paired baseline acquisition. Actual633 and6f qualify the existing functional/tool path; their measurements are historical nonmatched references, not a claim of net CPU/FPS/thermal improvement. The DRAFT input defaults execution_ready=false; root alone may author the independently hashed ROOT execution input after review.


Exact98a coldstack ownerlogical descriptor cache successor: only qualification gate/tests and this note change from05cc9fd. Runtime/canonical/fresh raw/source closure pinned to actual root receipts; prior846actualanalysis is a nonmatched historical reference. The original game route, six-directory restoration and official8s stat/record/Perfetto helpers remain byte-identical. No persistentphysicalresource caching, no removed hazards, no new timing, collector or PMUevents. Default DRAFT execution_ready=false; root independently authors hashed execution input after review. Analyze the absolute translation CPU budget, actualcore/frequency/work differences and newownercache coldlookup costs; do not call cachedlookups zeroCPU.

Exact848 combined operation Buffer geometry + alias-gap successor keeps the existing official CPU/Perfetto collection, original route, six-directory restoration and cleanup code unchanged. The qualification gate, tests, this note and the official stat event list change. DRAFT is not executable until the real combined canonical/raw/source audit and independent root review are pinned; no CPU/FPS/heat gain is assumed.

This successor adds only the existing official Simpleperf `instructions:u` hardware event to the original eight-second stat window, alongside cycles and task-clock; system hardware event listing and app-UID PMU capability were checked before this change. Availability/scaling must be read from actual output. It adds no timer, sampler or game instrumentation and does not combine counts across independent windows into per-frame values.


Exact716 texture retirement successor changes only the candidate/artifact gate, its offline qualification checks, and this note. The original official CPU helper (including instructions:u), Perfetto workflow, native6217/f830/.60/1MiB setup, three walk points, fresh save and six-directory CAS restoration remain unchanged. Runtime reduces full-list and full-descriptor retirement searches; previous848 actual qualification is a historical functional/tool reference, not a matched performance baseline. Its stat window had PSO/variant changes, explicitly retained as not stable. Candidate test data and execution input must remain non-executable until exact716 canonical/raw artifacts and independent root review exist. No FPS or sustained heat/memory improvement is assumed.


Exact ebe0 combines operation-owned sampled source handoff and locked Pool span indexing atop actual716. Only qualification gate/tests/documentation change from c8eda95; run-game, installer, original three-point route, save2/six-directory CAS restoration and all official CPU capture helpers remain byte-identical (including the existing instructions:u event). Target remains Q735/f830/native6217/fixed.60/1MiB. Actual716 is a functional/tool predecessor, not a matched performance baseline. DRAFT refuses before device construction while the exact combined artifact/root receipt is pending; root alone creates an execution-ready input after review. No new sampling, timing, driver/resource or game changes.

Optional `official_memory_map: {"tool": "/system/bin/showmap", "format": "json"}` adds one existing Android system-tool snapshot after all performance windows and before lifecycle save. It runs as the already verified ordinary app UID against the same PID, preserves raw per-mapping JSON and checks PID continuity. It does not sample, attach a debugger, scan bytes or implement `/proc/smaps` accounting. Tool availability/JSON fields were checked on Q735 with showmap's own PID; that capability check is not game memory evidence. Snapshot RSS may count shared aliases multiple times; use the system's PSS fields and inspect names before inferring physical duplication.
