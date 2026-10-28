# 高通 GPU 阶段与访存：19项原厂指标及后续实验

本轮实际采集把主线进一步收敛到 Fragment Shader 的依赖读取与调度。没有取得新的游戏提速结果，也没有把编译探针当成游戏运行。CPU 计算减负继续停止；CPU只检查锁、线程交接和同步等待。

## 当前真机与测量范围

Q735（10CG570J59001U1），正常 c09 APK、b828 host、cf83 Shader、12b8 Turnip、原BC3共享资源；原游戏 ARM64 模块正常执行。游戏和交换链1968×900，主3D1673×765、比例0.85；营地5225,6289，controlled22角色。没有更换APK、driver、游戏资源或模块。

使用已有 Snapdragon Profiler 实时采集及同窗 Perfetto。保留原14项，仅增加总线忙碌、ALU容量利用和FS/VS/CS阶段时间5项。19项先在本机同一新会话共同输出并冻结，再采一次8秒窗口，得到400个完整Surface区间。不是将不同窗口的指标拼在一起。原厂指标为global，包含合成器等其他工作。

| 同窗指标 | 实测 |
|---|---:|
| 工具活动窗口呈现FPS | 50.425 |
| GPU主批次，396个有明确API归属的SOP/EOP区间均值 | 19.586ms |
| 呈现复制批次均值 | 0.191ms |
| 应用context的SOP/EOP内部覆盖 | 99.769% |
| FS / VS / Compute，占全部shading时间 | 81.095% / 13.229% / 5.676% |
| GPU到系统内存总线忙碌率 | 23.921% |
| Shader工作周期内的ALU容量利用率 | 33.912% |
| Shader stalled / Texture fetch stall / System-memory stall | 16.655% / 12.196% / 0.418% |
| L1 hit / Texture L2 miss | 63.709% / 4.358% |
| Texture pipes busy / Wave context occupancy | 53.057% / 58.036% |
| 总读 / 纹理读 / 总写，积分到同Surface窗口 | 104.787 / 85.692 / 44.067 MB/Surface |
| 原厂GPU平均频率gauge | 499.310 MHz |

阶段份额在155个共同timestamp逐点和为100，表示shading内部构成，不能乘19.586ms宣称FS独占15.9ms或CS独占1.1ms。各stall的分母不同且可重叠，也不能相加凑等待时间。SOP/EOP包含内部停顿，不等同ALU busy；统计内部跨度前另有75.689ms未知头部，未补成已知。

Bus Busy的定义是GPU通往system-memory的bus近似忙比例，不是整颗SoC的DRAM控制器利用率。因此本窗不支持“GPU持续以峰值bulk吞吐打满总线”或“ALU峰值吞吐用满”的简单结论；仍不能排除依赖读取、片上cache/加载管线限制、QoS或其他master竞争。低L2 miss比例不等于读取延迟低。

GPU Average Frequency与Clocks/s÷1e6来自同provider，旧三臂逐timestamp值完全相同；它们不是两份独立物理频率验证，不能据此补出OPP上限/温控原因。本轮未自行实现频率采样。

同窗owner Running16.188ms/Surface，全应用多线程Running总和30.083ms/Surface。后者不是单帧关键路径。历史同c09九指标窗约50.191FPS，但其取图周期60Hz，本窗实际90Hz（target仍60），工具counter集合也不同；两窗仅作宏观诊断，不能宣传0.47%为净优化收益。原统一比较保留这些资格失败，不用忽略差异后的数字认领加速。

## 先核真正可消除的 GPU 工作

另外完成独立8PSO编译探针：普通UID10344、精确12b8 ELF和真实API属性 `minStorageBufferOffsetAlignment=4`，四个original native hash全部复现当前游戏。无draw、dispatch、queue submit或计时；探针已停止卸载，游戏锚点前后相同。

宿主storage-bias词初始化为零，唯一非零setter要求 `prefix=off%alignment != 0` 且 `off%4==0`；alignment4时两者不可能同时成立。共享窗口只改变off，不改变这个证明；typed texel handle不使用这些词。对已认证的bias槽消除未知值后，确实让驱动看出一些相邻只读加载：

| CS | native readonly isam.v条数，原→零bias | 发生位置 |
|---|---:|---|
| depth bounds | 3→2 | preamble |
| light grid | 8→3 | preamble |
| light bin | 9→8 | preamble；主体2条与14条counter读取未变 |
| LUT combine | 19→9 | preamble |

有效scalar分量数、robust和原内存访问合同保留，四者full寄存器数均未变。preamble不按每个逻辑invocation重复执行，不能把这些差值乘dispatch线程数推DRAM/FPS。该机制有实际减少读取发射的编译证据，保留实验；CS仅占shading约5.7%，当前不为它安排整轮游戏测试。

同一零bias合同也适用于两热点FS，但各自的5个SSBO load分散在主体和4个loop，16个typed Buffer fetch不受影响；没有理由把CS的合宽结果直接移算给FS。

## 按主路径并行推进的两项实验

1. **关键读取链的调度。** 两热点FS的原生代码先发3/4个独立材质采样，再发负责后续 `height → UBO → UBO` 地址链的采样；首次 `(sy)` 使用后者结果，却等待此前outstanding批次。原驱动调度源码确有尽量延后sy消费者、先发更多texture的策略。实验按数据DAG的后继读取依赖优先推进关键生产者/消费者，保留所有hazard边和无此链时的原策略，不能简单把outstanding上限8改3。需核原生序列、额外等待、GPR和spill后才进入游戏对照；目前没有证明首次sy最慢的是哪条请求，也不保证19个full降到16。
2. **先选word，再读取。** 两热点FS主路径显式读取同一UBO vec4的四个word，再用2-bit互斥掩码只选一个。选择公式对任意uint32载荷已通过Z3验证；只读所选word可保持位型语义并缩小逻辑读取集合。但Adreno现有lowering会将动态component标量重新扩成vec4，实验必须在真实native上保留窄读取，并核新增分歧/发射/等待，不能仅删AND/OR后声称减少访存。

两项均走独立原生编译对照，保持原游戏与生产Shader链。通过编译机制资格后，再用同尺寸、同周期、同场景和共同官方指标核游戏净成本。没有把静态指令减少或局部编译通过记成已涨帧。

## 保存与证据

游戏按原Android生命周期新提交2次存档后停止；本次入口22份用户文件逐项恢复，未冷启动恢复后的用户存档；原APK、profile、Shader、driver、table、模块及UI偏好保持。本次GUI/Xvfb和13个collector均已核实退出。清理原记录保留了一次进程退出时身份查询竞态；独立末次核查确认全部已退出，没有覆盖失败记录。

- 原始窗口及统一归约：`.local/logs/gpu-memory-counter-baseline-v1/`；完整阶段/分母核查在`whole-profile/`，硬件区间在`hardware-analysis/`，恢复证明`final-restoration.json`。
- trace SHA-256：`74e86a24d89e8c99a0a4aedc8f1d1b75ebe9a7ec1a911fdf1bebce34fe2cc5ba`；SDP数据库：`0676de8c84e68fa7f94bb6466c798933ab7d1b429e7eed6b07877f6ab636d6cb`。
- 精确8PSO编译：`.local/logs/current-cs-memory-next-review-v1/zero-bias-candidate/ACTUAL-COMPILE1/`及同级`ACTUAL-COMPILE1-analysis/`。
- 两条FS主路径依据：`.local/logs/fs-main-memory-schedule-audit-v1/`。编译候选和后续driver构建各自独立，不重标旧payload或旧native结果。
