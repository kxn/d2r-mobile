# 共享池的预排布贴图转换追踪（默认关闭实验）

本实验基于固定 shared/legacy 启动模式。共享 Buffer 继续直接使用导入的 CPU backing，不建立旧整池 dirty 登记、镜像、memcmp 或上传缓存。它不等于共享 VkImage：NVN 预排布数据仍须经原 deswizzle/必要 BC 解码，上传到 optimal VkImage。

## 已实现路径

- 只在 packaged image 确实需要 CPU 转换时执行原外层 preflight；首次保护前完成对应整个 Pool 的原 lazy import/pin。初次 import 失败不临时解保护重试、不在线退镜像。
- `dm_guest_shared_heap_contains` 必须证明完整页包络属于已提交 shared heap；缺接口、PRIVATE、越界均拒绝。它不是内存生命周期 pin，也不授权与 `SetHeapSize` 缩减竞争。测试只显式登记自己有限的 MAP_SHARED 映射，不能当游戏证明。
- image scope 引用实际覆盖的页面。相邻、重叠贴图复用原 state/gen；最后一份页面引用释放后才恢复 RW。每个不可变 1MiB 元数据桶只是寻址包络，未引用页面不会成为 tracked，也不会保护。元数据/页表不回收或重分配，避免 signal 读者访问释放的指针；每页高水位约13B，最多4096槽（legacy槽也占容量）。容量不足完整回滚并拒绝，不能产生半份 proof。
- 冷上传仍用原 protect/stamp、原范围 proof、原脏页判定。未改变的贴图重复使用不重传。跨桶不能取得单槽范围 proof，但按原页面代号保守判断；不猜 clean。
- 已知 Buffer GPU 写入更新对应 image 页代号，不假设 mprotect 能检测 GPU。只扫描已创建 image 桶的元数据，不读或比较 Buffer 内容。
- CPU 转换前若源有尚未发布的 GPU/shared writeback 或 CPU completion 效果，使用原提交/等待及 completion 发布合同。原 shared writeback 已完成 Invalidate 后才发布 CPU 完成；这里不随意 Invalidate 整个 CPU-dirty 纹理区间，以免丢弃新的 CPU cache 内容。
- Pool 关闭沿原队列顺序 finish 后移除其逻辑图像与 scopes，撤销 recipe generation，最后解除导入和 Pool 元数据。旧物理图像 promotion 副本仅释放物理资源，不重复释放 scope。

## 尚未闭合，不能称为可玩共享游戏完成

GPU 写 optimal VkImage 后，尚未实现其 NVN 预排布内存的反向 retile/别名发布。因此 GPU-owned 图像与线性 Buffer 实际访问相交、或该图像又需从 CPU packaged 区间重建时明确拒绝，不能拿旧 CPU 数据上传冒充正确。只按实际 GPU-owned 图像列表检查，不把全体静态贴图放到每次 Buffer 访问扫描。

通用 ReadTexels 的 shared 目的、其他 CPUeffects 与同录制 GPU 消费依赖、任意 image/Buffer alias、原 WaitEvent 全语义，以及完整游戏模块生命周期/heap 缩减仍需单独闭合。Event 和 Query GPU-native 发布由相邻候选完成，不由本实验假装 noop。

本候选只完成 CPU/GPU 对 packaged bytes 改写后、转换 image 正确刷新的受限路径。宿主 LVP 类型0xf不能证明 Mali 类型0xb；无真机或游戏/性能/温度结论。

## 成本与验证

新增 image 页引用/不可变桶元数据、首次 import、dirty 检查及实际 CPU 观察所需等待；未引入每 draw 全纹理上传、计时器、采样器或新缓存预测。shared 模式仍编译和启动双重默认关闭。后续须在同配置原厂 CPU/GPU profile 中评估查找及 NC cache 成本。

原 GPU 纹理字节 fixture 验证同页两图、重复使用零重传、真实 CPU fault、原 shared GPU Fill/WB 后两图刷新、Pool 关闭后解除保护且原映射可写。纯 host dirty scope 验证跨桶、重叠/部分页引用、最后释放、旧 proof 失效、故障 BUSY 与并发释放、容量部分失败回滚。缺 provenance、GPU-owned image/linear alias、CPU dirty/GPU-owned image 冲突保留明确负例。
