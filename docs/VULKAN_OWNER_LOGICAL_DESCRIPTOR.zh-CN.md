# Owner 纯逻辑 descriptor 跨 Draw 复用

本候选基于冻结 `846fb023`，只改变其纯逻辑 descriptor 表的寿命与存储。单 recording owner 的 Draw/dispatch wrapper 继续以 TLS 开关访问；无 scope 的生产线程沿原窄锁查询。相同 ID、相同 `descriptor_epoch` 可跨 Draw、递归 ghost、物理提交与 submission context 复用完整按值 `tex_t/view_t/valid/has_view`。不新增共享图像、dirty、CPU-ready、初始化、layout、GPU visibility 或物理句柄证明。

原每 Draw generation 与八张表合并为一张 128 槽 direct-map 表。所有内部返回值在 mip-clear、publication、render-end、upload 或 ghost 回调前复制到调用者输出/局部值；子调用可替换同槽，仅导致父调用下次 MISS，不影响其已复制值。原物理 `texture_binding_scope` 栈和 scope-depth 超过 8 的锁内回退保留。

注册/替换、limit 改变和真正退休失效都持有 `V.resources` 并推进 epoch。新增统一 checked publisher：同一锁下读取旧 epoch，在 `UINT64_MAX` 时终止，绝不发布回绕后的 0。generation 不再被当作意外的 wrap 防护。首次表分配把 ID 初始化为 -1，避免空表 ID0/epoch0 被当成已填充；负 ID 仍只能得到 invalid。一次分配失败不在每 Draw 重试；quiescent shutdown 释放/清零并允许下次初始化。

已有 host DWARF 实际布局：tex_t 144B、完整 descriptor 184B；新 entry 200B，128 槽共 25,600B。旧普通一层为 26,624B，减少 1,024B；旧八层极值为 212,992B，减少 187,392B。省掉每 Draw 换代与 scope-generation HIT 比较。新的有效跨 Draw HIT 省注册表锁、原 registry 读取及 184B cache fill；公共完整 tex_t 输出仍复制 144B。direct-map 冲突与 epoch 变化仍 MISS，不增表或新增生产计数/计时器。

线程合同仍是原单 recording owner。legacy `DM_NVN_SYNC` 可以在不同线程顺序执行；TLS 不是并发录制锁，候选不声称支持同时操作全局 S/texture_state 的多个提交线程。`DM_VULKAN_ASYNC` 不等于 NVN 翻译线程模式。shutdown 仍需 owner quiescent，不能一边执行 Draw 一边拆 renderer。

复用原有限 GPU fixture，补跨 Draw 同 ID 重注册的真实颜色、跨实际退休宽限与同地址新 serial、shutdown 后 ID0 refill，以及原真实 ghost 碰撞、STORAGE promotion、CPU/GPU 改写、mip feedback 和物理 context recycle。host-only cold getter 仅观察表槽/空表，另有 epoch 耗尽 death entry；两者受 `DM_VULKAN_TEST_EMBED && !DM_ANDROID_APP` 限制。missing-epoch fault 应命中跨 Draw 的实际像素断言，而不是依赖抽样命中或微秒计时。

此阶段只执行 host Lavapipe/Core+SYNC 与 ASan，无 Android canonical、ADB 或真实游戏模块执行。旧 93% physical-recipe 命中不是新 logical 表命中率；不承诺 10ms、16.7ms、FPS 或功耗收益。冻结 Ready 给出精确命令、源码与每条输出哈希。
