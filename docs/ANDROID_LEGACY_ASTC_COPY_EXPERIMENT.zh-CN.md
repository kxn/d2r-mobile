# Legacy Vulkan：fresh BC3 数组保留 ASTC4 物理编码（实验）

本分支基于 `c50faf743d4c199f270bc71edbae40d7c3b5b713`。默认关闭；不是游戏验收，也不依赖 PanVK、host pointer 导入或共享 GPU Buffer。Android 冷启动 Intent `legacy_astc_copy=true` 选择一次；改变选项须重启进程。需要原有独立翻译 owner，原同步/reference 模式或 owner 创建失败时保持原 RGBA 策略。测试入口另加 `run_vulkan_tests=true,legacy_astc_copy_test=true`。

## 允许的替换

仅实际 ASTC4 UNORM/sRGB 源到全新、未定义、原本需解码为 RGBA8 的逻辑 BC3 同色域目的；保留原 T2T GPU copy。目的无 packaged layout、非 external、非 Storage/RT，块几何、层/mip/ROI、物理格式支持及旧写历史均须满足原受限条件。目的完整地址和长度必须按真实 host 页对齐，并完全落在原 whole-pool dirty tracker。未知、未跟踪、页包络不完整、能力/容量不够、首次观察时发生变化均在替换前保留原 RGBA 路径。

不把 ASTC 字节冒充 BC3，不创建 BC3 view 读取 ASTC，不写回伪 BC3 canonical backing。无 Pool pin、新镜像、纹理内容快照或第二份 dirty scope；复用原 Pool state/gen 和公共短 CPU 观察原语。原源图像 deswizzle、BC 解码与 LDR 分类的实际 CPU 读取到最终重新保护都在同一短阶段，FS 配对 lease 与页门处理 prepare→pread 窗口。仅等待原确有 CPU effects/writeback 的页；私有 GPU Buffer 的普通读取不阻塞 CPU 源观察。

## 替换后的编码边界

原逻辑 BC3 默认 view 和显式同色域/对应 sRGB view 映射实际 ASTC4；后续相同物理编码的合法图像 copy 可继续。CPU 同值写、其它 mip 写、未知 dirty 生命周期、原逻辑 BC3 新内容、Storage/RT、T2B/ReadTexels、原线性 GPU Buffer 别名读写、其它 optimal writer identity 等不允许默默复用过期或异编码数据。可选 CPU proof 返回失败走原 GPU fallback；必需读取/非法混合编码明确拒绝。严格 alias 列表在 legacy 仅记录已替换图像，普通未替换 BC/RGBA 图像不继承 shared GPU-owned partial-freshness guard。

已替换范围另有最多 **256 个全进程 exact CPU 入口排除节点**（每个 24 字节加原 Pool 指针；先申请成功才替换）。无重叠时先检查原子 count/单个保守包络，不往返 owner；可能重叠才持原 resources 锁查 exact 范围，通过既有 CPU-exposure job 在 owner 拒绝。UpdateUniformBuffer 的录制时 CPU 源、Event 初始化/已存在 EventGetValue、InvalidateMappedRange 等已覆盖；GetBufferAddress 只是 GPU 地址元数据，不禁整池。

节点故意保留到 Pool 销毁，图像销毁后同范围重新作 CPU 输入仍可能被保守拒绝；Pool close 的原 owner FIFO/finish 之后才清节点，随后移除原 dirty range，同地址新 Pool 无旧排除。此项是默认关闭实验约束，不能称普适 NVN 内存复用支持。原 CPU 已持有裸指针、绕过宿主入口直接读取的行为无法由写保护发现；本实验只覆盖已明确审计的调用集，**不声称任意 CPU 可读纹理语义完整**。

## 验证口径

宿主 LVP 无 ASTC 硬件能力时，物理编码测试必须报告 `unsupported`，不能用元数据或 CPU 阶段通过冒充压缩 GPU 路径通过。现有测试复用真实 NVN Pool、原 dirty fault、配对 pread、原 copy/readback；不包含游戏模块或性能计时。Android 真机后须确认日志 `[ASTC resource copy] serial=... logical=0x44 physical=...` 实际命中目标数组，并由原 GPU 工具检查资源要求、采样带宽和整帧成本。

已知实际候选的 128×128×42、7 mip 数组有效 RGBA 逻辑像素约 3,669,792 字节，ASTC4 有用块约 917,952 字节；这是格式工作量，不是已测 VkMemoryRequirements、PSS、DRAM、FPS 或发热收益。主生产版本和共享驱动故障诊断不随本分支改变。
