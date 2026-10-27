# 录制入口状态去重：正常 APK 同窗指令与阶段分析

结论：当前 OFF/ON 官方硬件计数表明，**owner 用户态退休指令减少 5.00%–5.14%，周期减少 3.64%–3.78%**。此前普通 ABBA 的 Running/FPS 基本不变，不应据此称新增比较抵消了省工。该单对 PMU 不能证明所有差异都由候选造成，亦未证明稳定整帧涨幅。

## 身份与工具

两组使用同一正常 APK `c8a3b458…`、host ELF SHA256 `d327a5a261ce58cc47405b6c098a92bd2d24b3c4418900027d2cc0651fd9634c`，Build ID `0ea8fc4326161fec14530b9ad27c6cd9f40c0022`。逐实际 record DSO 路径建立 symfs，host/driver Build ID 均须匹配。官方 Simpleperf ReportLib 解析 instructions:u DWARF 与附加 cycles 累计计数；官方 Perfetto 转换同 record 的 MONOTONIC 完整 Surface 边界。没有新增采样器、计时器、运行时计数或设备访问。

两段样本分别 3083/3021，记录元数据均为 lost_records=0、lost_samples=0、truncated_stack_samples=0。无丢失并不代表所有 DWARF 祖先都可恢复；缺失调用链仍保留。

## 精确整 owner 硬件计数边界

| 每完整 Surface 间隔 | OFF | ON |
|---|---:|---:|
| 指令 M | 74.689560–74.739133 | 70.901201–70.952313 |
| 周期 M | 29.799235–29.819498 | 28.691119–28.714306 |
| 完整间隔数 | 858 | 860 |
| 同窗 Surface FPS | 43.109 | 43.180 |
| 同窗 owner Running ms | 19.008259 | 18.808771 |
| 同窗 Running 加权频率 MHz | 1708.931 | 1656.906 |

指令减少 3.737247–3.837932M/间隔，周期减少 1.084929–1.128379M/间隔。以上上下界来自边界内外相邻累计 counter snapshot；**不是统计置信区间**。采样 period 的阶段分配不用于替代这些总计数，也不使用 CLI filtered additional-counter 的前缀累计值充当窗口总数。

相同 PMU 窗口的 Running 约减少 1.05%，同时频率下降约 3.04%。不同普通 ABBA 中 owner 18.9005→18.9685ms、频率1683.75→1610.29MHz，由根代理另行记录，不能与本表混成一组精确因果或按 MHz 线性校正。

## 实际阶段与祖先预算

单位为 sampled M instructions / Surface，属于采样分配估算，不是每函数独立精确 counter，也不是毫秒。

| 互斥职责 | OFF | ON | ON − OFF |
|---|---:|---:|---:|
| 提交规划 | 7.727 | 6.974 | −0.752 |
| 整个 command replay（包含下面细分） | 49.282 | 47.785 | −1.496 |
| 其中 replay state 应用 | 4.172 | 3.429 | −0.744 |
| 其中 binding 准备 | 12.289 | 12.502 | +0.213 |
| 其中 CPU source 预检 | 3.788 | 3.093 | −0.695 |
| 其中其他 draw | 21.074 | 21.009 | −0.066 |
| 其中 replay slice 缺真实 replay 祖先 | 6.170 | 5.886 | −0.285 |
| backend 切片外，实际已知 NVN 录制祖先 | 0.967 | 0.617 | −0.349 |
| backend 切片外，其余 guest/host/unknown | 15.245 | 13.822 | −1.423 |

同窗 planning `walk` SELF 约4.232→3.389M；replay `vk_state_op` SELF1.692→1.100M，`pipeline_set` SELF0.558→0.270M。实际 recording 父调用合计包含新增 bind_state 比较；ON 中 bind_state SELF约0.121M，不能再把它当没有记账的新开销。

变化最大且与机制直接相符的三项是录制、规划、状态应用，采样分配合计约减少1.85M。**不能据此把总3.74–3.84M都强行归为这三处**；CPU source、unknown 栈等变化没有本候选直接删除该职责的证据，保留采样波动/场景细节/其他工作差异的解释空间。当前 binding 与其他 draw 父预算仍然存在，去掉约30%命令节点不等于去掉30%draw资源准备。

## 分类审查

- 使用真实 Perfetto slice 的 depth 选择当前最内层已命名阶段；prefix-max 区间查找会保留子切片结束后仍有效的外层。记录到的多阶段重叠仅为 planning 包在 command replay 中；已互斥处理，未相加重算。
- 所有 owner 样本必须且仅进入一个阶段和一个职责，总和断言相等。
- NVN 录制仅认可真实 `cmd_alloc`、`rec`、`bind_state`、record-state helper 或 public `h_nvnCommandBuffer*` 录制 API 祖先。切片外的其他 owner CPU 没有被统称为翻译或录制。
- 所有未展开 driver/guest 祖先保留实际缺失状态，没有为 parentless driver 拼接 draw/binding 父节点。
- 此 PMU 只覆盖实际 owner TID。诊断 CMDSTAT 是跨录制者的进程级总量，不能用其44844次录制直接除本表0.967M推单次录制成本，也不能声称采到了所有 guest worker 的录制 CPU。

## 与原命令统计的关系

另一诊断 APK 的两轮完整300-NVN-present窗口已证明新记录44844→约31600/帧、bytes1851.0→1333.6KiB，减少29.53%节点、27.95%字节。它有原 CMDSTAT mutex 开销，不是本正常 APK 的 FPS/CPU 窗口。

正常两段宽 game-state 端点的 PSO prepare尝试约2418.327/2419.143次每guest submit，readiness的242 PSO/32 variants相同。它们支持没有通过少画一大批 draw 冒充节省；但宽端点不与内窗 Surface 完全重合，不能作为精确 draw 或 PMU 分母。不得将其当成两个端点命令流逐条一致证明。

## 复现

```sh
python3 .local/logs/record-state-cache-v1/pmu/offline-budget.py PMU-OFF1-BASE --export-only
python3 .local/logs/record-state-cache-v1/pmu/offline-budget.py PMU-OFF1-BASE PMU-ON1-BASE
```

第一条只从实际采样路径建立严格同 Build ID symfs，并导出官方 report CSV/full callgraph；第二条复用原累计计数 reducer 的 snapshot 边界算法，使用官方 Perfetto 实际 phase 与官方 ReportLib 调用链分配预算。

精确输入、工具命令与结果：各 `offline-analysis/<arm>/symbol-source.json`、`official-export-commands.json`、`budget.json`；配对汇总 `comparison.json`；原现成 scheduler/频率与宽工作量端点 `scheduler-and-workload-context.json`。采集脚本、生产源码及原采集文件均未改。

## 实现与边界

实现提交 `730cc69583a5c6ad871605993b213d02ec1ed730`，普通测量 APK
`c8a3b458c11204f3032a938840c256b27b06f0368ac4d9a3b5f321dd7a44146d`。
每个 NVN command buffer 持有可选的151字节shadow，只处理六类相互独立的
固定状态（blend、color、channel mask、multisample、polygon、depth/stencil），
以包含目标槽的完整原状态字节判断相等。命中发生在原 REC 分配/写入之前。
不是根据上一帧猜当前值，不更改游戏 binary，也不消除 Draw、资源绑定、
Uniform 更新、Query、Barrier 或 Fence。

Initialize 仅在冷启动开关开启时分配；失败保持原录制流程。Begin 及其内存回调
前后、CALL/CopyCommands、任意命令内存回调都会失效；普通连续 JUMP 可保持。
MISS 在实际 REC/回调结束后捕获最终载荷，避免重入回调改变状态造成错误命中。
End 失效，Finalize 仅释放shadow。每次访问无 getenv、全局锁、hash或堆分配。
原 runtime 默认OFF；schema5可为通过资格验证的设备显式选择ON。

## 常规运行 ABBA 与回归

四轮同一普通 APK、同游戏1968×900/.85、相同场景/存档、B3/prefer_sysmem/
UBWC/shared，采前要求PSO/特化/图像缓存字段连续稳定并前进150tick。
正常20秒Perfetto窗口不开CMDSTAT/图形抓帧层：

| 普通运行均值 | OFF两轮 | ON两轮 |
|---|---:|---:|
| FPS | 43.2252 | 43.1534 |
| owner Running ms/Surface | 18.9005 | 18.9685 |
| whole app Running ms/Surface | 35.4152 | 35.8272 |
| owner Running加权频率 MHz | 1683.75 | 1610.29 |

帧率−0.17%，不宣称涨帧，也不按频率换算出不存在的固定频率性能。
之后单独的同APK硬件计数对照见前表。命令字节减少是CPU录制写入减少，
不能当作SDP测出的GPU DRAM写入下降。截图人工检查未见该候选新增黑块或缺面，
不是逐像素一致或全场景证明。

生产公开NVN入口主机测试、GCC ASan/UBSan通过；原始CALL、Begin、回调重入、
OOM、分对象、生命周期及Query/更新/Barrier保留有覆盖。真实NVN→Vulkan
GPU全套OFF/ON×同步/异步均通过：同步各23442408项，异步各23806534项，
Core/SYNC validation零错误；共享查询单独四组通过。全套GPU用普通私有pool，
共享子集仅为query夹具，不冒称Android全场景共享测试。

schema5提交 `1129bb4f09b66d7bb618ca19d3e14f3c30667327` 仅改变Java冷配置，
APK `619c6529c8d14ecb5e5c97b4510049d20520484a7ef0a62525dff9f9017d0434`，
全部六个native库及assets与测量APK逐字节相同。schema1–4不产生新extra；
schema5必填严格布尔，沿用原APK/输入/driver/receipt锚点验证，显式Intent旁路不变。
发布与桌面启动仍需独立验证，不能仅靠host通过即称完成部署。

## 原始证据

- `.local/logs/record-state-cache-v1/recording-work-summary.json`及诊断窗口原log。
- `.local/logs/record-state-cache-v1/normal-ABBA-summary.json`及四份Perfetto/receipt。
- `.local/logs/record-state-cache-v1/pmu/`：正常APK两份Simpleperf、同窗Perfetto、实际symfs/工具参数、counter边界及互斥阶段预算。
- `.local/logs/after-shortwait-macro-v1/record-state-cache-host/qualification.json`、`independent-review.txt`及`full-gpu/qualification.json`。
- `.local/build/record-state-cache-v1/`和`record-state-cache-schema5-v1/`：源码/构建清单、native SHA、APK与来源资格。

所有真机arm采后执行原生命周期fresh save2并停止。未使用Mali或6GB平板。

## Q735桌面启动部署已完成

schema5配置明确`record_state_cache=true`，保留原HLE30us、inline/native/shared、
单提交、prefer_sysmem及UBWC等选项；游戏/Shader/driver/native resource原锚点全部核对。
日常输出仍2358×1080、3D比例0.75（与性能对照1968×900/.85明确分开）。
两次不带Intent额外参数的桌面冷启动，均实证读取该配置并打印
`[nvn record state] enabled=1 fixed-before-recording`，进入真实游戏后原生命周期
fresh save2，退出并核实进程已停。receipt与profile采用原停止状态CAS发布，
不修改用户存档或HUD偏好。

最终身份`.local/logs/remaining-upstream-gap-v1/accepted-record-state-deployment.json`；
发布过程`record-state-publication/result.json`（两次ordinary记录及真实UBWC布局）。
profile SHA `d759969dbca068944c5bcc0ac4507b90ed11fb66522d617a503ce1a85db2e238`，
receipt SHA `b859fb27c612946acdf49e2e580e321356805c08342663390326d11348db5035`。
APK仍为上述619c65…，全部六库/assets与已测730cc69逐字节相同。
上游原carrier已恢复；工具/全局Mesa实验开关清除，perf_harden恢复1。
