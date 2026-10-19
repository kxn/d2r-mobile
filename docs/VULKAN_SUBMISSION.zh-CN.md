# Vulkan 提交、依赖与完成发布

目标是让录制、顶点/分桶、片元及 CPU 发布在正确的数据依赖下重叠，保持原 NVN
命令顺序、查询累计前缀、游戏事件及栅栏语义，不改游戏或 shader 算法。

## 已实现的提交边界改动

原来每个 `mtl_sub_begin()` 和 `commit()` 都发出 `ALL_COMMANDS → ALL_COMMANDS`
全局屏障；即使绘制入口只等待片元阶段，提交边界也可能重新阻止后续顶点工作。
现在移除默认的这两处全局依赖。提交前完成的 CPU 写入依靠 Vulkan 的提交域操作；
GPU 消费者仍在实际 transfer、compute、render 依赖点建立同步。
见 [Vulkan 的提交/host 写入规则](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-submission-host-writes)。

池回读按实际字节范围保存生产阶段。重叠范围合并时同时合并生产阶段，TRANSFER
生产者与 compute/fragment/vertex shader 生产者分别记录；不能识别的生产者保留
保守源范围。回读前只对该 `VkBuffer` 区间建立生产阶段到 TRANSFER_READ 的依赖，
而不是让全部 GPU 阶段等待。GPU 复制到 CPU 专用暂存和查询结果后，统一保留
TRANSFER_WRITE → HOST_READ，再等待真实 fence，非 coherent 内存仍 invalidate。
CPU 专用结果缓冲不再被当作后续顶点/UBO/indirect 输入的生产者。

这不是完整的所有资源 hazard 图。原有显式 NVN 顺序、compute/transfer 与复杂别名
分支仍有保守依赖；后续继续按真实消费者收窄，不能仅删除屏障。完整依赖跨内部
command-buffer 边界保留，尤其不能在新批次开始时遗失尚未满足的 vertex 可见性。
`submission_barrier_reference=true` 可在同进程恢复原提交/回读屏障，策略在批次入口
快照，`game-state.jsonl` 报告实际生效值。

## 安全地提前送出批次

累计至少 256 draws 后，真实目标切换处结束原本就要结束的 pass；其他命令检查点
必须确认 Render Pass 与 Occlusion Query 都关闭，才 `vk_internal_commit()`。
保留原操作/依赖扫描成立条件，不为达到配额而从任意 draw 中间切 pass。
不再以“每个 NVN 提交最多四块”限制后续安全边界，已有最多八份在途资源提供背压。
查询、常量更新、事件及最终像素仍与单批次路径一致。

提前提交只取得内部队列 ticket，不推进游戏 fence。原 NVN 最终提交才推进一次
guest fence，跨批次的查询累计结果及 reset/report 位置保持。回归用六个每段超过
配额的 pass 验证六个内部批次、一个 guest fence、累计查询和全部像素。

## 异步 GPU 完成与所有权

现有完成线程继续承担唯一的 GPU fence 等待与 CPU 发布，不把等待搬回录制线程：

1. 录制所有者独占当前 command buffer、描述符、查询、暂存和不可变效果列表。
2. `vkQueueSubmit()` 后整份资源转入 FIFO 在途队列，录制者可取得下一份继续录制。
3. 完成线程等待真实 fence，invalidate 回读，先发布全部 GPU/已知 CPU 快照，再发布
   事件和完成计数，最后交还资源。完成前不复用引用、命令池或查询池。
4. 资源上限用尽时只等最老的一份；真正的 CPU 读依赖、上传覆盖、显式 finish 和
   生命周期排空继续等待必要的完成条件。

这部分基于已有完成线程和资源所有权实现，本轮并未另写一个模拟完成信号的系统。
测试用真正的 timeline gate 阻挡 GPU，确认能同时保有两份真实在途提交，事件和
像素在实际完成后才可见。内部批次增加后沿同一所有权模型回收。

## 验证与测量

核心候选 c01798ca… 的同步校验通过：lavapipe 512159 checks，Mali-G710 512940，
Adreno650 509997，没有 VUID/SYNC-HAZARD。旧全局屏障参考模式在宿主也通过。
后续 APK f7fa8346… 只增加实际策略观测和启动核对，GPU 命令语义不再变化；两份
APK/ELF/247 个源码输入及归档均已核验。没有改写游戏 binary 或正式 shader cache。

真实游戏用同一进程做 candidate/reference/reference/candidate 四窗官方
LWI/gatord/Streamline 对照：新路径为精确回读/提交依赖和提前分块，参考路径为全局
提交/回读依赖和单块；两者都保留异步完成。固定原生 2772×1240、3D 1.0，检查实际
策略、同 PID、无冻结/外推。状态及原始窗口记录在
`.local/logs/scoped-submission-game-windows.json`；完成分析前不预告涨帧幅度。

第一版四窗真实游戏均值为 14.008 对 13.548 FPS，约 +3.40%，未达到 30 FPS。
两模式每帧算术和 LS 工作量相近；更多批次伴随 interrupt/MCU/cache-flush 计数增加，
不据此声称零成本。记录为 `.local/logs/scoped-submission-game-official-comparison.json`。
不与不同温度/时段的先前 18.851 FPS 直接做代码效果比较。

## 继续拆开 compute 与独立顶点工作的依赖

第二版把 compute 入口依赖的目的阶段限制为 COMPUTE_SHADER，保留全部必要的源访问；
不在 compute 完成后立即发往所有阶段的屏障。实际输出池范围由已有回读声明确认，
写入后进入跨批次的待可见区间表。CPU 完成/发布不会删除该表；真正到达所有阶段的
GPU 屏障才可以清除，防止“fence 已完成，所以不存在后续 GPU 可见性依赖”的错误。

绘制前检查实际使用的顶点流、索引范围、VS 的池内 UBO/SSBO 与这些区间是否相交。
有交集仍建立完整依赖；VS 图像/texel 读取仍保守处理。没有这些依赖时，后续顶点与
分桶可先执行，片元消费者依然通过其屏障等 compute 的真实输出。区间表到达固定
容量后保守回退，不丢弃写依赖。图形着色器的可写资源暂保留原完整屏障。

NVN barrier 的缓存/顺序请求可延迟到实际消费者；强制 primitive/indirect 顺序与 TOP
时间戳仍有完整依赖。不会因为当前没有 active Render Pass 就立即发往全部阶段。

已补充 compute 生产索引以及跨两个真实 NVN 提交的 compute→VS/索引消费者像素回归。
无 QueueFinish 夹在生产者和消费者之间。第二版同步校验通过：宿主 513965，Mali
514746，Adreno 511803；候选 APK 53ae9248…、ELF f3ebebd1…、247 源输入归档于
`.local/build/compute-overlap-candidate/`。真实游戏验证与官方对照仍须单独完成，测试
通过数量不是帧率或跨所有游戏场景正确性的替代证据。


第二版真实游戏先修复了 WSI acquire 链：交换链图像的初始布局变换从
TOP_OF_PIPE 改为 TRANSFER → TRANSFER，与 acquire semaphore 的 TRANSFER wait
正确衔接。APK 30e03ef2… 在实际营地、原生分辨率下启用同步验证，未报告
VUID/SYNC 错误；证据为 `.local/logs/compute-overlap-wsi-realgame-validation-result.json`。

同一 PID 10648 四窗官方对照为候选 14.475 / 14.140，参考 13.304 / 13.343 FPS；
均值 14.307 对 13.323，约 +7.38%。实际带宽和算术工作量基本不变，MCU
cycles/frame 约 -11.01%，GPU active cycles/frame 约 -1.11%，中断约 +62.24%。
这些是重叠硬件计数，不能相加当作独占耗时；温度、天气及 DVFS 未锁定。
结果不是 30 FPS，也不能把两个不同构建的非同时窗口直接当作增量收益。
完整记录 `.local/logs/compute-overlap-wsi-game-official-comparison.json`。

## 后续消费者范围与显示票号

复制/transfer clear 的目的阶段进一步限制为 TRANSFER；附件 clear 限制为
fragment/depth/color 阶段。准备纹理描述符时已知消费者是 FS 或 CS，原附件读取
依赖分别送达该阶段，VS/未知阶段保守回退。目的阶段被收窄后，既有的 buffer/
vertex 待可见状态不会被清掉；后续真实消费者继续建立可见性，不能把 transfer
屏障误当作对所有 shader 阶段生效。

原 Metal `mtl_present_wait()` 等待的是显示线程已提交 blit，并不等 GPU 完成。
Vulkan `vk_window_present()` 返回票号前已经提交 blit；翻译队列
`x_present_ticket()` 又已等待该任务执行完。故异步候选在 acquire 原 NVN 纹理时
只核对票号确实已提交，不再在 CPU 上等待这张票号的 GPU fence。纹理复用仍由
同一 GPU 队列及已有 read→write/attachment 依赖保证；WSI 的二进制 semaphore
槽位复用、CPU 读写冲突、显式 finish、资源上限和完成发布仍等待真实 GPU 完成。
参考模式仍走原 fence 等待。此处没有改变游戏时钟、丢弃绘制或提前发布 CPU 结果。

回归为 GPU 队列加入真实 timeline gate：present 所有权在 gate 未开启时即可
返回，但 CPU 结果必须保持原值、完成序号不得提前；开启后再核对最终结果与
信号消费和复用。此后还需以真实 WSI 同步验证和官方 GPU Profile 验收。


上述消费者/显示票号候选 d1112ab6… 已通过宿主 514101、Mali 514882、Adreno
511803 checks；实际营地 PID 13447 开启同步验证未报告错误，画面正常。
Adreno 不支持该 timeline gate，故其二进制信号链通过不算 gate 覆盖；真实
未完成 GPU 的所有权/结果发布测试由宿主与 Mali 完成。官方性能四窗另行采集。
交换链显示的裁剪复制和最终 blit 前依赖也收窄至 TRANSFER，避免在每帧结尾
重新引入全局目的阶段；WSI acquire 仍沿 TRANSFER wait/布局变换链，图像复用
依赖仍由后续真实消费者保证。该两处后续改动需要实际 WSI 同步验证。


显示票号和实际消费者进一步收窄后，同 PID 14143 的官方四窗为候选
15.636 / 15.257，对照 13.807 / 13.365 FPS；均值 15.447 对 13.586，约 +13.69%。
GPU active cycles/frame 约 -12.36%、MCU cycles/frame 约 -42.03%，每帧外部读写
带宽、算术及 LS 工作量相近；新增分块的中断成本仍在。原始结果为
`.local/logs/async-present-gate-game-official-comparison.json`。四窗均 Thermal Status 3，
表面温度由约 48℃ 到约 52℃；仍非锁频/锁温实验，也未达到 30 FPS。

历史 `.local/profiles/present-copy-elision-native-game.capture.json` 中的 18.851 FPS
是在 Thermal Status 0、SKIN 约 44℃ 记录的。不能将其与当前热状态的 14/15 FPS
直接相减作为代码回退结论；完整的绝对帧率比较需匹配温度、GPU 频率和场景。


最终显示 blit/裁剪依赖收窄的独立四窗记录
`.local/logs/present-transfer-game-official-comparison.json` 为 15.121、13.353、13.301、
9.797 FPS（候选/参考/参考/候选），候选均值反而低约 6.52%；不能隐藏末窗或把
此组写成已验证涨帧。此时每帧 GPU active cycles 约 -15.18%、MCU 约 -47.95%，
算术/外部流量相近。四窗后新采的官方 Perfetto thermal 类别在同一 PID 16478
记录 `gpu_freq`/`gpu_freq_max` 均为 383 MHz，`gpufreq` 均为 383000 kHz；这不
追溯证明整个 ABBA 每个窗口的频率相同，恰恰说明需要匹配温度与频率重新比较。
见 `.local/logs/present-transfer-hot-frequency-counters.csv` 和对应 pftrace。
trace 有 2068 条 systrace_parse_failure，所列频率轨道有 98/193 条有效样本，不能
把未解析的厂商其他事件当作有效数据。此处记录工具原始计数，不自行计时/采样。

后续将 transfer 写入也纳入跨批次逻辑地址区间：B2B、UpdateUniform、T2B 和对齐
storage 回拷的目的地址已知，仅在与实际 VS UBO/SSBO、顶点/索引输入相交时
恢复完整顶点可见性；只有 FS 读到该范围时保留 FS 目的阶段。未知暂存/对齐来源
继续保守标记，固定表溢出同样回退。复制入口为 TRANSFER 目的阶段，不能因为
“之前出现过任何复制”就让所有后续顶点工作一起等。

同一像素回归增加 B2B/UpdateUniform 产生 VS 数据与索引，覆盖七种对齐、同批次
和独立 NVN 提交，检查相邻红/蓝片元和 CPU 发布；没有生产/消费间 QueueFinish。
候选 APK 6dde33f2… / ELF cd96e7df…、247 源输入归档于
`.local/build/transfer-range-candidate/`。宿主 521325（含旧屏障参考模式），Mali
522106、Adreno 519027 checks 通过；真实游戏与新对照另行验证。


transfer 范围候选的真实营地 PID 18758 同步校验也未报告错误；记录
`.local/logs/transfer-range-realgame-validation-result.json`。同 PID 19469 的四窗
冷却对照已完成：18 秒以内的官方 LWI 窗口分别为 16.547、13.876、9.618、13.057
FPS。候选均值 14.802、参考 11.747，但不能将约 +26% 解释为纯代码收益。
每窗虽都在 Thermal Status 0、实际 HAL SKIN 约 39℃ 恢复，Perfetto GED 频率
轨道加权均值仍分别为 809、822、630、672 MHz；此最小 thermal 配置还缺少直接
`power/gpu_frequency` 事件。完整记录为
`.local/logs/transfer-range-hal-matched-game-official-comparison.json`。


## 分块检查成本与启动配置

官方 Streamline 符号报告、Android simpleperf 调用栈确认：达到 draw 配额以后，
旧 `stream_flush()` 在仍处于 Render Pass 内、根本无法提交时，每个 NVN 操作仍调用
`vk_replay_preserves_ops()`，反复查询冻结/外推环境开关。先判定 RP/query 安全范围，
再做保留操作证明，保持全部分块和查询语义。候选 APK 177c65fa… 的宿主/Mali/
Adreno 回归通过；与上一版只有此一处 CPU 检查顺序差异。

同 PID 23051 官方 LWI 四窗为 18.911、13.755、13.608、17.840 FPS；候选均值
18.375、参考 13.681，三项改动合计约 +34.31%。四窗同时采集的 Perfetto 直接
`gpufreq` 均为 848000 kHz，GED `gpu_freq`/`gpu_freq_max` 均为 848 MHz。
此组不再混入不同 GPU 频率，但 CPU 调度、温度、天气仍非严格控制；不是单独
检查顺序的 A/B，也未达到原生 30 FPS。每帧 GPU active cycles 约 -12.82%、
MCU 约 -48.69%，外部流量、ALU、LS 工作量基本相同，中断约 +38.74%。
证据 `.local/logs/stream-checkpoint-only-game-official-comparison.json` 和
`.local/logs/stream-checkpoint-only-window-frequency-summary.json`。

独立 simpleperf 原生调用栈中 `getenv` self 样本占比由 20.17% 降至 3.81%，
`stream_flush` children 占比由 18.64% 降至 2.30%；无丢样，但错误调用链约
42.6%/50.3%，仅采用已解析的后端调用链。CPU Perfetto 的录制区段均值由
39.28 ms 降至 27.32 ms，是两个独立时段的 CPU wall span，不能当独占 GPU
耗时或整个帧提速的因果分解。

仅调整检查顺序仍不彻底。当前进一步将启动环境配置集中为一次初始化的
`vk_startup_options`：布尔/数值直接缓存，路径复制到进程自有内存。绘制、冻结、
外推保留操作判断、附件覆盖/保存规划不再重复 `getenv`。运行中需要切换的策略
通过已有或明确的 atomic/request 接口设置；诊断帧请求保留提交入口快照与字符串
所有权，默认关闭时不加锁。测试同样用显式接口，不以每次读取环境来支持切换。
这部分还须以正式构建、真机回归及官方 CPU 工具核验。

## 顶点缓冲消费者继续细化

只读 VS texel buffer 通过原描述符解析到逻辑池范围，与 UBO/SSBO、顶点流及索引
使用同一生产区间表；未知/可写描述符和真正图像读取仍保守。compute/B2B/
UpdateUniform 三类生产者覆盖普通 SSBO、uniform texel 与 readonly storage texel
消费者，含索引、同批次/跨 NVN 提交及最终像素/CPU 字节回归。APK fc3d7e83…
通过宿主 543089、Mali 543870、Adreno 540791；实际营地 PID 25340 同步验证无
错误。storage 声明与游戏 binary 未改。

进一步为已知生产区间记录源阶段和已满足的 vertex 可见性。真正冲突时，已知
compute/transfer 写仅建立到 VERTEX_SHADER/VERTEX_INPUT 的读取依赖；不让这些
缓冲读取顺带等待无关片元/颜色/深度工作。全局内存依赖可批量覆盖相同来源的
先前写入，不逐资源插入屏障；新写入重新置为待可见，未知来源/表溢出保守完整
回退。图像可见性、片元消费者与显式 NVN 顺序请求不会被这项局部证明清除。
对应 [Khronos 的 compute→graphics 同步示例](https://github.khronos.org/Vulkan-Site/guide/latest/synchronization_examples.html)。


启动配置/已知顶点消费者 APK 25ca6757… 已通过宿主 543069（外部 Profile）/
543005（诊断与参考模式）、Mali 543850、Adreno 540771；真实营地 PID 28243
同步验证未报错。随后 PID 28992 的 simpleperf 937 样本、零丢样，没有 `getenv`
条目；53.3% 调用链未正确展开，不能据采样声称调用次数绝对为零。静态热路径
核对另见 `.local/logs/startup-options-hotpath-env-audit.json`。
单窗官方 LWI 为 18.418 FPS，原生分辨率不变；同次 Perfetto 直接 `gpufreq`
均值约 845 MHz。不是独立 A/B，不能宣称已得到额外整帧增益。

## 短卡、等待与持久 PSO 数据

原 `nvn_mtl.m` 已有 `pso_manifest.tsv`/`pso_user.tsv` 记录及后台预热，不能说
原项目完全没有 PSO 管理；没有找到显式 Metal binary archive 保存。
此前 Vulkan 有进程内键索引及 `VkPipelineCache`，但每次都空初始化，没有
跨运行保存。SPIR-V 离线翻译也不等于设备驱动的 PSO 已经编译。

新增持久驱动数据使用 Vulkan 原生 `vkGetPipelineCacheData`/`pInitialData`。
文件独立放在应用 `temp/vulkan-pso/`，不混入原游戏/正式 shader 资产。加载检查
格式版本、指针宽度、vendor/device/driver/API、pipelineCacheUUID、长度及 SHA256，
同时检查驱动数据的 Vulkan 头；损坏、不兼容、写入失败或驱动拒绝均退回内存
缓存，不影响游戏。文件采用临时文件、fsync、原子 rename。

后台线程合并创建高峰后取驱动快照并写盘；仅 PSO 创建和快照串行访问缓存，
文件 I/O 不占绘制线程、不等待 GPU。原后台存档和图形暂停完成后显式 checkpoint，
不在活跃帧里强制落盘。宿主用真实驱动导出/重新加载，并逐项破坏元数据、ABI、
驱动、UUID、摘要、载荷及截断文件，验证回退与恢复。待真机/实际游戏验收前，
不声称缓存命中或短卡已消除；首次新管线仍可能需要编译。

已有 Perfetto app 标记补上 PSO/module 创建、完成线程 fence、CPU guest fence、
CPU 翻译交接、上传等待 CPU 发布/GPU 访问、八份在途资源背压等区段。全部由
官方 ATrace/Perfetto 记录时钟；没有新增自制计时器/采样器。simpleperf 的 off-CPU
尝试已因缺少可访问 `sched:sched_switch` 事件失败，记录在
`.local/logs/three-directions-offcpu-result.json`；系统 trace 的 scheduler 事件仍可用。
标记用于区分实际阻塞原因，不能把队列到 fence 的延迟当作纯 GPU 执行时间。


## 全量阻塞入口复查：显示诊断误排空

完整启动 Perfetto `.local/profiles/pso-cache-cold-startup.pftrace` 中，翻译线程
`Vulkan CPU waits queue completion` 共 2263 次、CPU wall 合计 40.72 s；完成线程的
真实 fence 等待不是此项，资源上限背压标记没有出现。此处包含启动与营地，不能
把多个线程/嵌套区段相加，也不能仅据合计推断可提升多少 FPS。

沿调用链发现明确的额外排空：Android 壳总是设置单次诊断
`DM_VULKAN_FRAME_DUMP`，实际有 Surface 时 `mtl_present()` 也调用
`offscreen_present()`。后者把 `mtl_finish()` 放在 `!dumped && count >= at`
判断之外；即使文件早已生成，每次真实出图仍先等全部 GPU 完成，再提交 WSI blit。
这使独立完成 Worker 无法发挥作用。将排空移入真正一次性 CPU 读回分支，日常
呈现不再经过该等候，不改 guest fence、实际 readback、WSI semaphore 所有权。

回归用真实 timeline semaphore 阻挡 GPU，分别在截图阈值前及截图完成后调用
`mtl_present()`：应当在 GPU 尚未完成、CPU 副作用仍未发布时返回；真正的截图
帧仍检查所有 RGB 字节。用旧源码运行同一测试必须失败，再验证修正版。Adreno
没有 timeline 扩展时明确跳过 gate 分支，不把普通通过数量冒充该能力覆盖。

| 阻塞入口 | 语义与处理 |
|---|---|
| `completion_thread` 的 `vkWaitForFences` | 独立 Worker 的真实完成等待；保留 |
| `mtl_wait` / NVN SyncWait | 游戏显式 CPU 等 GPU；不能提前信号 |
| `vk_internal_wait` | 通用等待机制，逐一审计调用方，不能整个删除 |
| one-shot frame dump | 已移入实际读回分支，禁止在日常 present 排空 |
| CPU texture read / screenshot / instance expansion | CPU 真正消费结果，等实际生产批次；保留 |
| CPU pool upload | 区分先前 CPU 发布与 GPU 读写覆盖；目前保留冲突范围等待 |
| eight in-flight resource sets | 只等最老批次；本次工具没有观测到背压 |
| WSI acquire semaphore slot | 只有三个槽都未完成时等最老槽，防重复 signal；保留 |
| present texture ownership | 已提交即可，不等 GPU；既有 GPU gate 回归覆盖 |
| surface/swapchain 重建、后台挂起、显式 QueueFinish | 生命周期/显式排空，保留 |
| pool 删除 / texture shutdown | 仍有保守全队列等待；应按真实引用缩小，不能当作已经优化 |

上述是阻塞点的完整调用方审计，不声称已把所有保守等待移除。当前优先验证每帧
显示诊断的排空消失，再按工具给出的剩余临界路径推进分块和范围依赖优化。


第二处无条件等待已定位于实例属性展开。真机没有 vertex attribute divisor 扩展
（`max_vertex_divisor=0`）；旧 fallback 每次先 `vk_internal_commit` 并等待整个
此前队列，再扩展 CPU 属性，即使源数据从未被 GPU 写入。工具完整启动末 30 秒
的 replay 内部队列等待共 119 次、合计 6.59 s，上传的发布/访问冲突另有独立区段。

现在以 `baseInstance`、实例数、除数、stride、实际属性跨度计算所有读取范围，
先检查当前与在途 CPU 发布写入。全部是已知 CPU 数据时直接在当前提交的独立
暂存中展开，不切批次、不等 GPU；任一真实生产范围尚未发布时保留原同步 fallback。
检查必须发生在任何展开暂存分配之前，防止隐式提交后继续使用旧资源所有者。
不改变实例数、InstanceIndex/BaseInstance、最终像素或 guest fence 域。

补充真实 GPU gate 下的 CPU 属性回归：不相关 GPU 仍被阻挡时，NVN 翻译应能完成
该 instanced draw 的提交；随后解除 gate，再验全部像素。旧源码在同一测试失败，
修正版通过；已有 GPU UpdateUniform 修改属性的分支继续通过，证明未跳过真实依赖。
记录 `.local/logs/instance-source-red.txt` 与
`.local/logs/instance-source-host-validation.txt`，后者 543649 checks。

显示诊断修正版 f0c8889c… 已通过宿主 543648、Mali 544429、Adreno 540815；
真实营地 PID 32400 开启同步校验没有 VUID/SYNC 错误。实际游戏也确认从磁盘加载
1446633 字节驱动 cache；这是数据加载事实，不冒充 pipeline cache-hit 或 FPS 收益。
下一候选 29efcab7… 仅再增加实例源范围证明及对应测试，继续原生分辨率实测。


## 2026-10-17：缓冲上传改为队列依赖，消除 CPU 原地等 GPU

`29efcab7` 的原厂 Perfetto 捕获中，30 秒内有 496 次上传发布等待，平均
25.637ms，来自 `sync_page`：先等整页的在途 CPU 回写，再检查 CPU 数据是否变化。
先比较 guest/shadow（与完成线程的发布用短锁互斥；锁不跨 GPU 等待）可跳过无变化页。
但 `70b18f52` 对照进一步暴露同页不同字节的假冲突，以及直接写 GPU 在用镜像的等待。

`f86dd064` 保留完成 Worker，按实际变化的四字节连续段检查 CPU 发布依赖。
整页没有变化时直接返回；只有变化段与未发布结果真实相交才等 CPU 发布。
GPU 仍使用该页时，CPU 生成独立快照，在主命令缓冲前提交上传前缀，GPU 按队列依赖
复制变化段，避免 CPU 原地覆盖在用镜像。未变化邻居不能随整页上传，否则会覆盖先前
GPU 产生的新内容。无在途 GPU 使用时仍直接更新镜像。非对齐尾部、串行和全池参考
路径保留原同步后备。同一次 `vkQueueSubmit` 按先前缀、后主命令缓冲的次序提交两份命令，
不额外提交；其命令对象、快照均归当前在途资源所有，真实完成后回收。
前缀入口保守等待此前 GPU 读写到 TRANSFER，出口只以 TRANSFER 写为生产者，覆盖
后续实际绘制/计算消费者；不是把依赖直接删除。CPU 明确读结果及原 NVN SyncWait 仍等真实完成。

GPU 门控回归同时验证 CPU 提交可先返回、旧 GPU 读取仍见旧 CPU 字节、同页 GPU 写回
与后续新 CPU 字节均保留。旧上传路径稳定卡在 `CHECK(returned)`；新路径通过。
Host 544182、Mali 544963、Adreno650 540815、Adreno735 544187 checks 通过，均无
同步校验错误。650 不支持 timeline semaphore，门控部分明确跳过，不能称已覆盖。
原游戏同步验证也通过，记录 `queued-upload-realgame-validation-result.json`。

官方 `queued-upload-system-queue.pftrace`（PID11513）30 秒内，上传的 CPU 发布等待和
GPU 访问等待均为零；剩余翻译线程 GPU 等待 146 次全为八份资源上限的背压，平均
3.970ms、总579.639ms。不因此提高资源上限，也不伪造原游戏的完成栅栏。
旧采样精度缓存下本轮官方 LWI/Streamline 2..18秒平均21.2077FPS，非受控A/B，
不能当作当前正确精度缓存的帧率或已达30。CPU墙钟跨度有重叠，不能相加为GPU执行时间。

## 选角黑块：采样结果精度的确认回归

用户确认选角场景有大块黑色遮挡。回退全局同步、关闭提前提交和运行时 CPU 常量缓存
均不能消除。在相同 `f86dd064` APK、原游戏与资源下，仅关闭
`DMSH_VK_RELAX_SAMPLED_RESULTS`，缓存从 `2ee3b4b91974` 变为 `122643330366`，
角色、树枝和天空恢复，用户已目视确认。6931个模块中2397个逐字节相同，4534个仅
RelaxedPrecision装饰不同，全部元数据逐字节相同，无其它SPIR-V指令变化。
证据 `selection-sampled-precision-regression-proof.json`。该额外精度实验因明确画面错误
禁用，不因未涨帧而撤销；原上游 reduced 浮点规则、AST/常量优化、三项同步/提交/
异步完成改动继续保留。不能按游戏场景或着色器键添加特例绕过黑块。
