# Immutable 纹理 recipe 不随 submission 重写

此变更只删除 `texture_binding_recipe.submit` 字段及其触发的整结构重写。原 physical recipe 已经允许跨 submission 命中：查找依靠 descriptor epoch、recording-owner image lifetime generation、id/storage 和 owner 当前 VkImage。原字段只在末尾比较及赋值，不参与 prepare、查找、回收或任何物理生命周期证明。

原末尾条件将一个新的 recording serial 当作 MISS，即使全部 immutable 字段都未变化，也重新计算 logical preflight 分类并写入整份 192B recipe。现在仍逐项检查其余原 identity 条件，仅在 identity 变化时重建；未变化的跨提交 recipe 不再重写。

所有原动态工作均保留：每次消费先更新 image.last_use，执行上传、pending mip/NVN barrier 处理、alias/layout/hazard；回调后用当前 generation/epoch/handle 重验。末尾仍要求初始 local serial 非零且等于当前 serial，禁止把发生提交重入的结果错误写进另一个 recording。scope snapshot 的 `submit` 和 sampler lookup 的 `submit` 是其他语义，完全不改。

可复用字段严格限定于 immutable view recipe。没有增加 dirty/layout/visible 缓存，没有添加跨 draw 资源有效性假设或新每 draw 标签/原子。

## 现有证据与成本

add4bb1 当前官方 Simpleperf 676 个 NvN 用户态样本中，PC `0x2fa518` 为 recipe 重建中的实际 `stur q2,[x8,#0x3c]` store。另一个对应末尾源行的样本是函数 epilogue，未算为重建工作。1 个样本不足以估计净节省；不能把全部 texture 17.3% 或 binding self 2.51% 作为可省份额。

这属于删除已有机制升级后遗留的重复 stores，而不是新增缓存。新增成本为：旧代码在 serial 不同时直接短路进入重建；新代码在跨 serial 的有效命中上需要执行原有完整 identity 比较才跳过重建。已有同 serial 命中本来就执行这些比较；实际 MISS 仍走原重建逻辑。

NDK 28.2.13676358 / clang19、Android API29、O3/TLSdesc：recipe 192→184B，256 项的 texture context 51208→49160B（每上下文少 2048B，最多 16 个上下文少 32KiB metadata，非 GPU resident/Graphics 估算）。index stride 的原 `mov + umaddl` 指令模式未增加。`texture_view_binding_impl` 静态指令 800→796、frame 656B 不变；prepare 483→483、frame 544B 不变，callee saves 不变。没有新 heap、锁、线程或开关。现有 texture-binding reference 仍完整可用。

## 正确性验收

复用现有独立 GPU 字节 oracle，而非编写实现镜像测试。完整 Lavapipe、`LP_NUM_THREADS=2`、官方 validation + synchronization validation 的 async 22031790 / serial 21929811 checks 通过，零 VUID/SYNC。原 full suite 内包含：

- `recipe_reentrant_test`：outer upload 结束 pass，实际 nested ghost 覆盖碰撞 cache slot；后继 view、physical/storage/format promotion、跨 recording scope 与 reference 均核 GPU 像素。
- `logical_preflight_test`：同 id 的 CPU/GPU 内容更新、同 id retarget、碰撞、limit/epoch、格式、mip feedback、storage，18 轮跨 recording 与 recycled contexts 的真实红绿蓝像素。
- 原 depth/array/mip/deferred-clear、buffer 依赖等完整 GPU 回归保持。PL 专用真实 GPU 夹具另外以 subset async 398402 / shared-DS async 398486 / subset serial 398402 checks 通过，零 VUID/SYNC。

没有执行候选 APK 或真实游戏，没有新增采集、计时器或采样器；不宣称 FPS、热、功耗或净 CPU 时间改善。是否省掉整份重写由旧 submit-only 短路和新 identity 命中条件直接证明，实际动态命中次数没有另加计数。
