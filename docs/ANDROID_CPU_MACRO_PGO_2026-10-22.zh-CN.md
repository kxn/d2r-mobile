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

## 当前 PMU 与大块内存来源

另一个原 f75 游戏事务已实际采集基础、core、memory、front-cache、front-stalls 五组官方 Simpleperf 计数，以及独立 cpu-clock、raw L1I refill 和 Perfetto。控制器实际退出 0，原存档／停止／恢复完成。基础 stat 窗 PSO 172→179，后续记录／硬件计数／scheduler 的 bracket 为 179；分析保留这一差异，并按原输出核对事件支持、每核心有效计数及 multiplex/scaling；不能把 cache-miss 或 bus-cycles 当实际 DRAM 字节。采集源为 `.local/logs/f75-current-PMU-a322-baseline-v1/game/`。

完整官方分析现已完成，根复核 116 项原始／工具／分析文件。core 窗的 NvN 用户周期为 9.092B、退休指令 18.568B、IPC 2.042，前端停顿约 26.875%、后端约 23.553%。独立 front-stalls 窗前端约 26.492%、L1I 相关约 17.405%；事件可能重叠，不能相加或当成可回收时间。front-cache 窗 L1I access 4.811B、refill 556.719M，比值约 11.571%。memory 窗 generic cache miss/ref 约 4.915%，没有独立 L2／DRAM 字节计数，未核 OEM 的全部事件语义，不能据此宣称已经证明带宽瓶颈。

四组 verbose PMU 的实际每核心原始计数逐事件相加与报告一致，聚合 running/enabled 约 100%；保留异构核心差异及原工具的 childcmd 拒绝日志，不能据最终有数就省略限制。正常 DWARF 栈 634 个 NvN 样本中 495 个保留 `x_thread`，独立 raw refill 栈为 547 中 455 个；无丢失／截断不代表所有父链都完整。raw refill 的 self PC 约 76.23% 在 host、15.90% 在驱动；它们不是整个 L1I 停顿成本的归属证明。独立 scheduler 窗 NvN Running 27.971ms/完整 Surface，不能用该帧数去除先前独立硬件计数。

这将代码／数据工作集及必要代码的执行形态提升为当前研究重点；没有新增可省 17% 的承诺，也不因 PGO 第一种实现未见净收益就将取指问题排除。证据为 `.local/logs/f75-current-PMU-a322-analysis-v1/{ready.json,facts-and-priority.json,PMU-counts-and-coverage.json,summary.txt}`。

内存调查继续针对同停点 SDK 快照中 NVN Pool 外两块大分配，250MiB 与 260MiB 的 CPU-view present 并集合计约 382MiB。这不是可释放或独占物理 RAM 的证明。原 SDK 分配来源调试首轮在 native 启动前因 `adb jdwp` 等待 EOF 超时，未进入 LLDB/JDB；原失败保留，确认未启动 native 后退出并恢复，无存档成功宣称。工具后继 `a31c333` 仅有界消费原 ADB 输出的精确完整 PID 行，保留真实 stdout/stderr/EOF/退出状态并清理自己的本地 reader；原 SDK 244B、断点／读预算、官方 JDB/LLDB、后路快照及存档恢复不变。其 host 检查和离线资格不代表已经取得分配调用栈。

本轮没有新的持续散热或 6GB 稳定游玩验收，目标仍未完成。Mali 与 6GB 平板未访问。
