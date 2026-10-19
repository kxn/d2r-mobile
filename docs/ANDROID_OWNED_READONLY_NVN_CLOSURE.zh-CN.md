# 有限自有页 NVN 只读导入：接入闭包设计（未实现）

审计基线：`9f930e94c46b3783378e940fee1c764794371f52`。本文件是下一层设计，不把旧导入原型带回主线；无设备操作，无默认启用，无真实 guest/allocator 接入。

已有证据：独立标准 HOST import + OWNED exclusion 包 `2a86557` 在 Mali 普通 UID 10218、type 2/flags 0xb/atom 64 上通过 98,684 字节/状态断言。它没有执行生产 NVN pool，不足以证明本设计已经正确或有实际游戏收益。旧 mprotect 失败、原字节断言与失败产物保留。

## 1. 首版接入边界

- 仅 owned fixture 显式登记的自有匿名分配。一个 fresh NVN pool 生命周期固定一个完整页窗口；页/atom/导入对齐同时满足，大小受既有 16MiB/32MiB 限额限制。
- 自有证书绑定 pool identity、精确 base/size、固定窗口、代数与唯一持有人；不能从地址、malloc 调用栈或 NVN flags 推断所有权。没有证书的游戏池继续原镜像路径。
- 证书必须在任何资源借用、图像暴露、初次初始化、GPU/CPU effect 前取得。首版可要求 fixture 在 MemoryPoolInitialize 后立即登记，且尚无提交引用。若不能证明历史无暴露则拒绝，不能扫描现存描述符后猜测安全。
- 保留 import 的通道仅完整有界的普通 UBO、VBO 的 GPU 读取。UBO 对齐 copy 是已知的只读源用途，保留原 GPU copy 和依赖，不把它误作 writer。CPU 的真实即时读取允许读取 live guest，但不得使用未更新的 shadow 或负 dirty 证明。
- INDEX、SSBO（包括声称只读的 storage）、texel/image-buffer、indirect、显式 transfer、任何 image/attachment/raw image、GPU writer、CPU effect 目的地在首版均视为不兼容，进入前整窗口一次性退回镜像。后续可单独证明扩充，首版不靠 shader 名称或游戏场景白名单。
- 不允许 CPU 与仍在 GPU 使用的相同字节进行无序覆盖；保留 NVN 原提交/完成契约。不能承诺原镜像偶然提供的额外快照语义。

## 2. 必须先修复的性能合同：范围凭证

旧第一层 `any owned -> global epoch 0` 和 `containing tracked range -> unknown` 对安全是必要的保守实现，但不能直接用于大 pool 的窗口：它会打掉其他 pool 的 image 全局 fast path，也会打掉同 pool 非重叠图像的范围 fast path。

拟新增一个**带精确查询 envelope 的范围凭证**，而不是削弱旧 API：

```
scoped_proof = {slot, lifetime, changes, exclusion_version, query_begin, query_end}
observe(address, size) -> scoped_proof / unknown         // cold, trylock/find
refresh(scoped_proof) -> {global_epoch, range_changes} / unknown // cached slot O(1)
```

- 旧无地址 `nvn_dirty_change_epoch()` 在存在 owned 时继续返回 0；旧无 envelope range proof 对 containing range 继续 unknown，不能让旧调用者意外宣称 clean。
- 新凭证仅对其固定 query 有效。热路径只读取缓存 slot 的 lifetime/live/changes、一个固定窗口的原子边界与 exclusion version；不遍历 registry，不遍历窗口表，不扫描页或内容。
- owned 窗口边界改为原子，增加单调 exclusion version 的发布序列：转换中/奇数/饱和/读取不一致直接 unknown，不能自旋等待。开始、失败回滚、释放均发布版本与原全局/范围变化，防止旧凭证跨转换复活；slot lifetime 防复用，版本饱和永久 unknown。
- 对与完整 owned 页区间不相交的 query，允许返回原 g_changes 与 range changes；owned 页上的不受保护 CPU 写不影响这个 query。相交的 query 必须 unknown/live。以页为边界，同一页中“不同字节”仍算相交。
- image 的全局 epoch 直接返回分支必须先消费有效的 scoped 凭证；不能先按旧 global epoch 返回，再检查重叠。非导入实验模式保留原分支，不为默认路径新加每 draw 调用。
- `dirty_since` 的 latest 早退前只判断查询与固定窗口的交集；非重叠用原代数判断，重叠返回 dirty。`dirty_visit` 只对相交页无条件访问，其他页用原 gen，不再把整 range 的 stamp 清零。protect 仍跳过 OWNED 页。
- 释放只在 GPU 完成、引用/效果/借用退出且 import 销毁后进行，随后原 release 发布页/范围/全局代数，恢复原 mprotect。

成本不是零：新凭证按字段估计 48B，替换原 24B range proof 时每 image 约 +24B；每 tracked range 增加 8B exclusion version（4096 槽约 +32KiB），窗口边界原子化不增加尺寸。实验启用时每 image 增加固定次数原子读，转换后冷失效一次；每实际导入读取仍需按 atom Flush。第一层原有约 96KiB 增量仍存在。必须以编译 `sizeof` 和现有 Profile 复核，不承诺这些成本低于省掉的拷贝。

## 3. 已有源入口与所需闭包

| 入口（基线行号） | 当前语义 / 不能漏掉的情况 | 设计处理位置 |
|---|---|---|
| nvn_vk_pool_policy.inc:161 `vk_pool_operation_begin`，177 fast pin-return | 已有 backing 可不经 reserve 就借用 | 在任何 pin/return 前分类并处理不兼容窗口；reserve 后重查 identity/descriptor epoch；不能仅挂慢路 |
| 同文件 12 `pool_operation_add` | 仅用于 reservation 的重复 address/size/prefix 会丢后续 writable/kind | 单独合并访问位或先处理语义；任何 writer/不兼容 kind 优先。不能拿首项 UBO 给后项 SSBO 背书 |
| shader_pool_ranges.inc:15/85/152 | UBO/SSBO/texel、VBO/IBO、图像来源分开枚举；后续 texel shader alternative 可改变物理表述 | 复用一次逻辑收集，分类所有实际候选；未知/alternative 首版退回。完整 fallback 是 writable whole-registry，先 demote 再 pin |
| nvn_vk_draw.c:998/1000；shader.c:1671 | draw/dispatch 前图像预备仅在 defer-publication 开时执行 | 新 owned 模式也必须逻辑预备，在 shader/texture/view/temp 借用前；不能依赖旧开关默认 |
| texture.c:681；2037/2049 | CPU image preflight；cached trivial recipe 可以直接返回 | 所有 sampled/storage/attachment/raw image 的逻辑来源先过 owned 检查；不能把 demote 放到深处 image_get 才等待 |
| texture.c:1215/1221/1269/1306 | B2T 只读 pool源、T2B writer、T2T 双图像；BC CPU 解码直接读 source | 首版所有 transfer/image 相交窗口 sticky demote；源/目的都覆盖，包含完整 row/slice footprint |
| texture.c:2651；texel_storage.inc:121 | 普通 texel view / storage 替代 descriptor，可能历史 buffer view 借用 | 首版不兼容；逻辑选择前置，内层只断言无 imported 重叠，不替换已经借出的 handle |
| replay.c:89 OP_UPDATE_UB | GPU 精确目的写，known temporary CPU publication | operation begin 前 demote 目的窗口；深处 writer 仅 assert，无 wait |
| nvn_vk.c:3179 `mtl_sub_copy`；3317 | B2B 除 GPU copy 外，known-copy 在 commit 从 source shadow 获取快照 | 首版源/目的任一重叠均先 demote；将来允许 source import 时必须禁用 shadow-known 捷径或单独证明快照来源 |
| shader.c:282/1280/1342 | SSBO/对齐 copyback writer 和 prospective writeback | 全部由完整 preborrow plan 先 demote；writeback/dependency 内层 fail-closed 断言，不临时等待/回收 |
| nvn_vk.c:3009/3023/3227；counter.c:252 | completion-copy/event/query 目的可为 pool页，真正写发生在 worker | 在录制该效果前由 translation owner 处理；completion worker 绝不等待/退 import，不在 fault hook 做GPU操作 |
| hle_nvn.c:1085、cpu_demand.inc:10/48 | TextureInitialize、ReadTexels 目的、EventInitialize、ReportCounter、UpdateUB CPU源、Finalize 已有 x_cpu_exposure；但 defer-publication OFF 会立即 return0 | owned 模式必须有独立 needed 判断和原 owner job；重叠时在HLE继续CPU使用/移除前同步完成 demote。不能只改 driver branch |
| hle_nvn.c:227 | 先 remove dirty range，再 mtl_pool_del | 现有 x_cpu_exposure 必须保证 import 已退/凭证已释放才允许 remove；仅在 mtl_pool_del 补会太晚 |
| nvn_vk.c:1700/1775/1780/1807 | retained lookup与普通 lookup、地址-only、overwrite | 必须只消费已分类/已pin operation；缺失/未知的 import lookup 直接断言。绝不在返回 buffer/map 后变更物理映射 |
| nvn_vk.c:1810/1840/1853；runtime.c:154 | CPU即时读、optional proof、camera常量读 | 无 GPU writer 的 imported 源可读 live guest；任何 generation/shadow clean cache 禁用。带 CPU effect/unknown authority 必须前置退回 |
| nvn_vk.c:1509 | 初次初始化同时复制 mirror/shadow、写 initialized bits | imported 页只完成必要物理ready/引用元数据，不能 memcpy 到同一 host alias、不能把未同步 shadow 标成新鲜；普通页原路径不变 |
| nvn_vk.c:2801/2823/3309 | used run 可能跨普通/导入 backing；commit 最后上传 | 按既有物理节点边界分段，imported 子段原 Flush，每次都做，不依赖 dirty代数；普通子段原 protect/scan/upload；不新增每draw全表扫 |
| pool_migration.inc:82/150/163 | migration复制旧 GPU initialized bytes，最后销毁旧节点 | 任何包含 import 的组件先在外层 demote，再用原迁移；不得直接 free token，也不得由 GPU copy 保存不一致 shadow |
| nvn_vk.c:1044/1080/1609；pool_spans.inc:111 | fence后仍有CPU效果、views/CB引用；pool删除等references | 回收必须经过原 fence→effects/callbacks→references释放→CB/view retirement，不能仅见 fence signal 就释放 import/owned证书 |

`vk_pool_span_query` 的生产调用仅用于 address-only VBO 的逻辑规划（shader_pool_ranges.inc:107），未发现其他生产 raw VkBuffer escape；该未知长度情况首版不导入，whole fallback 提前退回。任何新增出口应继续纳入检查。

图像历史别名也必须闭合：fresh 自有证书之前若已有 image/raw exposure 则拒绝；证书固定窗口之后，对相交图像创建/借用做 sticky rejection/demotion，非重叠图像正常保留 scoped proof。不靠扫描当前 descriptor 表猜测“没有历史 raw handle”，也不新增每draw registry 全扫描。

父 operation 必须包括待 replay ghost 的资源；已有 `vk_ghost_pool_ranges` 只解决 buffer admission，图像源也须预先处理。嵌套时发现遗漏不能擅自结束父 borrow；owned fixture 第一批不启用这些可选 replay，生产闭包未补齐前仍不接游戏。

## 4. 状态机与回退

```
NONE -> CERTIFIED_FRESH -> CLAIMED_RW -> IMPORTED_READONLY -> RETIRING -> MIRROR_STICKY
                              \-> import failure -> release token -> MIRROR_STICKY
```

1. 证书只由 owned fixture 创建。claim 仅原 PG_NONE 页；PROT/BUSY 拒绝并回滚，不解保护、不 GUP 重试，不留半成功节点。所有否证对该窗口/该 pool lifetime sticky。
2. 只有取得 exclusion、标准 external properties/host type/内存导入/map/bind 全部成功后才发布不可变 backing 与 generation；任一失败先销毁私有 Vk 对象/退 pin，再 release，保留原镜像路径。
3. 真实读取保留原 hazard、stage页图、submission引用。commit 对实际访问范围执行原 atom对齐 Flush，不假设脏页代数意味着已Flush；同 atom 邻居不得有 GPU writer。不能省去队列顺序、fence、CPU→GPU 域操作。
4. 首次不兼容用途在**借用前**将窗口置 RETIRING/sticky。可提前分配普通 backing 以免 OOM 后错误撤销旧状态；然后使用原维护边界结束/提交已有工作，等相关引用、效果、views/CB 全部退回。不能在 per-draw深层、completion worker 或 fault hook 插入等待。
5. 因 imported 窗口从未有 GPU writer，合规 CPU 内存是该窗口内容来源。在 publication exclusion 内复制到新 mirror 和 shadow，按原 initialized/page stamp 规则标记；窗口相关 old shadow/known-copy 证明不沿用。切换 backing generation/registry epoch/buffer epoch、清 resolver hint/descriptor缓存身份，保持其他节点不变。
6. 销毁旧 import 后 release exclusion，发布新的 dirty代数，再允许原保护；demotion 后以后不重试 import。任意 GPU writer 从新 mirror 执行，后续 GPU权威/写回继续原逻辑。
7. 若不能证明 owner/identity/引用已排空，或 allocation/import/释放异常，保留旧存储并返回错误/失败，不得执行 writer、不把旧 shadow 当正确回退，不通过加一层深 wait 掩盖遗漏。

冷路径的一次性 drain/copy 确有代价，不能写成免费；sticky lifetime 防止每 draw 反复 import/demote。后续若实测转换太密，应该拒绝该类候选，而不是把 wait 藏到提交内。

## 5. 下一组最小 owned-NVN 验收（尚未写/未跑）

- 先仅做 scoped API 单元负例：同一个大 tracked range 的 owned窗口 / 同页邻居 / 另一页纹理 / 另一pool。普通页负证明仍可刷新，只有重叠unknown；普通页真实fault必须失效；开始/失败回滚/释放/slot复用/饱和/转换竞态都不能返回错clean。
- 再复用原 NVN UBO/VBO 小场景与字节/像素 oracle，显式 owned证书只针对测试分配。原非导入纹理与 imported UBO/VBO 位于同pool的不同页，证明非重叠纹理不反复上传（现有计数器/实际字节，不造timer/sampler）。
- 覆盖同址读后写去重、不同range别名、image registration/attachment/transfer、CPU event/query/readtexels 目的、unknown fallback、late shader alternative、retained lookup、跨提交、多pending、迁移、Finalize。每条都须在借用前 sticky demote；纯borrow期间错误入口应负例断言，不自动深wait。
- demotion 后真实 writer结果和 shadow/new mirror一致，后续原 dirty保护/CPU改写/upload恢复；失败 import/分配不残留 token/backing，原断言不弱化。
- 宿主官方 VVL/sync/API29 NDK 后才申请独立包真机小页测试；不重跑历史被拒 guest mprotect 项，不运行游戏，不接全部 malloc。

当前结论：范围凭证和 preborrow 入口可做受限实现；生产闭包尚未闭合，不能仅把标准 fixture 成功接到任意大 pool。尤其 HLE exposure 的原开关、plan 去重访问语义、known shadow-copy 与图片/ghost 逻辑前置必须一起处理，遗漏任一条都不启用。
