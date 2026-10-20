# 原生图像前一提交写入与 pending acquire

31d3c98 修正了跨 mip acquire 目的链与物理 envelope，但其独立性证明拒绝所有 retained Buffer writer。真实 native B2T 不会进入 private-optimal alias 树（`image_shared_alias_write` 对 native_entry 直接返回），却会留下跨 chunk 的 Buffer 写入记录。除非原始完整屏障清表，后续首次采样仍无法用 pending primary。这限制适用性，不能把旧 fixture 命中当成真实游戏命中。

后继只允许真实已知 TRANSFER/COMPUTE producer，且必须保留原 Pool identity、物理 Buffer 与 backing generation。未知合并来源、overflow、CPU publication、NVN order/reference、无界 transfer 都继续拒绝。完整实际 import/page/atom/bufferImageGranularity envelope 的当前 recording GPU pages 必须完全无交叠，读和写都拒绝；因此这里只处理前一 chunk 的 writer，不把同 chunk 的早一 pass 猜成安全。普通 private-optimal alias writer 仍全部拒绝；其内容在另一份 VkImage 中，只有原 canonical retile、CPU 完整重建或合格逻辑退休能够撤销其字节权威，不能仅靠年龄／fence 完成放行。

通过全部证明后，在同一个 pending primary 上、FOREIGN acquire **之前**调用原 `vk_buffer_native_image_read`，记录真实全局 producer-write → fragment-read 内存屏障。该全局屏障覆盖不同 Vulkan 对象，不能用某个 VkBuffer 的 visible 位代替。复用其真正的 writer generation，并只把实际 FS 目的保存给 image，后续 post-Draw 记录无需为同一代再发一遍。新 writer 仍使 generation 变化；饱和的零值不复用。没有清 writer 历史、内容预测、新缓存或新的同步引擎。31d 的全 mip 有向 scope、visible=0 和非命中 ALL 目的原样保留。

代价是首次 acquire 多一次原有 bounded writer 查询，以及存在旧 producer 时的一条全局 memory barrier；它可能省掉一个实际 render pass 结束／重开，未测 GPU 时间、DRAM、FPS 或发热。创建新 image 的冷路径强制结束与每 chunk FOREIGN release 仍未改。

有限宿主 GPU oracle 使用真实 memfd/imported RGBA 原图像，真实原 NVN B2T 或原 compute-copy shader 在前一 chunk 写入同一 physical backing；后一 chunk先绘制无关 pending pass，再 FS mip0、CS mip1、T2B mip2，逐项核对输出。明确断言 writer 仍保留、同 recording 拒绝、未知 ALL producer 拒绝；另保留当前及前一 chunk private-optimal alias 否定和原完整回归。前面 ReadTexels 有意留下无界 staging transfer，因此只在新 producer **之前**发原完整屏障隔离测试 setup；被测试的 writer 产生后不清表。它不是不安全游戏状态的放宽，也不是 ASTC opaque 布局的证明；实际 AA/ASTC 与批量游戏仍需真机验收。
