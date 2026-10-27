# 整轮性能对照：先核净收益，再看局部优化

`scripts/dev/compare-profile-runs.py` 整理已有官方 Simpleperf、Perfetto 和可选 Snapdragon Profiler 离线输出。它不连接设备、不采样、不加运行时计时、不重新计算硬件计数边缘，也不替代原工具的符号化、callchain 分类或 GPU rate 积分。

默认输出简明 `comparison.md` 和完整 `comparison.json`。Markdown 第一屏是整轮净变化，随后列完整互斥阶段表、最大新增/减少、线程和资源重点；完整 metadata、所有原有资源计数、所有线程、输入哈希在 JSON。

## 调用

```sh
python3 scripts/dev/compare-profile-runs.py \
  .local/logs/record-state-extra-v1/pmu/EXTRA-PMU-OFF1-BASE \
  .local/logs/record-state-extra-v1/pmu/EXTRA-PMU-ON1-BASE \
  --reference-label 'extra OFF' --candidate-label 'extra ON' \
  --output .local/logs/record-state-extra-v1/whole-profile-comparison

python3 scripts/dev/compare-profile-runs.py \
  .local/logs/sparse-plan-v1/pmu/SPARSE-PMU-OFF1-BASE \
  .local/logs/sparse-plan-v1/pmu/SPARSE-PMU-ON1-BASE \
  --output .local/logs/sparse-plan-v1/whole-profile-comparison

python3 scripts/dev/test_compare_profile_runs.py
```

`Δ = Candidate − Reference`。帧时间、Running、指令、带宽下降通常是少做工作；FPS上升是呈现速率提高。不能对所有字段统一使用“负数就是回归”的规则。

## 输入发现与资格

每个 arm 目录读取现有 `analysis.csv`、`capture.json`、`before-state.json` 和 `after-state.json`。目录名可包含 `-BASE` 后缀；去掉该后缀后的名字用于发现父目录 `NAME-receipt.json` 和 `NAME-launch.json`。完整目录名用于发现父目录 `ARM-counter-receipt.json` 和 `offline-analysis/ARM/budget.json`。

这些文件采用仓库现成离线 schema：

- `analysis.csv` 是官方 trace_processor 多段 CSV，包含 `section` 表头、完整 Surface intervals、whole-app/thread Running 和运行期频率等。按每段表头解析，不能作为一张单一表格读取。
- `budget.json` 复用现成 Simpleperf reducer 的 `exact_owner_counter_brackets`、`disjoint_actual_phase_duties`、`top_self_by_duty`、`inputs_sha256` 等。不把inclusive调用栈相加当互斥预算。
- 若预算旁有原reducer的`sample-chain-values.json`，先核其半开MONOTONIC窗口、条数等于`budget.samples`、period总和及每个既有phase/duty分区完全一致，并核原top SELF的period。然后只按已有phase/duty和`chain[0]`聚合原始sample条数；不重符号化、不重分类。完整sidecar校验失败只将sample支撑标invalid，不篡改仍合格的硬件exact边界；文件缺失为missing。原文件路径与SHA进入来源清单。
- Counter receipt 的 MONOTONIC 边界必须与预算一致；预算 Surface 数必须与 `analysis.csv` 一致；实际 trace/counter/receipt 哈希和receipt里的host ELF必须与预算关联一致。Exact边界还须满足上下界与分母一致。资格不符时，预算数据标 `invalid`，不会继续作为精确结论使用。
- 分区要求key唯一、样本period之和等于总period、每Surface归一化一致。分区不完整就不生成可相加的差值表。只有两边都是完整互斥分区时，某职责未出现才可作为该窗口的0；原top self列表缺项不能按0处理。
- `same_binary=same` 要求两臂APK和host ELF哈希均存在且相同。缺少身份为 `unverified`；capture与receipt内部冲突为 `invalid`。这只是binary身份，不是因果证明。

缺某类输入仍生成报告，字段为 `missing`/`null`，不得当成0。JSON语法错误和CSV列错位会直接报错。输入文件逐个记录来源与哈希；不会提交原始大数据。

独立保存的预算可用 `--reference-metadata / --candidate-metadata` 指定一个JSON，内容为 `{"budget_path":"/absolute/path/budget.json"}`。预算仍必须通过原始输入的身份/时窗/哈希核对，不会因override跳过校验。

## 报告如何读

1. 先读第一屏 FPS、mean/p95/max、Surface帧数、whole-app与owner Running、运行期频率和exact指令/周期边界。owner是实际被采线程，可能包含游戏代码、HLE、录制和翻译，不能全部叫“翻译CPU”。不同进程的TID不直接按数值匹配。
2. 再看整张互斥职责表以及所有新增成本。目标路径少了，但其他路径增加、频率变化、调度变化，都可能让Running或FPS暂不变。采样工作量不是精确函数时间；不能拿某项Δother填成已解释根因。
3. 精简SELF函数变化表保留阶段/职责/函数三重定位，只对两边原top列表都有的条目算Δ；未进top列表标not_ranked，不能补0。SELF不与父职责预算再次相加。unknown、缺祖先和宽泛other保留单独身份。`migration` 默认 `unproven`：阶段间一增一减不足以证明工作搬家，需要核对真实祖先与同窗行为。
   表旁显示两臂原sample条数，另列总samples及samples/Surface。几十条样本的SELF差值是有限观察，不是精确调用次数；各sample的period会变化，不能拿条数比例代替指令比例，也不据此制造置信区间。自定义高层组只合计原成员sample；缺失sidecar、非法窗口或原top未列出的SELF都不补0。
4. 默认用现有官方trace_processor从同一原trace自动补齐Sleep/runnable/waker，见下节。工具不可用、原trace或track缺失时才明确missing；工具查询/身份/时窗校验失败标invalid。不以帧长减owner Running构造等待时间；CPU/GPU和其他线程可能重叠。
5. 单对实验不构成跨温度/频率/场景的统计置信度。Exact counter区间是现成reducer的两个采样边缘上下界，不是置信区间。原lost/truncated记录也保留并提示。

## 自动同窗调度：复用官方trace_processor

默认发现仓库 `.local/tools/perfetto/trace_processor`；也可用 `--trace-processor /path/to/official/trace_processor_shell`。原官方launcher的固定版本prebuilt解析函数负责定位其二进制，不另写下载器；报告同时记录launcher和实际执行binary的SHA、官方version、命令、原trace SHA。

每臂输出 `OUTPUT/scheduler/{reference,candidate}/owner-scheduler.sql`、`.csv`、`.stderr`、`receipt.json`。SQL沿用已验证worker-handoff方法：

- 以实际PID+TID和thread生命周期定位唯一owner，避免TID复用污染。
- 使用 `analysis.csv` 的完整Surface **BOOTTIME** 边界；若counter receipt也有该边界，必须一致。绝不把Simpleperf的MONOTONIC值填入trace时间列。
- 在完整owner thread_state序列上先做`lead`，再clip正duration记录到同窗。`dur=-1`是不完整记录，不推断为持续到trace_end，否则会与后续记录假重叠；未覆盖时段单列untracked。
- Running、R/R+、S、D及其他状态互斥汇总为ms/同Surface interval数；Running必须与原analysis一致，状态总量不得大于窗口。
- Sleep对应的waker取紧随后续且连续的R/R+行的`waker_utid`，不能取sleep行本身。分成same-process其他线程、其他进程、self和各未知来源；完整线程名/TID/PID保留在JSON。
- Waker分组必须覆盖全部S/D clip时长。来源未知单列；某线程唤醒owner并不证明全部sleep都在等锁/GPU，也不证明其工作恰好等于该睡眠时长。

同trace/SQL/tool/CSV哈希全部一致时复用已验证官方整理结果，避免反复载入同trace。`--no-scheduler`允许显式关闭此可选工具步骤，报告会记录关闭原因；默认不会因为旧CSV未导出就放弃工具可取得的状态。

第一屏同时列owner S、D、Runnable、untracked与waker几大类变化；后面保留完整互斥状态表。若某category在完整分类的一侧未出现，可以是该窗口0；track或数据缺失仍是missing，不能补0。

## 资源计数：禁止偷换窗口和分母

游戏状态的 `time_ns` 使用原有MONOTONIC时钟。程序列出 before/after 时间、是否括住完整Surface窗口、前后额外时长。

只有明确声明为单调累计计数的资源字段才计算差值。默认覆盖descriptor、资源准备、pool上传、readback/writeback相关现有字段、提交与barrier等；live_bytes、当前allocation、对象数量、容量等gauge不推算速率。计数倒退标 `reset_or_invalid_window`，不报告为“省下工作”。

归一化除数是 **同一对before/after状态之间的guest_submit增量**，名称始终为 `per_guest_submit`，不叫每Surface帧。真实extra例子中，Surface有860个interval，状态快照却跨900个guest submits；拿资源差值除860会错。若快照没有同窗提交计数，只报告原差值，不补一个外来的帧数。两类窗口不完全一致时，只能作相关性线索。

## 可配置高层分组

开关对照默认同时读取launcher的`requested_*`/`actual_*`标量，以及已有
`performance_hints`、`async_present`和`actual_game_state`内的present-worker/
performance-hint真实布尔状态。这样不会把只在嵌套状态中出现的呈现线程
实验错误地显示为“没有改变开关”。计数器和对象不当作开关。
组配置可再增加`"switch_paths": ["before.my_policy.enabled", "launch.my_mode"]`，
来源限`launch`、`before`、`after`、`receipt`，只允许有限标量；缺项保持null，
不补成false。配置路径仅整理原数据，不猜测实际生效，也不代替工具时窗资格。

`--groups groups.json` 接受例如：

```json
{
  "groups": [
    {"label": "资源绑定准备", "phase": "replay", "duty": "binding_preparation"},
    {"label": "源数据验证", "duty": "CPU_source_preflight"}
  ],
  "counter_paths": ["my_existing_monotonic_counters.uploaded_bytes"]
}
```

规则按顺序首次命中；只用现有phase/duty标签，不写死函数地址。未匹配的条目保留原完整key，不扔进一个黑洞other。合并成员也在JSON保留；即使把unknown合进自定义组，未解释清单仍会识别出来。额外 `counter_paths` 是调用者对现有字段单调语义的显式声明，不应填gauge。

## 可选GPU：只消费已有官方整理结果

支持两种原有schema：`{"metrics":[{"name":"...","mean":...}]}`；或现成analyze-geometry/analyze-gpu产出的 `{"arms":{"NAME":{"metrics":{"METRIC":{"time_weighted_mean":...,"integral_per_complete_Surface":...}}}}}`。

```sh
python3 scripts/dev/compare-profile-runs.py \
  .local/logs/scale-causality-d015-v1/gpu-tools/SCALE-SCALE85-G1 \
  .local/logs/scale-causality-d015-v1/gpu-tools/SCALE-SCALE65-G1 \
  --reference-gpu .local/logs/scale-causality-d015-v1/gpu-tools/comparison.json \
  --candidate-gpu .local/logs/scale-causality-d015-v1/gpu-tools/comparison.json \
  --reference-gpu-arm SCALE-SCALE85-G1 --candidate-gpu-arm SCALE-SCALE65-G1 \
  --output .local/logs/scale-causality-d015-v1/whole-profile-comparison
```

只读取原time-weighted mean及积分结果，不重新实现rate积分。若还没有原GPU整理结果，应先用已经验证的官方SDP数据库整理入口处理；此程序不直接解析数据库或counter samples发明另一套算法。

只有GPU整理结果的trace哈希和完整Surface BOOTTIME边界都与该arm相同，才标为同窗；否则明确 `unverified`。所有GPU计数始终标global/system（含compositor等），不能当进程独占。

`Clocks / Second` 保留clock-count rate，不改叫实际瞬时GPU MHz或锁频。百分比计数可能互相重叠，不相加，不乘帧长变成独占stall ms。没有GPU数据时不宣称GPU bound，也不声称“FPS不涨已由GPU解释”。Scale GPU实验单独成报告，不能嫁接到extra/sparse CPU窗口伪装成同窗采集。

## 非标准目录的身份与owner证据适配

GPU工具窗口的目录可能没有自动发现规则所需的run receipt，也未采Simpleperf。不得因此猜最忙线程或伪造一个budget。现在 `--reference-metadata / --candidate-metadata` 除了原budget_path，还接受显式证据：

```json
{
  "run_receipt": {
    "path": "/absolute/path/original-run-result.json",
    "sha256": "原文件的64位小写SHA256",
    "fields": {
      "pid": "PID",
      "apk_sha256": "APK.apk_sha256",
      "host_elf_sha256": "APK.host_library_sha256",
      "owner_tid": "owner_TID",
      "source_revision": "source"
    }
  },
  "launch": {
    "path": "/absolute/path/original-launch.json",
    "sha256": "原文件的64位小写SHA256",
    "fields": {"pid": "pid"}
  },
  "owner_evidence": {
    "path": "/absolute/path/original-native.log",
    "sha256": "原文件的64位小写SHA256",
    "line": 587,
    "text": "[dm] [nvn] translation CPU mask=80 tid=23904 result=0",
    "regex": "\\[dm\\] \\[nvn\\] translation CPU mask=80 tid=(?P<tid>\\d+) result=0"
  }
}
```

以上SHA是占位说明，须填原文件真实hash；实际可运行例子保存在surface-parity日志的owner-metadata目录。不要用这个例子的TID替代另一进程的实际TID。

规则：

- 所有显式文件路径必须为绝对路径、原文件SHA必须匹配；字段路径是显式dotted selector，不猜不同项目的嵌套布局。run receipt的PID与APK必须同时匹配原capture；launch的PID也必须匹配，若launch无APK字段则依赖已验证run receipt绑定。
- `owner_evidence`为可选日志证明：一基行号、原行全文、全行regex必须同时符合，regex必须命名`tid`。如同行有命名`pid`必须等于capturePID；否则必须有已验证run receipt把该日志证明绑定到同一capture身份。支持logcat行如 `I/dm-vulkan(26930): NVN pin tid=26997 ...`，可在regex中分别命名pid/tid。
- 所有声明的owner证据必须一致；显式日志、receipt、budget、官方唯一has_replay冲突时拒绝，不悄悄换个来源。没有明确证据时owner为missing，不按Running排名猜。
- 官方`thread_CPU`恰有一个`has_replay=1`也可独立确定工作负载owner；多个标记行不提供唯一owner。带确切日志/receipt的场景允许在多个线程中明确指认一个，但该TID必须对应CSV中的唯一原行。
- CPU总表和scheduler使用同一resolve_owner结果。官方scheduler还要以capturePID+同TID+生命周期定位原trace；结果不匹配就拒绝owner，而不是显示另一线程。正常报告中 `trace_PID_TID_verified=true` 记录此步已通过。
- 这些元数据不生成PMU budget。没有counter.data/合格budget时，instructions/cycles及sampled职责仍missing。GPU工具窗的 `normal_FPS_claim_allowed=false` 原声明也保留，并在Markdown提示不能当普通运行FPS基线。

真实surface-parity两臂适配后，own23904与upstream26997均可显示真实Running、频率、S/D/Runnable与waker表；原先不完整的报告以before-owner-adapter前缀保留，未重新采集。
