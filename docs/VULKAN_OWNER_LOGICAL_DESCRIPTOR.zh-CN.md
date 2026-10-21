# Owner 纯逻辑 descriptor 跨 Draw 复用

本候选基于冻结 `846fb023`，只改变其纯逻辑 descriptor 表的寿命与存储。单 recording owner 的 Draw/dispatch wrapper 继续以 TLS 开关访问；无 scope 的生产线程沿原窄锁查询。相同 ID、相同 `descriptor_epoch` 可跨 Draw、递归 ghost、物理提交与 submission context 复用完整按值 `tex_t/view_t/valid/has_view`。不新增共享图像、dirty、CPU-ready、初始化、layout、GPU visibility 或物理句柄证明。

原每 Draw generation 与八张表合并为一张 128 槽 direct-map 表。所有内部返回值在 mip-clear、publication、render-end、upload 或 ghost 回调前复制到调用者输出/局部值；子调用可替换同槽，仅导致父调用下次 MISS，不影响其已复制值。原物理 `texture_binding_scope` 栈和 scope-depth 超过 8 的锁内回退保留。

注册/替换、limit 改变和真正退休失效都持有 `V.resources` 并推进 epoch。新增统一 checked publisher：同一锁下读取旧 epoch，在 `UINT64_MAX` 时终止，绝不发布回绕后的 0。generation 不再被当作意外的 wrap 防护。首次表分配把 ID 初始化为 -1，避免空表 ID0/epoch0 被当成已填充；负 ID 仍只能得到 invalid。一次分配失败不在每 Draw 重试；quiescent shutdown 释放/清零并允许下次初始化。

已有 host DWARF 实际布局：tex_t 144B、完整 descriptor 184B；新 entry 200B，128 槽共 25,600B。旧普通一层为 26,624B，减少 1,024B；旧八层极值为 212,992B，减少 187,392B。省掉每 Draw 换代与 scope-generation HIT 比较。新的有效跨 Draw HIT 省注册表锁、原 registry 读取及 184B cache fill；公共完整 tex_t 输出仍复制 144B。direct-map 冲突与 epoch 变化仍 MISS，不增表或新增生产计数/计时器。

线程合同仍是原单 recording owner。legacy `DM_NVN_SYNC` 可以在不同线程顺序执行；TLS 不是并发录制锁，候选不声称支持同时操作全局 S/texture_state 的多个提交线程。`DM_VULKAN_ASYNC` 不等于 NVN 翻译线程模式。shutdown 仍需 owner quiescent，不能一边执行 Draw 一边拆 renderer。

复用原有限 GPU fixture，补跨 Draw 同 ID 重注册的真实颜色、跨实际退休宽限与同地址新 serial、shutdown 后 ID0 refill，以及原真实 ghost 碰撞、STORAGE promotion、CPU/GPU 改写、mip feedback 和物理 context recycle。host-only cold getter 仅观察表槽/空表，另有 epoch 耗尽 death entry；两者受 `DM_VULKAN_TEST_EMBED && !DM_ANDROID_APP` 限制。missing-epoch fault 应命中跨 Draw 的实际像素断言，而不是依赖抽样命中或微秒计时。

源码候选阶段执行 host Lavapipe/Core+SYNC 与 ASan。随后 `98a534e` 已完成独立 canonical、原始三份 NSO 重建和 28 项部署文件验证，并在 Q735 上实际执行原游戏：营地三段原操作路线、正常生命周期两次存档、恢复原包及资源、退出。两张实际营地图像未见明显黑块或几何错误，不能据此保证所有场景。

官方 Simpleperf/Perfetto 记录如下。各行是各自完整 Surface 间隔中的 CPU Running 平均工作量；历史窗口的绘制数量、PSO 和 CPU 核分布不同，**不是匹配的性能对照**。

| 实际版本 | 完整翻译线程 CPU / Surface 间隔 | 回放 CPU / Surface 间隔 |
| --- | ---: | ---: |
| `633ea75` | 27.22ms | 26.08ms |
| `846fb02` | 25.60ms | 24.57ms |
| `98a534e` | 26.88ms | 25.81ms |

`98a534e` 的 236 个完整 Surface 间隔均值为 33.09ms。呈现调用每次平均 wall 6.61ms、其中 CPU 0.56ms，不能把 wall 当作额外 CPU 时间；实际翻译 CPU 仍超过 16.7ms 预算约 10.18ms。描述符注册表锁的保留叶样本由历史 `846` 的 13/583 降至 2/604，但 TLS resolver 也有 5/604 个叶样本；不把采样比例直接换成每帧毫秒或声称已达到 60fps。

新增 Draw wrapper 引起的编译器内联变化曾把两个可选前瞻分析中的大 `vk_state` 临时值并入热 Draw 栈。`98a534e` 只为 `bq_prove`、`pl_plan` 保留独立函数边界，最终完整 APP 的 Draw 帧恢复为 6,496B（wrapper 48B）；参见 `DRAW_LOOKUP_COLD_STACK_BOUNDARY.zh-CN.md`。原 16KiB Simpleperf 采集未扩大，`x_thread` 祖先覆盖由 `846` 的 101/583 恢复至 495/604。这证明了栈结构修正，不等于已测得净帧率提升。

保留本优化的依据是有界内存、明确减少的注册表锁和复制，以及已通过的正确性检查；不以本轮即时涨帧为条件。旧 93% physical-recipe 命中不是新 logical 表命中率。后续仍需减少实际资源准备与依赖处理的重复工作，不能缓存掉真实 GPU 写入、CPU 发布、物理寿命或同步要求。

本机证据：`.local/logs/native-candidate-cold-stack-98a-Q735-v1-root-actual-review.json`（SHA256 `cf807201bddb4938e7f218bfbeb3fb7a7e6e9a9bf19fdb12a41796ff4a0dccfb`），引用完整原厂工具输出、源码指纹、产物身份与清理记录。未证明持续游玩功耗改善或 6GB 长时间稳定运行。
