# 完成线程的有界 readback Invalidate 批处理

基于冻结 1a5280a。现有 Mali OFF 官方 Simpleperf stat 的 8 秒窗口记录完成线程 96,448 次 minor fault；应用快照包络内有 1,649 次 allocation Invalidate。stat 没有调用栈，不能将这些 fault 归给 mprotect、Invalidate 或命令池 reset。此前官方用户态样本证明完成线程确实执行 Invalidate，但更大的 vendor 调用来自命令池 reset。此改动不是该 fault 数量的已证修复。

每个已完成 submission 仍先等真实 fence，再完成所有 noncoherent readback Invalidate，随后才允许 CPU map 读取、guest/shadow copy、事件和回调。唯一改变是将原逐 allocation 的 VMA API 调用改为至多 16 个 allocation 的栈批次，使用仓库既有 `vmaInvalidateAllocations`；VMA 自己处理原 allocation 与 nonCoherentAtomSize 范围。单 allocation API、coherent skip、错误检查、allocation 统计口径和 reset/回收时序不变。

n 个 eligible allocation 的驱动入口由 n 次变成 ceil(n/16) 次；不减少维护的范围或字节，不能据此声称 minor fault、DRAM、FPS 或发热已经改善。没有持久缓存、新锁、runtime 开关或计时器。C 侧指针数组与 C++ 转换数组各 128B（ARM64）；VMA 批量 API 另有自己的 inline 16 个 `VkMappedMemoryRange`，该小批次不触发其 vector 扩容。相较单项 API 新增有界收集/转换与栈占用，单项时没有入口数量收益。

原 GPU 回归继续覆盖字节写回与 reset 期间不可复用。新增 owned 测试有两部分：35 个独立 GPU temporary 的数组/发布顺序 oracle，包含真实 timeline fence 阻挡和 reset 阻挡；以及 35 个分离 Pool ROI 的原 writeback 路径。前者的 host wrapper 观察实际 allocation 集合和 CPU effect 尚未发布，coherent Lavapipe 仅在测试拥有的 VkBuffer 缓存字段清除 coherent 资格位，VMA 保持实际 memory type 不变。因此它不证明 NC 缓存行为。后者完全不改属性，Mali 0xb 可由 `NC_invalidations=35` 证明进入真实 NC 分支，Lavapipe 应报告 0。漏尾批次及把批次移到 CPU 发布之后的独立故障源码均应被原断言拒绝。

此阶段无设备、真实游戏或热收益验收；独立包后续必须严格要求 actual NC 结果，不能把 coherent/unsupported 路径冒充手机 NC 通过。
