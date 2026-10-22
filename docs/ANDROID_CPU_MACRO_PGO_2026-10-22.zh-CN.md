# 翻译线程宏观预算与样本引导编译对照

本轮不新增逐 Draw 缓存。问题从“还有多少重复操作”转为三件事：必要工作能否采用更便宜的数据表示、合法绘制入口能否减少，以及必要代码的取指／访存／执行成本能否下降。

## 先限定可得预算

现有真实 Q735 数据中，NvN 每次呈现约有 27–28ms Running；到 16.7ms 需要约四成减少，但没有已证明可删除的同等规模重复工作块。

- 绑定整个父职责约占 21–24% 用户态样本，包含实际资源、依赖及寿命消费。旧版表示构造／Descriptor／set 交付的 7.55–8.99% 只是混合包络，仍含必要工作；假设能减少后者一半，条件算术也仅约 4%，不是已测收益。
- 当前 2312 Draw 记录中严格相同状态相邻组为零；仅放松索引偏移后的 174 个入口不是合法合批数，也不是线程可省比例。没有相应语义证明前不实现命令缓存或重排。
- 已保留完成通知的局部 CPU 减少，约 0.30–0.35→0.08–0.10ms；Store 记录数量减少约 95% 亦保留。二者不能解释整个约 10ms 缺口。常量父职责实测低于 0.8%，不继续当 CPU 大热点投入。
- 历史取指停顿比例不是当前版本预算；需要当前官方 PMU，而不是将采样符号份额变化解释为操作被删除。

完整来源和限制见 `ANDROID_TRANSLATOR_WHOLE_PATH_PRIORITIES_2026-10-21.zh-CN.md` 及其中的原官方数据引用。

## PGO 四轮已完成，没有证明整线程净收益

原入口构建的候选 `33d2d8a` 与父版 `f75de42` 仅编译策略不同，C/C++/ASM、Shader 与游戏规则不变。候选完整 `.text` 增加 54,696B，约 1.93%；AppliedSamples 表示编译器消费样本，不表示省下指令或 CPU 时间。

Q735 按父／候选／候选／父顺序执行。每轮原游戏三段移动、三份 30 秒官方 FP 记录、五份 8 秒 Perfetto、原存档两次、退出和恢复均完成，四轮控制器实际退出 0。相同 0.60 比例、2750×1260 输出、f830、原 950 Shader 和 full2838 资源；这些不是日常默认 0.75 的直接性能结果。

| 顺序 | NvN Running ms/Surface | Surface 平均 ms | NvN 睡眠 ms/Surface |
| --- | ---: | ---: | ---: |
| 父版 1 | 27.754 | 33.060 | 5.121 |
| 候选 1 | 28.060 | 33.388 | 5.166 |
| 候选 2 | 27.280 | 33.942 | 6.473 |
| 父版 2 | 27.891 | 33.417 | 5.349 |

表中是各轮按完整 Surface 数加权的描述值，全部 20 窗保留，不是独立重复实验的置信区间。候选 1 未降低 CPU；候选 2 较低 CPU 伴随更长出图间隔、更多睡眠及核心／工作量差异，不能归因为 PGO。位置、PSO、频率和核心分布不同，不能线性按 MHz 归一。父版 2 首书挡的 PSO 174→181、variants 31→33 没有剔除。

12 份记录均无报告丢失／截断，12 次官方时钟重叠校验通过。父版 2 第二份记录有一条真实 `pthread_cond_broadcast→__pthread_start→__start_thread` 栈缺少 `x_thread`，原样保留；其余记录该祖先完整，不能补造缺帧。根复核 951 个原始／工具／分析文件的哈希。

决定：保留可选构建、来源和内容正确性资格；不将 PGO 默认启用，不称已经提速，也不推断 PGO 在其他场景绝对无效。四轮都恢复到已实际交付的 a322 日常共享内存／原生纹理组合；实际设备上的私有启动配置和收条哈希未变。

本机证据：`.local/logs/host-sample-profile-33d2d8a-pair-analysis-v1/{comparison-ready.json,comparison-all20-windows.json,summary-all4-rounds.md}`、`.local/logs/host-sample-profile-33d2d8a-ABBA-root-review.json`、`.local/logs/host-sample-profile-33d2d8a-post-ABBA-private-files.json`。

## 修改前的预算与优先级

当前不是已经找到约 10ms、只等删除的重复工作。27–28ms Running 到 16.7ms 需要减少约四成；单个叶节点或重复绑定次数不能解释这个差额。阻塞锁不消耗 Running，因而不能将这 27–28ms 大头解释为阻塞锁等待；自旋等消耗 CPU 的行为仍须看实际调用栈，不能仅凭 Running 排除。

优先研究的是改变必要工作的处理粒度和表示，而不是继续叠缓存：

| 方向 | 现有预算与限制 | 实施顺序 |
| --- | --- | --- |
| 将稳定资源的准备归到整个 Pass，逐 Draw 只处理真正变化和实际依赖 | 依赖／history 整段约 4.96–5.34%；扣除已有 range／stage／generation 早退后，剩余 request 入列和 history 外层 self 混合包络仅 1.03–1.28%，仍含必要工作，未扣新增版本／成员／寿命检查，净下界为零。 | 审计后降为低优先级，不实现新 Pass 缓存；不能拿静态重复次数放大预算。 |
| 长寿命资源表示与逐 Draw 动态参数分离 | 表示构造／Descriptor／set 混合包络约 7.55–8.99%。即使能省一半，条件算术约 4%，尚非预测。已有 schema、dynamic UBO、recipe、source_flow 与 history 不能重复计算收益；还须扣新增参数上传、GPU 访存及失效成本。 | 比单个查找缓存优先，必须先找出现有表示仍迫使重建整段数据的实际输入。 |
| 缩小必要代码与数据工作集 | 当前 PMU 已确认前端／取指成本存在，但 L1I 相关 17.405% 不是可回收预算；PGO 已有四轮无一致净收益，LLD 也已消费调用图排序，不能再假设这些能力未开启。 | 按完整 draw 路径审视热路径代码和数据访问形态，不再盲加编译开关。 |
| 附件不变元数据在仍活动的 Pass 内复用 | 较明确的视图／描述构造站点约 0.47–0.90% 用户态样本，较宽且含必要工作的乐观包络 1.03–1.63%；新增校验及回退后净收益未知。 | 小预算候选，不能作为解决十毫秒差额的主线。 |

最后一项的约 5.6–6.5% 附件父职责大部分是必须执行的布局、访问和依赖处理，不能全部删除。新 Pass 也不能复用旧 Pass 的一次性 CLEAR／DONT_CARE 描述。现有对象缓存已避免重复创建 RenderPass／Framebuffer／ImageView，候选不再重建相同缓存。详细合同与三个原 FP 窗的预算在 `.local/logs/f75-active-pass-metadata-readonly-v1/contract-and-findings.json`。

实际旧 Q 帧有 2,312 Draw／50 Pass；静态采样声明出现 10,701 次，按 Pass 和精确图像／范围／阶段／布局归类为 609 个组合。这仅说明资源身份具有复用结构，不是实际 Shader 读取次数，也不是可跳过 94% CPU 工作的证据。必须排除已做的早退、CPU 并发写、别名、反馈及实际 GPU 写入；不能按这些计数开写大改造。严格相同状态相邻组为零也意味着不能把通用合批作为现成大收益。

只读 Pass 审计最终证据为 `.local/logs/f75-pass-image-read-scope-readonly-v1/{ready.json,contract-and-budget.json,findings.txt}`。其中 254 项输入／源码／官方工具输出均已核对哈希；旧帧不能当当前场景命中率。仅 1,208 次静态声明能确认对应 host-imported 图像，其余不能直接认定 GPU-only。读取入列时已覆盖也不能无条件删除：同一 Draw 后续 writer 可能清理旧 reader，当前实际读取仍要正确恢复 WAR 依赖。

## 当前 PMU 与大块内存来源

另一个原 f75 游戏事务已实际采集基础、core、memory、front-cache、front-stalls 五组官方 Simpleperf 计数，以及独立 cpu-clock、raw L1I refill 和 Perfetto。控制器实际退出 0，原存档／停止／恢复完成。基础 stat 窗 PSO 172→179，后续记录／硬件计数／scheduler 的 bracket 为 179；分析保留这一差异，并按原输出核对事件支持、每核心有效计数及 multiplex/scaling；不能把 cache-miss 或 bus-cycles 当实际 DRAM 字节。采集源为 `.local/logs/f75-current-PMU-a322-baseline-v1/game/`。

完整官方分析现已完成，根复核 116 项原始／工具／分析文件。core 窗的 NvN 用户周期为 9.092B、退休指令 18.568B、IPC 2.042，前端停顿约 26.875%、后端约 23.553%。独立 front-stalls 窗前端约 26.492%、L1I 相关约 17.405%；事件可能重叠，不能相加或当成可回收时间。front-cache 窗 L1I access 4.811B、refill 556.719M，比值约 11.571%。memory 窗 generic cache miss/ref 约 4.915%，没有独立 L2／DRAM 字节计数，未核 OEM 的全部事件语义，不能据此宣称已经证明带宽瓶颈。

四组 verbose PMU 的实际每核心原始计数逐事件相加与报告一致，聚合 running/enabled 约 100%；保留异构核心差异及原工具的 childcmd 拒绝日志，不能据最终有数就省略限制。正常 DWARF 栈 634 个 NvN 样本中 495 个保留 `x_thread`，独立 raw refill 栈为 547 中 455 个；无丢失／截断不代表所有父链都完整。raw refill 的 self PC 约 76.23% 在 host、15.90% 在驱动；它们不是整个 L1I 停顿成本的归属证明。独立 scheduler 窗 NvN Running 27.971ms/完整 Surface，不能用该帧数去除先前独立硬件计数。

这将代码／数据工作集及必要代码的执行形态提升为当前研究重点；没有新增可省 17% 的承诺，也不因 PGO 第一种实现未见净收益就将取指问题排除。证据为 `.local/logs/f75-current-PMU-a322-analysis-v1/{ready.json,facts-and-priority.json,PMU-counts-and-coverage.json,summary.txt}`。

内存调查继续针对同停点 SDK 快照中 NVN Pool 外两块大分配，250MiB 与 260MiB 的 CPU-view present 并集合计约 382MiB。这不是可释放或独占物理 RAM 的证明。原 SDK 分配来源调试首轮在 native 启动前因 `adb jdwp` 等待 EOF 超时，未进入 LLDB/JDB；原失败保留，确认未启动 native 后退出并恢复，无存档成功宣称。工具后继 `a31c333` 仅有界消费原 ADB 输出的精确完整 PID 行，保留真实 stdout/stderr/EOF/退出状态并清理自己的本地 reader；原 SDK 244B、断点／读预算、官方 JDB/LLDB、后路快照及存档恢复不变。其 host 检查和离线资格不代表已经取得分配调用栈。

本轮没有新的持续散热或 6GB 稳定游玩验收，目标仍未完成。Mali 与 6GB 平板未访问。

## 后续实施：缩小绘制热函数，而非增加缓存

对精确 f75 的官方 LLVM inline ranges 继续审查，主绘制函数 22,356B 中，BQ 匹配约 4,696B、PL 准备约 1,428B、PL 消费约 864B、实例展开正分支约 1,304B，另有约 228B trace 正体。这约 8.5KiB 是包含共享分支及错误路径的静态范围，不是可省 CPU 百分比。现有独立 `pl_plan`、管线创建及早先冷阶段外提不重复认领。

独立 `codex/draw-cold-positive-phases-v1` 以已构建的 930 修复版为基线，实施 BQ、实例展开和 PL 成功路径的完整阶段外提。普通／禁用绘制的拒绝判断仍留在热路径；不增加 Draw 数据副本、资源缓存、分配或跳过依赖。原字段与参数直接传入独立函数，实例展开提前返回时仍由原调用者完成 operation/local-ready 清理。

同 Clang 19.1.7、原 host O1/no-LTO 的实际 x86-64 机器码显示：主 Draw 23,723→15,041B，减少 8,682B；四个独立函数合计 9,099B，合并计算总代码反而增加 417B。主函数栈仅减少 16B。机制是缩小普通绘制路径所混入的代码体积，不是删除真实绘制计算；完整正分支新增函数调用，最坏总栈深可能上升。这些不是 Android AArch64 数值，尚无 CPU/FPS 或散热收益证明。原内容／GPU／sanitizer 资格正在独立进行，结果及命令位于 `.local/logs/draw-cold-positive-phases-v1/`；静态合同在 `.local/logs/f75-draw-cold-stages-readonly-v1/`。

## 内存归属：从全分配器断点转为少量已证记录

原 main 的静态调用链已确定三个 TLSF 子分配器的 backing 记录：250MiB 的 pointer/size 位于 `main+0x31162e8/+0x31162f0`；260MiB 的记录位于 `+0x31dafb0/+0x31dafb8`，原名称为 `Popcorn Asset Pool`；另有 40MiB `Popcorn Scratch Pool`。分配参数、返回值存储、TLSF 初始化及两个原名称的关联均有原始指令／字符串证据，不能再将其仅描述为按尺寸猜测。

但历史 SDK 快照没有读取这些全局，尚未证明其指针就是先前的两个大 allocated span。下一次只在原 SDK/Pool 同停点补读这些固定记录并归一 backing offset，额外读取不超过 850B；原始指令、930 正式 ELF/DWARF 和本次前后记录均须核验。不会重新布置高频 Allocate 断点，也不遍历 TLSF。大 arena 是子分配器 backing，不意味着其中所有子块有效，更不意味着可直接释放；后续归属成功后才调查内部利用率。静态证据和待实施捕获合同位于 `.local/logs/sdk-large-nonpool-origin-static-v2/`。

另一项预算修正：在本次 f75 PMU 日志实际出现的 110 个 Shader key 中，旧 950 库的 33 个非空 DPL 文件总计 3,326,480B，新 8b 库对应文件为 13,912,752B。它们仍只是所有这些 key 均实际请求 lazy recipe 时的 blob 上界，不是已观测常驻量／PSS；新库全库存约 256MiB 的磁盘增长不能直接写成实际多驻留 256MiB。新 subset 模块、元数据和驱动分配仍须单独验证，当前未部署新库。原文件哈希与统计在 `.local/logs/f75-loaded-shader-DPL-residency-budget-v1/facts.json`。

## 共享 B2T 稳定性修复已通过一次原游戏功能验证

V5 恢复期间的实际 f75 崩溃位于 B2T 调用的 `image_upload` 源预检保护。原 GPU 夹具能通过真实 clean 查询后发生真实同值 CPU 写，复现相同 FATAL；窄修 `930ac8f` 只要求 B2T 在借用物理图像前完成既有源可见性准备，保护条件及普通采样 clean 快速路径不变。八项原 GPU 内容／ASan 用例通过，旧生产代码在相同最终夹具上仍失败；真实最初崩溃的具体纹理 serial 和暂停因果仍未知。

930 已由原 Android 入口构建，并从三个未修改 NSO 重新生成、独立校验全部 28 个交付文件，游戏可执行字节与 a322 相同。Q735 原游戏 PID 3782 已完成三段真实输入跑动、营地画面查看、原生命周期存档两次和退出，控制器终态 0、原日常 a322 APK／配置／资源恢复，cleanup 为空。此轮仅验证正确性与系统内存报告，没有 CPU/GPU 计时采集，不宣称提升帧率、持续散热或 6GB 稳定运行。证据为 `.local/logs/shared-b2t-ordered-preflight-930ac8f-root-actual-functional-review.json`；分支 `codex/shared-b2t-ordered-preflight-v1` 已备份到 fork，当前日常包没有被静默替换。
