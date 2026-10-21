# 压缩 page-array 后继：设计审查点

基线 `989320ea85f4278f2096ca14cdc942ab295448ef`。当前仅源码/既有证据审查，
没有新增运行时代码、构建、采集或设备动作。旧 legacy ASTC copy 开关保持默认 OFF。

## 当前证据与原实验不能直接扩开的原因

`mali773-official-gpu-priorities-readonly-v1/result.json` 的真实系统计数显示 TEX 外读
4.718GB/s，约为 global read 的 62.2%，并有 pending-data 等待。不能把这部分全部
归到某一数组。历史 full6 官方 API 中，586 个主 HDR 热 FS draw 仍绑定
R8_UNORM 128²×252、RGBA8_SRGB 128²×84，各 7 mip；绑定次数不是采样次数或流量。

现 `image_get_usage()` 不支持 BC sampling 时解码到 R8/RGBA8。
`nvn_vk_astc_copy_compat.inc` 的旧替换仅 BC3→ASTC4、whole-fresh/观察证明；
不支持 ASTC6、BC4 或通用后续消费。其 CPU dirty、混合编码、Storage、原 Buffer
输入/输出、ReadTexels 等拒绝不是可删除的优化门。混合页现已有保存所有 mip/layer
到 RGBA8 的路径，但不能据此重新编码 BC，不能把 RGBA/ASTC 字节返回为 BC。
普通 decoded BC 的 T2B 自身也明确 `decoded BC recompression pending`。

真实 bb4 typed 失败已经证明：BC3 128²×42/7mip array 在 layer37 接收 ASTC 时
目标已初始化、有原 shared alias/writer；不是 undefined。完整转 RGBA 的 56 后继
保留其余层/mip解决了这一情况，不能用 fresh 扩权替代。另一类历史 full6 黑地面
源于 terrain array 成员 ASTC4/6 混形，须保留原资产 array closure；运行时不得猜场景。

现有 full6 frame 的 executed graph 只有两次 CopyImage（D16 shadow、3D LUT）和
一次 BlitImage（bloom 尺度变换），没有上述 page-array 的装填。它能证明最终实际
采样格式/视图，不能证明启动历史中所有层/mip 来源，不能凭已绑定视图补造 coverage。

## 唯一可撤销的窄方案：采样缓存，保留 canonical

若要求后续未知混合、CPU、Storage/Buffer 访问不新增语义限制，先保留原 canonical
图像及全部原通路；额外维护可撤销 ASTC sampled-only 副本。它减少被采样的工作集，
**不减少 canonical 解压存储，不是净 RAM 节省方案**。直接删 canonical 必须先有
逻辑 BC raw 字节重建合同，当前没有，因此不作为本候选。

第一范围仅未外部导入的 2D array、单采样、原逻辑 BC3、实际 decoded RGBA8
UNORM/sRGB，来源实际物理 ASTC4/6 且色域严格对应。仅 sampled 使用，不替换普通
NVN资源身份、guest bytes、native recipe 或 driver import。BC4 SNORM 明确不纳入；
BC4 UNORM/R8 后续单独证明组件映射/量化后，只考虑确实省字节的 ASTC6。

在原 T2T 完成 canonical 写入的同时，以下条件才可给压缩副本建立一个 mip/layer
有效位：源/副本实际 ASTC 格式完全一致；copy 是无缩放、无过滤、完整目标子资源；
原源 bounds/block geometry、format feature 和访问依赖全部成立。未知/未填充从不
自动视为有效。所有 actual view 可访问的 mip/layer 有效，才可改采样 descriptor；
没有完整覆盖就一直走 canonical，不推断 shader 从未读某槽。

所有真实 canonical writer（原 image_record 的写入口）先使对应位失效；partial、
不同 ASTC 块、普通 BC/B2T、clear、Storage、CPU upload、物理更换等都回 canonical。
必须重用原 dirty/代际/实际 writer，不缓存内容或“以后不会写”的预测。清除/重建
coverage 必须使原 descriptor recipe/history 拒绝过期物理 view。不能每 draw 全扫
资源或全部 cell：小 bitmap + 实际 writer 代际，view 的完整覆盖证明仅在变化时重算。

副本采用原 image hazard/access history、view/cache keys 与 completion retirement。
旧 GPU 已持有的压缩 view 不得被立即销毁或无依赖覆盖；新 copy 与先前 sampler read
仍有真实 dependency。canonical 与压缩副本是不同 physical owners，不能伪造同一
VkImage 的 history。replacement/promotion、image eviction、finalize 和 shutdown 的
旧引用与 VMA 计账必须完整，不能将额外 allocation 隐藏在预算之外。

CPU ReadTexels/T2B/rawBuffer、Storage/attachment、格式重解释仍只消费 canonical，
不返回 ASTC bytes 冒充 BC。原不支持的 decoded BC recompression 仍明确拒绝；
这不是补完该既有 NVN 缺口。新路径必须使所有 baseline 已支持消费不受影响。

## 格式工作量与新增成本

7 mip 指 128,64,32,16,8,4,2；下表为 useful bytes（含 block 向上取整），
不是 VkMemoryRequirements、驻留、DRAM 或 FPS：

| 数组 | canonical useful | ASTC4 useful | ASTC6 useful |
| --- | ---: | ---: | ---: |
| R8 128²×252 | 5,504,688 B | 5,507,712 B | 2,644,992 B |
| RGBA8 128²×84 | 7,339,584 B | 1,835,904 B | 881,664 B |

R8→ASTC4 反而多 3,024B，不能当压缩节省；SNORM 也不能偷换 UNORM。
第一版 RGBA 数组额外 ASTC4 副本约 1.751MiB，ASTC6 约 0.841MiB，原约 7MiB
canonical 保留。除 VkImage/VMA/views/access 记录外，每 cell 至少 1bit coverage；
84×7 为 74B，需包含代际/查询 memo 的实际 sizeof。原 access history 若直接复用
image allocator 还会有 3×levels×layers×sizeof(image_access)，不能只报 bitmap。
每次可缓存装填增加一次 compressed GPU image copy；不会消掉原 canonical blit。
必须有冷内存/容量上限，OOM、能力不符或超限只失效可选副本，原操作照常进行。

现证据不能证明 full-view coverage 会在实际 camp 达成。若 conservative gate 不命中，
应报告适用性不足而非放宽 unknown slot。也不能在没有计数对照前将上述 useful
bytes 比例套到 4.718GB/s。

## 实现前与验证门

先由源码审查决定是否接受“额外小压缩缓存换 TEX 工作集”的成本；若目标必须是
释放 canonical 解压存储，则当前通用 raw/CPU 合同缺口仍阻断，不假装已解决。

实现只在独立默认 OFF 模式，不复用/放宽旧实验 flag。复用已有 mips/layers/partial
copy/CPUwrite/alias/ReadTexels/async/GPU-old-reference oracle。Host LVP 没实际 ASTC
时只能验证 RGBA plumbing/失效和拒绝，不能宣称 compressed GPU 通过；实际 ASTC4/6
须后续在原硬件/原测试入口验证，不写新 renderer 或计时器。正例覆盖完整有效 view，
负例涵盖漏一层/漏 mip、不同块/sRGB、partial、后续 CPU/Storage、旧 view inflight；
像素要检查未覆盖层和多 mip sentinel。独立去掉失效/coverage 门必须实际失败。
通过后再由已有官方 GPU 工具核实际被采样 VkImage 格式与 TEX 外读，守住额外内存、
复制和 CPU 维护成本；不把绑定次数、host probe 或短测当整帧/热收益。
