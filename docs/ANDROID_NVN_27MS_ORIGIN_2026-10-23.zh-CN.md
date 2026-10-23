# 27ms 的来源：原始调度与提交周期复核

结论：受控营地测试中的约27ms，确实是翻译线程每个渲染提交周期消耗的 CPU 在核运行时间。它不是 GPU 时间，不包含大部分 Present 睡眠，也不是用错 Surface 分母造成的约两倍误差。它仍不能称为单帧从生成到显示的延迟。

本次仅查询既有官方 Perfetto trace，没有改运行时、启动游戏或新增计时器。源码 HEAD 为 `28a2678`；最终候选运行时为 exact10d。范围是最近 PARENT2 / FINAL1 / FINAL2 / PARENT3 四轮各五窗，共20窗；固定0.60、external-profile/noADPF，不能外推为日常0.75/adaptive设置或所有场景的结果。

## 原始公式与独立分母

FINAL1 record-1 原始窗口：

- Surface 首尾时间：710369130095361 至 710376871071035 ns，跨度7740.975674ms。
- 对应应用 SurfaceView 的四个 Buffer track，共232个不同 PresentFenceSignaled 时间点、231个完整间隔；没有把四个 Buffer 当成四倍帧数。
- PID572 / TID670（nvn-translate），`sched_slice` 与上述窗口相交后累计6222.799483ms。
- `6222.799483 / 231 = 26.938526ms`。只有此线程参与分子，未加主线程、GPU完成线程或其他应用线程；该线程的调度片段没有相互重叠。

独立验证不以 Surface 为周期：取相邻两次 `NVN command replay` 开始时间，累计之间的同线程调度时间。上述窗口229个完整周期平均26.899876ms；每周期均为一次游戏 QueueSubmit、一次 Present、八次内部 Vulkan Submit。内部 Submit 是同一周期的分块提交，不能算八帧。

全部20窗共4611个候选周期，所有周期均符合上述1/1/8计数。进一步检查调度状态完整性后，4610个周期完整覆盖；PARENT3 off-before 的第一个周期有11.070ms未知状态，保留原始结果但不纳入完整周期均值。20窗独立周期均值与原 Surface 均值的最大差为0.115987ms。最终10d的两个运行共2296个完整周期，平均26.576914ms；其原2316个 Surface 间隔的加权结果为26.559060ms。

这排除了本批数据中“60次翻译被30次显示除，导致27ms其实应除二”的解释。时间顺序与代码的 FIFO 执行一致，但不将计数对应冒充逐帧 GPU 硬件执行时间或每个图像的端到端身份追踪。

## 26.559ms 的互斥 CPU 分解

下表为最终10d两轮十窗的调度 CPU 累计除以同窗完整 Surface 数。每个已有 trace marker 与 `sched_slice` 求交，再扣除直接子 marker，避免 planning、submit 等嵌套重复计费。表中数值不是将30秒 Simpleperf 比例乘另一个8秒窗口。

| 范围 | CPU ms / Surface |
| --- | ---: |
| NVN 回放主体，扣除以下已标记子范围 | 22.899256 |
| 提交规划，扣除 Store index finalization | 1.456776 |
| Store index finalization | 0.149020 |
| Vulkan queue submit，含回放内和回放外的真实调用 | 1.043989 |
| Vulkan present call 内的在核执行 | 0.650000 |
| Swapchain acquire 内的在核执行 | 0.091505 |
| Submit queue lock 范围内的在核执行 | 0.015718 |
| Presentation feedback | 0.006532 |
| Present queue lock 范围内的在核执行 | 0.001546 |
| 未被上述 marker 覆盖的线程在核执行 | 0.244719 |
| 合计 | **26.559060** |

回放主体仍包含 draw、状态、资源、依赖、copy/compute 等实际执行工作。没有足够细的既有时间 marker 把22.899ms继续精确分成这些子项；可使用官方调用栈的职责比例辅助归因，不能把比例伪装为这张表的直接计时。

同一窗口口径，线程状态平均为 Running26.5591ms、S6.7394ms、R/R+0.2716ms、D0.0101ms；另有下述少量边界未知状态。Present call 内的 S 为6.7183ms，几乎覆盖全部 S。不能再把这部分睡眠加进27ms，也不能把 Present 的整段墙钟时间当成 GPU 执行耗时。CPU时间是在核调度时间，含用户/内核执行；没有在此测量中进一步拆用户态、内核和中断成本。

## 边界问题与完整性

20窗均只有一个具有 replay 的翻译线程；Running 片段无重叠，`sched_slice` 与 `thread_state` Running 累计相同。互斥 marker CPU 与未标记 CPU 之和逐窗还原原分子；没有负的互斥时间或子 marker 越出父范围。

严格要求“所有线程状态之和等于整个 Surface 窗口”时，发现三处 trace 开头的未知状态，原查询以 `dur>0` 排除它们：

| 窗口 | 未知边界总量 ms | 即使全为 CPU，对原均值的最大影响 ms |
| --- | ---: | ---: |
| PARENT2 off-after | 9.750051 | 0.042026 |
| FINAL1 record-2 | 8.842814 | 0.038116 |
| PARENT3 off-before | 11.904428 | 0.050657 |

不能将 `dur=-1` 的初始状态擅自延伸、填成 Running 或睡眠。除上述父版一个周期外，独立回放周期全部有完整状态覆盖。最终10d的2296个周期均完整，因此26.576914ms不依赖这些未知边界。早先原始4611周期未做这项资格剔除时的最大分母差0.091881ms仍在原始数据中；完整资格后的0.115987ms是本报告采用的最终结果。

Surface Queue/AcquireFenceSignaled/PresentFenceSignaled 在所有窗口计数均与完整间隔数相同；FINAL2 record-3 的 Latch 多一个，保留为相位/边界计数差异，不据此声称逐帧绝对无丢弃。没有观察到稳态2:1翻译/呈现或持续积压的计数证据。

## 代码语义对应及限制

- `hle_nvn.c:x_thread/x_run/x_submit` 按同一 FIFO 串行处理提交、呈现和 FRAME 工作。
- `nvn_vk_plan.c:mtl_sub_index` 开始 replay 并包含 submission planning；`mtl_exec_rest(NULL,0)` 结束 replay。
- 因而 replay marker 不包含随后最终 `mtl_sub_commit` 和整个 XJ_PRESENT；不能把 replay 的 CPU 时间当成整个翻译线程时间。
- `nvn_vk_present_worker.inc:window_present_immediate` 的 Present marker 包围真实 `vkQueuePresentKHR`。本受控配置未使用独立 present worker，调用落在翻译线程。

这次确认的是测量来源及宏观责任，不证明为何现有代码必须消耗23ms回放主体，也不证明某个待优化算法的可省时间。指令数、执行频率、每周期命令数量的因果比较仍需另行对照，不能由此推断此前优化被 DVFS 全部抵消。

既有 trace 已足以回答这次分母和 CPU/等待混淆问题，因此没有重新采集或更改已交付版本。其他优化保持暂停。

## 本机复现

目录：`.local/logs/nvn-27ms-origin-audit-v1/`。

- `run_existing.py`：复用原查询的应用/Surface边界，调用官方 `trace_processor query` 分析20份原trace；每份原trace SHA已核对。
- `detail-tail.sql`、每窗 `audit.sql/output.csv/result.json`：调度交集、互斥 marker、独立周期、源提交/Present计数及全部周期数据。
- `edge.sql/edge.csv`、`edge-cycle-qualification.sql/edge-cycle-qualification.csv`：未知状态的原始证据及父版唯一不完整周期。
- `all20.json`：未筛选原始结果；`conclusion.json`：逐窗独立分母、完整性资格、边界上限及加权分解。
- `summarize.py`：读取官方输出并检查会计恒等式，不新增计时或采样。
- `manifest.json`：分析文件与原trace引用指纹。
