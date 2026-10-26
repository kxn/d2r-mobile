# 固定附件策略下 Primary / Secondary 对照（2026-10-27）

本次回答一个具体问题：当前每个 Render Pass 使用一个 Secondary Command Buffer，是否是剩余性能差距的大头？不把驱动源码里的 pending flush / invalidate 直接换算成已经发生的 DRAM 流量或 GPU 时间。

## 实验实现与范围

源码 `6cd091de671b1d3f2275c4bed5c8b075b0b9e46f`，默认路径保持 mode 0。

- mode 1：固定附件策略，继续 Secondary 录制。
- mode 2：相同固定附件策略，直接在 Primary 内录制。
- 两组均开启 keep-attachment-stores、关闭 pixel-local 和 deferred mip clear；共同禁止录制结束后修改 Store / Load、只读深度 NONE、首 draw 覆盖丢弃和取消旧 clear。这是隔离实验所需的共同限制，不是建议撤掉这些已有优化。
- 保留真实资源依赖。Primary 已经开始 Render Pass 后，不能再把屏障写回它之前；遇到此类依赖走现有结束 Pass 路径，没有加入全局屏障掩盖差异。实际 Pass / Barrier 数另外用工具检查。
- 默认不启用实验。启动选项固定，热路径不反复读取环境变量。JNI、启动工具和实际后端日志共同核对模式，拒绝缺少必要条件的启动。

主缓冲路径仍沿用原 NVN 命令、HLE、离线改写与 ARM64 原生游戏执行。没有 CPU 模拟、没有新增游戏逻辑、没有修改已加工游戏 binary。改动仅涉及 Vulkan 录制方式、Android 冷启动选择与验证工具。

APK SHA256 `cc1c578aceddd8e1bc29d52defd8df0033fb2490c2347522febf8f9ef9822cb3`；host ELF SHA256 `ea13f8e74bc6720ba1a0c056894520ce74a4ecfce7407c81d16bf639e13f71dc`。规范构建入口生成的 APK / build.json / ELF 固定存于 `.local/build/fixed-primary-recording-v1/`。

## 真机性能结果

同一 Q735 / Adreno 735，同一 APK、f830 Turnip、prefer_sysmem、共享 native ASTC6 资源、shader chain `29e56b51bce5`。同一营地存档位置；游戏输出 1968×900、固定 3D 比例 0.85；Android 缓冲仍 2358×1080。游戏线程内翻译、CPU7、关闭非必要提前 submit、保留异步完成。

四次冷启动按 A/B/B/A；至少 tick 2500、8 次不同状态观测且管线/变体稳定才采集。各正式窗口内 PSO 和材质变体数量不变。系统 Perfetto 每组 20 秒，使用完整 game Surface 间隔和调度 Running，不在性能窗口中抓帧、点击、截图或运行 Simpleperf。表中 replay / pool reset 是现有 Perfetto slice 的每次调用 Running，不能把嵌套 planning 再加进 replay。

| 组 | FPS | 整个游戏/翻译线程 Running ms/显示间隔 | NVN replay Running ms/次 | 完成线程 pool reset Running ms/次 | 主线程加权 MHz |
| --- | ---: | ---: | ---: | ---: | ---: |
| SECONDARY-A1 | 39.963 | 19.911 | 12.842 | 0.302 | 1430 |
| PRIMARY-B1 | 40.088 | 19.625 | 12.436 | 0.035 | 1410 |
| PRIMARY-B2 | 39.990 | 19.961 | 12.370 | 0.037 | 1439 |
| SECONDARY-A2 | 40.083 | 19.942 | 12.654 | 0.338 | 1442 |

A 平均 **40.023 FPS**，B 平均 **40.039 FPS**，差约 **+0.039%**，没有可认定的涨帧。

Replay 平均约 **12.748 → 12.403 ms/次**，当前窗口少约 **0.345 ms**。完成线程 reset 平均约 **0.320 → 0.036 ms/次**，每个游戏提交周期约两次，相当于约 **0.57 ms/周期的后台 CPU 工作**。它是并行工作，不可当成主线程少等 0.57ms，更不可与 replay 相加宣称整帧减少约 0.9ms。

整个主线程 Running 平均约 **19.926 → 19.793 ms/显示间隔**，只下降约 0.134ms。各窗口 CPU 频率不同，未固定频率或据此推算精确指令节省。队列锁约 1µs/次；replay Off-CPU 约 0.004–0.015ms/次，这次没有发现“大块队列锁等待随着 Secondary 消失”的现象。

两组另有各一份 15 秒系统 Simpleperf 用户态采样，均无 lost samples；FP 链因 guest 边界被工具标为不完整，不把部分链当完整火焰树、也不累加 inclusive 百分比作为时间预算。绝对运行时间和锁等待以上面的 Perfetto 为准。

## 独立 API 结构核对：不能把持平解释为 Primary 无价值

两次现有 GFXReconstruct API-structure 工具捕获均实际闭合六帧，再原生命周期保存、停止游戏后拉取；全部设置恢复。此工具用于 API 结构，不把抓帧 FPS 当性能，不声称导入资源可完整回放。

| 每帧 | Secondary | 直接 Primary |
| --- | ---: | ---: |
| 实际二级缓冲执行 | 52 | 0 |
| Render Pass | 52 | 64 |
| Pipeline Barrier | 172 | 199 |
| Draw | 2,421 | 2,419 |
| Dispatch | 15 | 15 |
| Vulkan submit（含出图） | 2 | 2 |
| 附件全区域逻辑 LOAD 字节 | 142.15 MB | 202.72 MB |
| 附件全区域逻辑 STORE 字节 | 205.88 MB | 266.45 MB |

最后两行只是附件面积×格式字节的逻辑估计，**不是 DRAM 计数器**，不能把它当真实总线增加量。Draw 的 2 次差异属于两个独立营地动画窗口，未以删除绘制提速。

Primary 确实省掉所有 52 次 Secondary 调用，且 CPU reset 成本下降；但同时多出 12 个 Pass、27 个屏障。主 HDR 与后处理链的多段额外开闭，包含 1673×765、836×382、418×191 及其后的小尺寸。逐条还原实际执行顺序：Primary 每帧 12 次连续同色图像/子资源重新开 Pass，其中 11 次的上一段尚无 Draw，只含 clear；Secondary 本身每帧只有 1 次连续同色重开。拆分之间能看到源纹理从 ATTACHMENT_OPTIMAL 转 READ_ONLY_OPTIMAL 的实际 image barrier。

因此目前只能得出：**立即开始 Primary 的直接替换版本没有涨帧，同时确实引入了额外 Pass 成本**。不能据此认定“Secondary 无成本”或“Primary 方向无意义”。后续应先修复 clear 过早开始物理 Pass：保留有序 clear，首 draw 完成依赖准备再开始；遇到真实读取、query/timestamp 等观察点须先执行，不能越过它们。然后重新核对结构和性能。保留真实依赖，不以删除屏障或保守大屏障制造对照。

## 正确性与局限

四组 Linux 真 GPU fixture（llvmpipe，mode 1/2 × 同步/异步）合计 **309,284** 项颜色、深度和模板读回断言通过，Khronos Validation 未报错。普通 mode 0 的原 Store fixture 同步 / 异步另通过 **136,400 / 137,005** 项。启动工具 23 项测试通过。这些 fixture 不算真实游戏原生执行里程碑；另有上述 Q735 的四次真实游戏营地运行和保存退出。

Primary 营地截图没有发现明显黑块、缺几何等错误；这不代表全游戏场景已覆盖。实验两边共同禁用部分附件优化，不能把 mode 2 直接作为生产默认值。代码和开关保留，性能未涨不会作为删除已证实局部节省的理由。

原始证据目录：`.local/logs/fixed-primary-recording-v1/`，包括构建 / 安装记录、实际模式日志、四份 Perfetto、调度分析 SQL / CSV、Simpleperf、GPU fixture、截图及每次原生命周期 save=2 后停进程的记录。


## 第二版：按真实依赖延后 Primary Pass 开始

源码 `92c827c101188453f59121508805776084d19af4`。只对实验 Primary 增加“逻辑 Pass 尚未开始”的状态：保留最初的 loadOp clear，最多暂存 8 条有序的额外 clear；首个真实 draw 发出前开始物理 Pass，并按原顺序执行。容量满即执行，不无限积累。结束 Pass、真实依赖的读取边界照常执行；NVN timestamp report 即使复用 query pool，也先执行 pending clear，再写时间戳，不允许跨过可观察命令。

所有现有访问历史、写时钟、之前 producer 的证明、布局转换及真实 RAW/WAR/WAW 保留，没有用跨帧预测或删掉同步来避免拆分。逻辑 clear 的写访问照常立刻记入历史，因此不能把对本次 clear 的依赖错误移到它之前。

311,340 项固定模式 GPU fixture 通过（含超出 clear 队列上限、局部重叠、无 draw 直接读回以及复用 timestamp pool 的观察点），普通模式原 fixture 136,400 / 137,005 通过。该版 APK `dc14c0f16c9c6af1637f2213ef719e38420151df7b961c0f25df6da45a0f3b95`，ELF `f053c073636ca7eaf445710c01a9e8ae1af1293c796841c39dbeef3ed48e6a34`。

独立新六帧结构捕获已完成：**Primary 从 64 Pass / 199 Barrier 降为 53 / 174，仍然 0 次 Secondary 执行、2,421 Draw、15 Dispatch、2 Vulkan submit/帧**。修掉额外 12 个 Pass 中的 11 个；基线 Secondary 是 52 / 172。这里复用上一版已有 Secondary API 数据，代码改动在该模式不改变 GPU 命令；第二版的性能对照仍使用同一新版 APK。

剩下唯一 Pass 类型差异是新增一次 1673×765 的 R16G16B16A16 + RGBA8 + D24S8 LOAD/STORE。对应逻辑 LOAD 162.62MB、STORE 226.35MB，均仍比 Secondary 多 20.48MB/帧。这些仍不是 DRAM 实测。说明目前仍未达到完全相同的物理 Pass 结构，不能把后续帧率差异全部归因于 Secondary 调用本身。

第二版原始证据：`.local/logs/fixed-primary-recording-v2/`；第一版四轮数据仍原样保留，不能用后来修正版本的结果覆盖第一次试验。


### 第二版正式性能结果与最终判断

同一第二版 APK，按 A/B/A 三次冷启动；中间 B 同 PID 额外重复一段独立 20 秒 Perfetto。不是四次独立冷启动 ABBA。四个窗口 PSO 179、variants 32 均稳定；原分辨率、比例、驱动、资源、CPU mask 与首版相同。

| 组 | FPS | 主线程 Running ms/显示间隔 | NVN replay Running ms/次 | 后台 pool reset Running ms/次 | 主线程 MHz |
| --- | ---: | ---: | ---: | ---: | ---: |
| SECONDARY-A1 | 40.093 | 19.808 | 12.590 | 0.320 | 1455 |
| PRIMARY-B1 | 40.219 | 19.720 | 12.429 | 0.037 | 1421 |
| PRIMARY-B1-REPEAT | 40.225 | 19.805 | 12.567 | 0.037 | 1404 |
| SECONDARY-A2 | 40.172 | 19.866 | 12.466 | 0.186 | 1453 |

A 平均 **40.132 FPS**，B 两窗口平均 **40.222 FPS**，差约 **+0.22%**，没有实质涨帧证据。主线程平均 **19.837 → 19.762 ms/显示间隔**；replay **12.528 → 12.498 ms/次**，约 0.03ms 的差不足以认定明显下降。主线程加权频率也不是严格相同。**第一版 replay 少 0.345ms 的窗口差值，不能再写成稳定可兑现的翻译收益。**

减少命令缓冲数量的机制成立，完成线程 reset 的实测范围从 **0.186–0.320 → 0.0365–0.0373ms/次**。这是后台工作，不代表主线程等候减少相同时间。新版还消除了直接 Primary 首版额外的 11 个 Pass / 25 个 Barrier，降低其额外附件逻辑读写需求；不能因此声称实际 DRAM 已下降同样字节数。

这个实验完整经过了“发现没有涨帧 → 找出新增开销 → 修掉 11 个额外 Pass → 重测”，没有因为第一版帧率不变就扔掉方向。最终说明：**当前这种 Primary 替换已验证部分管理工作下降，但仍没有解决整帧瓶颈；12.5ms 左右的实际 NVN replay 还在。** 当前 replay Off-CPU 仅约 0.002–0.007ms/次，没有十几毫秒锁等待藏在这段里。把剩余 CPU 时间归到 Secondary 执行或完成线程锁，缺少证据。

保留两版源码、原始数据、模式开关和有序 pending clear 实现，默认 mode 0 不变。Production Primary 仍需兼容原有附件优化，并处理唯一新增 HDR Pass 的后续资源依赖；未完成前不会默认启用。研究顺序应回到当前 CPU 实际执行的资源/状态准备，以及实际 GPU 主组，不能把这个小实验当成“60FPS 已解决”。
