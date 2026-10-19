# Android 默认启用源代码证明的局部附件读取

Android 普通启动默认尝试把符合条件的生产者和消费者放进同一个 Vulkan Render
Pass，用子通道的 input attachment 读取同位置像素，减少中间附件的访存。
`android-game-launch.py` 未指定开关时沿用应用默认；`--no-pixel-local` 显式选择
原分离路径。此项在 native 启动前确定，不能通过 `--attach` 切换。

这不是强制合并所有 Pass。离线 sidecar 必须与实际 SPIR-V、资源表和可选变体
逐项匹配；运行时还要证明坐标、采样器、附件、别名、查询和读写依赖允许合并。
缺少证明或资源条件不符时走原路径。回调后的资源身份与命令生命周期检查保留。
后端 CLI/host fixture 没有改变环境变量默认，Android 由启动参数明确选择。

共享只读深度附件仍默认关闭。它与此处启用的已有颜色附件 subset 路径是两个
独立开关；`--pixel-local --pixel-local-shared-ds` 才选择额外的深度实验。

保留依据是减少已证明的中间工作，并非即时涨帧：已有同包 Mali 官方完整帧数据
观察到外部读写合计约 367.8→354.8 MB/帧、片元任务减少约 3.65%；当时动画、
顶点工作和目标帧率有变化，不能把精确差值全部归因于此，也不能宣称功耗、温度
或 FPS 已改善。该 GPU 对照使用 whole-pool；当前默认的 16MiB Pool 窗口另外有
普通真机显示和 CPU 数据，不能拼成同一轮净性能结果。

成本包括有界命令前瞻、依赖重验、按需 Shader/PSO，以及已读取 sidecar 的内存。
不满足条件的尝试仍消耗 CPU；没有将“减少外部访问”当作无额外成本。已有候选
真机在 16MiB 窗口完成走动、550 次消费者合并、零次中途放弃和原生命周期存档，
尚不是完整游戏或持续温控验收。保留分离路径用于定位回归。

CPU 部分另外把计划中只需要的第二组渲染目标单独保存：每个 candidate/job 少
19,456 字节，保留完整生产者状态和两次动态扫描。原炫光前瞻也只在入口保存、
出口恢复整份状态，不再逐绘制反复复制。它们减少内存操作；不据此预报 FPS。

依据记录：本机 `mali-pixel-local-subset-adoption-readonly-v1.json`、
`current-cpu-298-mali-pl-off-subset-analysis-ready-v1.json`、
`pixel-local-second-targets-v1-source-ready.json` 和
`bloom-proof-state-cursor-v1-mali-actual-validation.json`。历史源版本、工具捕获与
当前默认启用版本分别记录，不能将历史捕获标成当前版本的新采集。
