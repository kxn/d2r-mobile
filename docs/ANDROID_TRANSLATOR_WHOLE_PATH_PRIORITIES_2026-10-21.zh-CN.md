# 翻译线程：按整条调用链安排优化

固定绑定 schema、图像请求阶段边界和首次 walk 融合 store index 已完成并做了真机对照，尚未证明整帧 CPU 或 FPS 净收益。下一步先验证官方 PMU 指出的取指开销，再扩大已经验证的 ASTC6 原生资源路径。已完成共享路径中的旧 Pool 扫描、比较、镜像上传计数为零；不能继续把该路径的 CPU 时间归因于已经消失的拷贝。现有证据也不支持阻塞锁是主要原因。

## 已有官方数据的结论

8480a53、同一 6217 资源、固定 0.60 比例、2750×1260 输出。原 Simpleperf 8 秒记录保留 605 个翻译线程用户态样本，完整保留实际展开结果，没有补造缺失的祖先。按可见调用路径将每个样本只分入一类：

| 职责 | 用户态样本比例 | 当前处理 |
| --- | ---: | --- |
| 描述符绑定构造和真实资源消费 | 23.14% | 固定 schema 提前整理；进一步研究表示复用和实时访问效果分离 |
| 操作资源准入 | 9.42% | 保留生命周期 pin；不把少数重复边界判断当成整项节省 |
| CPU 图像来源准备 | 4.96% | 已有操作内来源传递及 id/epoch 复用，继续复用现有机制 |
| 物理图像准备 | 2.31% | 已有 recipe 和 trivial 提前返回，不另造重复缓存 |
| 绘制后图像访问历史 | 4.30% | 消费追加时已知的读写阶段边界，减少重复过滤 |

以上五类并集为 44.13%，包含必须保留的访问、同步、引用和布局语义，**不是可删除的 44.13%**。提交规划 `mtl_sub_index` 另占约 5.95%，也包含必须执行的工作。驱动、memcpy、锁等底层占比已经包含在调用者中，不能再相加。

另一个 Perfetto 窗口里，同一 234 个完整 Surface 间隔中，整条翻译线程 Running 平均约 27.78ms，replay 内 Running 约 26.64ms。replay 内 Sleep 约 0.96ms/间隔；已标记的队列锁总区间约 0.014ms/间隔。后者不能证明所有资源锁均无竞争，但不支持把剩余二十多毫秒解释成主要在睡眠等锁。不同工具窗口不相乘来伪造精确的子任务毫秒。

## 优先级与成立条件

1. **绑定表示与实际资源消费分离。** 原 Metal 可以依据 stage dirty、shader 和 pass 复用 argument block；Vulkan 当前在最终描述符缓存命中前仍经过完整资源消费和 values 构造。先将固定类型、寄存器、源偏移和动态槽位整理到每个最终 shader schema。只缓存固定元数据，不缓存 GPU ready、布局或脏页结论；当前实际状态仍逐次读取。该第一步不减少绑定数量或实际资源消费次数，不能声称消除了整个 23.14%。
2. **操作内的结果直接交给下一阶段。** 图像请求追加时维护“只读前缀、写入后缀”是否成立，成立时绘制后历史遍历从 `3N` 降到 `N+W`；交错请求保持原三遍算法，所有实际历史更新及原顺序保留。新增总计 128B 批次元数据，单请求大小不变。这一步与既有来源/物理 recipe 复用配合，不添加跨绘制的动态依赖预测。
3. **合并提交规划的整次遍历。** 研究将现有 forward store index 融入原 walk，保留 reverse 深度/查询分析、别名判断及原失败行为。不能按命令 handle 跨提交缓存：handle 是可重录的内存地址，嵌套 CALL/JUMP 目标同样可能变化，缺少完整 generation 证明。
4. **低覆盖叶子优化后置。** 独立的重复边界判断删除只明确覆盖 2/605 个样本，约 0.33%。原理成立可以保留候选，但不抢占以上任务的构建和设备验证。

## 实施与证据边界

固定 schema 候选 `d4e22c9` 与图像阶段候选 `d31e931` 已源审，并组成 `b16184c`。组合桌面 Vulkan 检查通过，包含串行/异步、真实资源内容、重入、替代 schema 和图像升级路径；这不是 Android 游戏验收。Android canonical 和原始 NSO 经原工具 fresh rewrite/verify28 正在单独构建，之后才进行真机同资源、同比例的官方工具对照。

新的 8480a53 原游戏运行已完成三段原输入路径、官方 Simpleperf stat/record、两个独立 PMU stat 窗口和 Perfetto，已存档、退出并恢复原包。PMU 原始结果可用，但 CSV 未包含 enabled/running 时间，不能据此保证完全没有 multiplex；通用 cache 计数也不能冒充 DRAM 字节数。它们不构成候选已经减少整帧 CPU、涨帧或降温的证明。

本机证据：

- `.local/logs/q735-8480-cpu-flamegraphs-v1/high-level-analysis-root.json`
- `.local/logs/actual8480-wholepath-structural-review-v1/result.json`
- `.local/logs/native-official-cpu-pmu-7da96c6-Q735-v1/root-pmu-review.json`
- `.local/logs/shader-binding-consumption-d4e22c9-root-source-review.json`
- `.local/logs/operation-image-phase-artifact-d31e931-root-source-review.json`

Mali 依用户要求暂停。A650 平板独立处理实际 GPU fault：已在同一次 EPROTO 停点导出故障 IB 所在 BO，设备调试器与游戏已撤场，后续使用原 Mesa cffdump 离线分析；不与主线高通的 CPU 候选混测。

## 实际实施结果

三项现均完成，合入主线并备份：固定绑定 schema、图像请求阶段边界、首次 walk 融合 store index。前两项组合为 `9592893`，全部三项组合为 `d14808f`。两个版本分别完成 Android 原游戏进入营地、三段原摇杆跑动、原生命周期存档退出及官方 Simpleperf/Perfetto 采集；实际截图未见营地大黑块或明显几何错误。没有改动资源、驱动、分辨率、游戏逻辑或 Shader 运算来制造提升。

相邻运行保持同一 f830、6217 资源、固定 0.60 和 2750×1260。下表每一行均使用该次 Perfetto 中同一完整游戏 Surface 区间内的 Running，而不是将另一次 Simpleperf 的百分比换算为毫秒：

| 运行 | 完整 Surface 间隔 | 翻译线程 Running / 间隔 | 实际运行时加权频率 | 平均出图间隔 |
| --- | ---: | ---: | ---: | ---: |
| 8480 对照 A | 238 | 26.89ms | 1573.50MHz | 32.90ms |
| 959 前两项 B | 238 | 27.13ms | 1608.02MHz | 32.86ms |
| d148 全三项 C | 236 | 27.68ms | 1561.96MHz | 33.04ms |

**尚未证明整帧 CPU 或 FPS 净收益，三次仍约 30 帧。** 相同旧版本此前也测出 25.13～27.78ms，不能把这个范围内的变化归因于候选；也不能只按频率线性归一化，因为线程还在不同核心间迁移。两次 PMU stat 现使用官方 `--verbose` 保留原始 enabled/running，事件在核心间的实际覆盖合计约 100%，没有发现显著 multiplex；它们仍是独立 8 秒窗口，不是每帧指令数或 DRAM 流量。

保留三项的依据是可核查的重复分类和整次遍历减少、必要的串行/异步与内容检查、真实游戏短测通过，以及未见明显画面或重大资源成本退化；不是将局部减少写成已经涨帧。新增元数据读取、追加分类、边界检查和 walk 栈成本均保留在候选中，没有从对照中隐藏。

下一层表示复用暂不另造缓存：现有 stage-last、shader history、texture recipe 和 operation geometry 已覆盖相当多重定位。最终 descriptor 命中约 93% 不能证明 NVN 输入状态未变，更不能跳过实际资源消费。已有旧捕获的有限子集也没有证明有规模的重复消费者；例如同一 VkBuffer 的不同 offset/range 必须视为不同访问，不能按 handle 伪造去重收益。完整当前 N→U 比例尚缺证据，因此大型消费合并后置。

A650 补充结果：强制 SYSMEM 的第二次普通运行仍发生真实 kernel GPU hang/skip，不能把故障归为纯 GMEM；原有完整 shared-import-windows 双向、重叠和跨窗字节 oracle 在实际 A650/f830 上通过。基础共享能力有实证，实际游戏故障仍未修复。以上不能替代 6GB 持续游玩验收。

本轮真机结果：

- `.local/logs/native-cpu-combined-pair-v1-candidate-B-root-actual-review.json`
- `.local/logs/native-cpu-combined-pair-v1-candidate-C-root-actual-review.json`
- `.local/logs/descriptor-consumer-split-v2-investigation-agent.json`
- `.local/logs/descriptor-consumer-duplicate-audit-v1/result.json`
- `.local/logs/tablet-shared-633-f830-v1/standalone-shared-window-oracle-v1/closure.json`

## 新证据与下一轮优先级

同一 `d14808f` 原游戏另一次完整事务保留了官方 Simpleperf 的两个独立 8 秒窗口，以及原始 enabled/running 输出。前端停顿窗口中，用户态周期约 102.19 亿，L1I 相关前端停顿约 19.03 亿，占 18.63%；全部前端停顿约占 27.99%。另一缓存窗口中的 L1I refill/access 约为 11.03%。事件实际覆盖合计约 100%，未见显著 multiplex。

这些是**整条 NvN 线程的 PMU 事件比例**，包含宿主、Turnip、Bionic 和核心迁移的影响；不是已定位到某个函数的 18.63% 时间，也不是可直接兑现的提速比例。两个窗口不能拼成一条因果计时，前端各分类也不能任意相加。官方采样的具体调用路径与实际机器码用于选择候选，之后仍需真机对照。

1. **优先验证热路径代码布局。** 候选 `53c7d26` 将原有 PSO 准备、driver constant 真正未命中的构造，以及未对齐 Storage 复制阶段，分别留在原条件后调用独立函数。没有跳过资源消费、ready、hazard、pin 或写回，没有新增缓存或改变 Shader。实际原 NDK/O3 ARM64 对象中，绘制和绑定两个主要热函数合计缩小 6,988B，两个完整对象的 text 合计增加 504B；常见 draw/binding 的入口栈帧和减少 2,384B，PSO 准备慢路径的两层帧和增加 4,432B，并增加对应调用/返回。16 个原内容/异步/失败回退 Vulkan 用例通过，候选与同函数体内联参考的画面内容一致；这些不能代替 Android 原游戏验收。canonical 包和原始 NSO/verify28 构建进行中，设备对照使用原 `d14808f` 包。
2. **并行验证完整游戏中的 ASTC6 原生资源。** 实际 f830 bake 的 32 个条目已组成完整 6,217 资源族；其余 6,185 个条目、ID 和 family 字节保持原样，上游 UI 保留。165/166 两种格式均已在真机原 NVN 资源路径通过全部 mip、尾 mip、原图读写、共享页及先前 GPU 写入的内容验证。32 个条目的 payload 从 2,949,120B 降至 1,671,168B；全包只减少 1,277,952B，**不是全游戏减少 43% 内存或带宽**。下一步用原游戏验证 Builder、streaming 和画面，再扩展经过原画质规则批准的范围。
3. **A650 平板独立修复真实 GPU fault。** GMEM 和 SYSMEM 都出现过 kernel hang，基础共享字节 oracle 已通过。现在针对一次实际错误停点，用原 Mesa cffdump/RNN 解码命令和着色器输入，再在同一停点读取有明确 BO 映射的输入及生命周期信息；不把另一次运行的地址或资源拼接成证据，不继续随机改标志重试。
4. **大型消费缓存和去重继续后置。** 尚无当前完整 N→U 数据证明规模，已有多层缓存也已覆盖许多命中；先不引入新的失效协议。帧回调和翻译多线程依用户要求暂不实施，Mali 继续暂停。

本轮新增证据：

- `.local/logs/q735-d148-frontend-official-v1/root-frontend-PMU-review.json`
- `.local/logs/cpu-cold-phase-outline-53c7d26-root-source-review.json`
- `.local/logs/native-qualified-astc6-NVN-165-166-root-actual-review.json`
- `.local/logs/native-qualified-astc6-pilot32-root-asset-review.json`

ASTC6 格式 165 的实际 GPU 终态为 rc=0，内容和清理均通过，但当时控制器进程退出 1：原因是成功 stage 白名单漏掉新格式阶段。原记录保留；`882cca3` 修复了控制器，格式 166 已用其完成实际退出 0 的事务。没有把旧控制器退出码重新标为成功，也没有将独立资源验证记作原游戏通过。
