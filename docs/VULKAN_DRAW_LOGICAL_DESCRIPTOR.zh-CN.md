# 单次绘制的逻辑纹理描述符复用

基于主线 `2ad92a2`。当前官方 633 CPU Profile 中 `vk_texture_descriptor` self 约 1.24%、inclusive 约 5.75%；锁释放/CAS 样本与后代有重叠，不能相加推断节省毫秒。这是消除重复逻辑查询的源码候选，尚无本候选真机性能结论。

## 范围与有效期

一次 draw（含全部 CPU preflight、Pool admission、prepare、view binding）或 dispatch 由薄 wrapper 开启 scope；每个递归重试、原 ghost draw 开启独立子 scope，所有正常早退经 wrapper 关闭。8 层有界表外直接走原查表；128 槽 direct map 冲突只损失命中。只复用注册表中的按值 `tex_t`、`view_t` 和有效性，保留原 NVN opaque 字段合同；无后端 Pool、image、VkView、GPU handle 或 CPU-ready 证明。

表由现有单 recording owner 管理，只有开启 wrapper 的线程的 TLS 指针能访问；游戏/注册线程没有 TLS scope，继续原 `V.resources` 锁内读取。注册表增长不能留下借用指针。shutdown 释放全部表。缓存返回的内部指针立即复制到原 API 输出/调用者局部值，任何 publication、pass-end、ghost 或 upload 回调之前已完成复制。

最小键为 scope generation、descriptor id、descriptor epoch。注册/替换、退休、limit 改变均推进既有 epoch，第一次 MISS 在同一锁中复制值和 epoch；HIT 每次核 epoch。CPU 脏页、buffer epoch、物理 image generation 不改变这些逻辑值，故不加额外原子读取。原 CPU publication、动态数据、Pool/物理资源解析、promotion、mip-clear、hazard/producer 检查仍各次执行。

原 `texture_binding_scope` 仍仅位于 prepare→binding 的原位置，并继续表示物理借用禁止迁移；新逻辑 scope 不加入其 guard，不阻止原 Pool admission/promotion。既有物理 binding recipe 和 Vulkan descriptor-content cache 未改变。

## 减少与新增工作

同 scope、同 epoch、未冲突的同 descriptor 首次读取只做一次注册表锁与 184-byte 完整描述符复制；后续阶段不再重复锁或读取原 registry。原公共完整 `tex_t` 返回仍复制 144 bytes，不能声称所有结构复制都消失；footprint 只提取既有 24-byte 包络。scope 内 packaged 首次查询将完整逻辑值填入表，较原只读 has_layout/mem/size 的 private 轻分支多一次 184-byte 写，但给后续 admission/binding 共用。只有单次读取的 descriptor 不保证获益。

每 HIT 新增 TLS 指针读取、一个 32-bit 乘与右移索引、scope generation/id 比较、一次既有 descriptor_epoch 原子读取及 valid 检查；不读 dirty/buffer/image epoch，不做 allocator/hash loop。scope 开关仅 recording-owner 的普通整数与 TLS 存取；每层首次使用分配一次，之后不按 draw 分配/清表。无 scope 的公共 descriptor/footprint/packaged/view API 保留原窄锁内读法，不新增 epoch load 或 full-copy。

64-bit host DWARF 实际 sizeof：tex_t=144、texture_descriptor=184、snapshot=208、scope=24 bytes。每层 128 槽=26,624 bytes，最多 8 层=212,992 bytes（208 KiB），外加 192-byte scope metadata、owner clock 与小型 TLS；普通非递归绘制只分配一层。分配失败或 generation 耗尽回退原注册表查找。Android ARM64 这些 C 布局无指针/long 变宽差异，最终 Android 产物仍待独立构建核验。

本候选不消除 shader binding 枚举、物理绑定 recipe MISS、资源上传、dirty 检查或 Vulkan driver 工作；可给后续单 draw range/resolve 计划提供稳定逻辑值，不是已完成执行 packet。

## 源码验证

复用原 logical_preflight 的真实 GPU 像素 oracle（CPU/GPU 更新、格式、mip feedback、Storage promotion、回收 contexts），补充注册线程替换、registry growth、limit 失效、九层溢出/回父层和同深度重开。复用原 recipe_reentrant 的真实 ghost/pass-end 重入与缓存碰撞像素；复用原 native-retirement 的 release publisher/fence 生命周期。

仅新增 `DM_TEXTURE_LOOKUP_FIXTURE=1` 的 host 入口选取这些已有夹具；不新增生产统计器、计时器或采样器。Lavapipe `LP_NUM_THREADS=2`、官方 Core + synchronization validation，async/serial 均通过。夹具执行原 NVN 和同一 Vulkan 后端，不执行真实游戏模块，不能替代真机性能或游戏验收。
