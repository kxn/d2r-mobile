# 描述符创建 MISS 的冷路径

基于 e341eea 的官方 Simpleperf/Perfetto 原游戏记录，`shader_bindings_selected_impl`
仍在翻译线程热点中。原函数即使完整描述符内容缓存命中，也带有创建路径的大型
`descriptor_payload[128]`、`VkWriteDescriptorSet[128]` 临时数组。

本修改仅把创建与缓存发布的原 MISS 尾部逐字移入 noinline helper。资源解析、上传、
同步依赖、动态 Uniform offset、完整 key 比较、history 命中仍在原位置执行。
两种 Vulkan 更新方式及 last/history 发布顺序不变；没有内容预测、额外缓存、线程、
原子、环境变量、计时器或游戏修改。

NDK 28.2 / Clang 19、原 Android O3/TLSdesc/栈保护配置的独立编译结果：

- 主函数栈预留 14,864 → 6,192 B；这不是等量访存或 RSS 节省。
- 一个合法动态 Uniform 热前缀到首 binding 分派的静态路径，107 → 106 条指令，
  直接栈访存 29 → 27 次、少写 16 B；不是全部分支的动态平均。
- 较小的栈 offset 消除了部分原先需要两条 ADD 的地址构造，包括部分 descriptor
  value 和纹理 layout 参数。没有宣称减少同等数量的数据读写或 GPU 带宽。
- MISS 新增一次函数调用及其保存/恢复；父子合计栈预留 17,648 B，比原函数多
  2,784 B。主函数与 helper 合计代码增加；并非免费优化。

原内容缓存命中时可以省掉上述已核实的地址计算和少量 spill；MISS 仍支付 helper
成本。保留为低优先级源码候选，等待与下一次正式构建一起验收，不宣称已经涨帧、
减少大量 spill 或解决发热。当前更高优先级是官方缺页/内核数据暴露的写回成本。

原完整 NVN GPU 测试在 Lavapipe、两条 CPU 工作线程、Khronos validation 与
synchronization validation 下通过异步与串行模式，包含动态 Uniform、描述符
更新模板/普通写入、内容/history 缓存及碰撞、在途资源复用检查。没有增加语义镜像测试。
源尾部与原文件的逐字复原检查、NDK 编译结果、实际日志和独立审查保存在本地
`descriptor-create-cold-v1` 记录中；这些不代替 Android 真机验收。
