# 压缩 canonical + 按需 RGBA 重建：范围复核 v2

基线 989320e，仍是设计/源码审查，未修改运行时或构建。此文纠正首版设计中
“必须常驻 canonical+shadow 才能稳妥回退”的过强结论；首版及其报告保留。
`image_bc_copy_decode()` 已有逐 mip、全部 layer 的 GPU blit/物理 clone/退休骨架。
ASTC→RGBA 保留像素不需要 BC 重编码。BC 重编码只阻断编码后的 raw 字节出口，
不能据此否定原已支持的 decoded GPU consumer 可以恢复。

## 首个可实现范围

只在系统/私有(sharedOFF)、旧 legacy flag OFF、无 external/native recipe，且
**设备实际 BC3 格式不可采样、baseline 已 decoded 为 RGBA8** 的 fresh 2D array。
单采样、无 packaged initial contents、无已定义 alias、原 writer/history 为空。
初始新表示的“已定义内容集合”为空，不把已有未知/已初始化数组改说 fresh；
只有之后实际相容 copy 写入的内容才加入集合。未定义槽仍是未定义，不需假造全填。
任何已定义内容不相容或不明，都在替换前保留原 RGBA 路径。

来源仅实际 ASTC4/6、同 UNORM/sRGB，对应完整源 staging 的 LDR 分类证书。
不能从格式名推断 LDR（原 RGBA blit 会限幅）；复用现 `nvn_astc_mip_is_ldr`，
在原 upload 已持有的 staging 字节上分类，不重读 guest、不每 draw 扫描。
现证书只在 decode-mode/UNORM 单层条件下建立，不可假设所有源已有，需新模式
显式复用分类。未知/GPU改写失去证书/HDR 不进入此优化。

copy 保持压缩仅允许同一实际 ASTC block 格式、色域及合法块几何；128 等非块倍数
只允许 Vulkan 合法的 mip 边缘。partial 同编码块 copy 可以保留原已定义像素；
不同块、过滤/缩放、非兼容 view、普通 BC/CPU 输入等先完整 demote，再走原操作。
不要求事先猜完未来命令；一旦 demote，此 texture identity 固定留在 decoded，
不在采样/写入之间反复变换。BC4/R8/SNORM 第一版排除。

## Consumer 矩阵（相对实际无 BC 支持的 baseline）

| 原入口 | baseline | 新表示要求 |
| --- | --- | --- |
| 默认 sampled view | RGBA8 | 同色域 ASTC；沿原 hazard/layout/descriptor 使用 |
| 其它合法格式 reinterpret view | 原 decoded 映射/兼容类 | borrow view 前 demote，原映射与合法性检查原样 |
| 相同实际 ASTC T2T destination | ASTC→RGBA blit | 合法同块 copy 保持压缩；其它情况先 demote，再原 copy/blit |
| 新 BC、不同 ASTC、filtered/scaled/partial 不合法块 copy | 原 copy/blit | 全 mip/layer demote 后原逻辑，不拒绝原来合法 copy |
| B2T CPU BC 数据 | CPU decode + RGBA upload | admission/CPU proof 后、借 handle/temp 前 demote，然后原 decode；不读旧 GPU 源 |
| clear、Storage、attachment | 原 RGBA clear/usage promotion | descriptor/framebuffer/physical access 前 demote，再原 feature 与 usage 检查 |
| 原 T2B | decoded BC 明确 recompression pending | demote 后保持同一既有拒绝，不推广到有 native BC 支持设备 |
| ReadTexels | 缺合同：物理 copy 大小与逻辑 BC callback 行距不一致 | demote 仅保持既有行为，不称该 API 正确；独立待修项，不能错说 baseline 都拒绝 |
| raw VkImage/external/host enhancement | 原 RGBA physical handle/unknown access | 返回任何 handle/view 前 demote；保持原 lifetime/unknown 标记 |
| Pool Buffer/CPU alias | 私有图像与 Pool 原语义 | 不写回 ASTC 冒 BC，不启旧 sticky CPU 禁入节点，不新增原来合法访问拒绝；shared 模式不 admit |

ReadTexels 缺口在 `mtl_read_texels`：`copy_region/copy_bytes` 基于物理 RGBA，
回调宽度/行数又取逻辑 BC；GPU 色值重建不能提供原 BC bytes。此项不因为新优化
变成已实现，不能用测试句柄直接读 RGBA 就称 NVN ReadTexels 验收通过。

## 替换前能力闭包和物理寿命

在第一次可选替换前，必须同时证实：实际 ASTC sampled/transfer/BLIT_SRC 支持；
原 RGBA sampled/transfer/BLIT_DST 支持；两种 2D array 的完整 extent/levels/layers/
flags/usage 查询合法。没有可靠 demote 能力就不替换。第一次可选 allocation 失败
保留旧物理图像及操作，不能先改 metadata 才发现不能创建。
后续本来合法的 Storage/attachment usage、format-view 能力沿原 baseline 查询，
不能以压缩格式不支持它们为理由拒绝；先回 RGBA。新状态不能复用旧不可逆
`astc_copy_encoding`/`bc_copy_decoded` 的 guard 语义。

重建先结束原 deferred pass/回调并重查身份，再借旧 physical handles；不调用
internal commit/wait，也不把 operation 中的指针跨新增维护边界。逐 level、全部
layers blit，原真实 image access dependency 不删，未覆盖层/mip 保留。
复用 `image_replacement_detach/image_binding_replaced/image_recipes_changed` 及
`vk_completion(image_storage_destroy, old)`；旧 views、descriptor/framebuffer keys、
image_batch owner、access generations 与 VMA 总字节一直保留到真实完成。
不能在 image_record 已记录旧命令之后临时 demote；必须从所有 consumer 入口前置。

常态只保留压缩 canonical；首次 promotion/后续 demote 各可能有旧+新 transient峰值。
128²×84/7mip useful RGBA=7,339,584B，ASTC4=1,835,904B，ASTC6=881,664B。
因此理论稳定 useful storage 可减约5.25/6.16MiB，但实际 VMA/驻留/DRAM 未测。
新增代价：首次分类、一次物理创建/退休、真实 fallback 全图 blit，以及小状态字段；
不引入每 draw coverage 全扫描、CPU镜像或常驻双份。不能把模型当 4.718GB/s 的份额。

## 已有适用性证据与仍缺的验证

旧实际 1a ON 日志确有一条 fresh BC3→ASTC4 的原游戏命中：serial19、7mip、42layer，
但不能转称当前 full6 的 RGBA sRGB84layer 或 R8 252layer 已满足条件。
full6 单帧没有目标 array 填充历史，只证 sampled资源；运行时在真实 copy 位置建立
fresh/来源不变量，不能从该 frame 补造历史或放宽混合页。新模式可能对混形 array
首次碰到不同源后回 decoded，需明确适用性可能下降。

旧 Mali ordinaryUID legacy ASTC oracle 1,039,198 checks 实际通过，能复用测试入口；
不能当 ASTC6、新 demote/混形/Storage/原 raw 合同通过。实现后 host LVP/ASan 先核
状态、完整层/mip、旧引用与失效；没有 actual ASTC 支持就明确 unsupported。
后续硬件只用原 GPU fixtures 验证同块保留、不同块/BC/partial/clear/Storage 后全部
层/mip正确，以及旧 GPU view 仍在途的退休；不新写渲染器、计时器或先跑整游戏。
