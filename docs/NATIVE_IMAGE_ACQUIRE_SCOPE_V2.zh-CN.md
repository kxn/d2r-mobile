# 原生图像 acquire 有向范围修正

c78704e 的单 mip 正负例不够：窄 FS acquire 把全部 mip 的状态清零，后续首次读取另一个 mip 时，布局转换可能从 TOP 开始，随后又被当作新阶段已可见，缺少 acquire→新消费者的链。c787 已禁止部署，原 SourceReady 和日志保留。只加测试的 25859ea 保留其生产 acquire 逐字不变，在记录真实 FS 采样后，由 unused-mip 有向源断言捕获该缺口；这是提交前的状态合同失败，并非观察到 GPU 坏像素。

后继将实际 acquire 目的阶段记入 whole layout_scope 和每个 mip 的 mip_scope，visible 仍为零。任何未使用 mip 的第一次布局转换，都从该实际源开始；GENERAL 不变的后续访问，也由原 image_dependencies 使用此源建立链。不是把只读访问当成永远不需同步。只有命中完整 pending-primary 独立性证明的片元采样，才收窄到 FS/SHADER_READ；无 pending pass 或 proof 否定时，保留原 ALL 目的范围，覆盖同操作其它阶段的 Buffer 别名。每 chunk 的 FOREIGN release、Pool lifetime 和 writer/WB/gate 原样保留。

原证明按宿主页取整，但启动允许更大的 host import/image alignment；NC atom 的 page 限制也不能代替所有别名规则。现在从独立 native allocation 的不可变 token 查询真实 import begin/end，再按 page、实际 nonCoherentAtomSize、bufferImageGranularity 的最大值扩张。同一绝对区间用于 Pool access、CPU publication、Buffer writer 和图像 alias 否定。不会扩大实际导入、拷贝 payload 或给所有资源新增内容追踪；代价是每次有资格前移的首次 acquire，多一次短资源锁及更保守的区间检查。注册 padding 不等于 image 绑定范围，但检查整 import envelope 避免依赖这个较窄推理。

多 mip host GPU 例使用真实 memfd/imported RGBA VkImage，三个 mip 经标准 GPU clear 初始化；原 NVN FS/CS shader 和标准 T2B 验证同 chunk FS mip0→CS mip1/T2B mip2，以及 CS1→FS0、T2B2→FS0。先用原 uniform shader 建 pending pass，确保第一组命中窄 FS acquire。由于生产原生表只允许 ASTC，测试只把一个已完成、从未生成 view 的 RGBA 占位 owner 换成真实 imported image；不放宽 parser、不宣称生产 RGBA/native 文件支持，也不伪造共享堆来源。所有 mip 在 Pool 账本中保守覆盖整份绑定字节，不假装取得了 opaque mip 物理排布。这个有限适配仅验证依赖算法；真实 AA185 ASTC 和原游戏仍需单独验收。

规范依据：

- [绑定范围](https://docs.vulkan.org/refpages/latest/refpages/source/vkBindImageMemory.html)由实际 requirements.size 决定。
- [线性与非线性资源的别名范围](https://docs.vulkan.org/spec/latest/chapters/resources.html#resources-memory-aliasing)还涉及 bufferImageGranularity。
- [同步与内存依赖](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)需要有向的可见性和执行链；只读以及队列提交边界均不能替代该链。

没有 GPU 时间、DRAM、FPS 或热收益测量。多 mip 字节通过与元数据正确性不能代替实际 ASTC 路径验收。
