# PanVK borrowed NVN Pool 最小可执行闭环（独立实验）

基于 `0104c1b4157e0112157b009c826a51e08d5d49e8`，沿用已独审的 scoped dirty proof、owned NVN Buffer 原型及 setter 修复。**不合 main、不默认启用、不接任意 malloc，不修改游戏模块。** 本轮只在 host GPU 和独立自有存储夹具中执行原 NVN 入口；没有访问手机。

## 不再把 malloc 来源当成必要条件

外部 HOST_ALLOCATION 导入需要的是对齐、有效且持续可访问的 payload，以及合法同步，并不要求 payload 是 Vulkan 后端调用 malloc 获得的。[Vulkan 导入契约](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryHostPointerInfoEXT.html)。原 Metal 的 no-copy buffer 也不拥有 Pool payload 的 allocator/deallocator。

本实验以原 `MemoryPoolInitialize` 到 `Finalize` 的**借用存储有效期**为前提：合法调用方保留整个 mapping，不提前 free/munmap、不在 GPU 尚读同一内容时改写该内容。这里没有引用已核实的完整 Nintendo NVN 存储寿命规范；这是与本项目 HLE/Metal 行为一致的调用方契约前提，不能用测试替代对任意游戏分配器的证明。

原 `nvn_dirty_owned_exclude()` 的 self-owned 前提和旧夹具保持原样。新的 `nvn_dirty_pool_lease_observe/exclude()` 独立匹配原 dirty range 的 slot、lifetime、完整首尾，复用原 PG_NONE/BUSY/PROT/OWNED 排除和恢复协议。没有把一个地址强称为后端自己拥有的 malloc。

## 实际接入点

| 步骤 | 生产入口与行为 |
|---|---|
| 登记 | 原 `nvnMemoryPoolInitialize` 在 `mtl_pool_add_flags`、`nvn_dirty_add_range` 之后捕获 lease。只允许完整 64KiB、64KiB 对齐、flags 恰为 CPU_CACHED\|GPU_CACHED 的原 NVN Pool。不是显式测试 attach。 |
| 历史用途 | 原 TextureInitialize（含 BUFFER/texel target）、EventInitialize、ReportCounter、ReadTexels 的 CPU 目的范围、UpdateUniformBuffer 的 CPU 源、AddCommandMemory 在首次使用前保守登记 sticky history。对象随后 Finalize 或命令未提交都不清历史。 |
| 首次准入 | 原 operation resolver 已查到 owner 但缺 physical span 的冷路径，检查该已有 bounded plan 的全部重叠要求。仅 UBO/VBO 只读可尝试；跨范围/未知用途/writer 一律保守。旧 backing、shadow、引用存在时不替换。 |
| 导入 | 先排除原 dirty mprotect，再标准 EXT HOST_ALLOCATION 导入，map 保持到 free。原 descriptor/hazard/used-page 路径仍执行，不跳过依赖。初始 PG_PROT/BUSY、别名 dirty range 或不匹配 lifetime 拒绝，不解保护重试。NVN 每 lease 只尝试一次。 |
| CPU 修改 | 排除页的 old global/range proof 保守 unknown；renderer 使用先前已独审的 fixed-envelope scoped proof，保留其他普通纹理的快速证明。合法 CPU 修改可原 write_prepare 或直接写；非 coherent 在原提交前按实际读取页/atom Flush。只读不需要 Invalidate。 |
| writer/alias/CPUeffect | 原外层 operation 或 HLE CPU exposure 先排空原已记录/提交 GPU 工作及完成副作用，确认 refs=0；重建镜像/shadow，换物理 generation，销毁 import/unpin，最后释放排除。该 lease 不再导入。没有在持有 descriptor/operation borrow 的深层做等待。 |
| Finalize | 原调用先在资源锁下置 closing，阻止晚到的首次导入。无论当时有没有 import，都通过原翻译队列运行 owner-close；随后原 dirty remove、Pool delete 才发生。不会用一次“当前没有 import”查询作为放行依据。 |

`hle_svc.c` 的原 SetHeapSize、MapMemory/UnmapMemory、MapPhysicalMemory/UnmapPhysicalMemory 在实际页权限/搬移前接同一保守撤销 hook。夹具只在自己的普通地址上测试该 hook 和合法 mapping 改变；没有执行 guest 固定地址空间/SVC 诊断。该 hook 不是任意宿主 munmap/free 拦截器；非法提前释放 live Pool 仍违反借用前提。

## 默认与成本

- 编译 `DM_VULKAN_OWNED_NVN_FIXTURE=ON` 和 `DM_VULKAN_BORROWED_NVN_FIXTURE=ON` 都必须显式指定；两者默认 OFF。运行还须在首次 Vulkan 初始化前选择 `DM_BORROWED_NVN_BUFFER_FIXTURE=1`，只存在测试入口，没有产品 UI/游戏默认 driver 开关。
- 已有 backing 的 steady-state draw 不增加额外 Pool 查找或全注册表扫描。新资格检查只在原 span miss，复用该 owner 和 operation plan。实验计划每次增加一个 8B identity 清零；plan 额外 24B。
- GDB/DWARF：本树 ON 的 `pool=312B`、plan=196672B；先前 owned-only `pool=272B`、plan=196648B，因此借用层额外 **40B/Pool、24B/plan**。本树 compiled-OFF 的 pool=256B（owned Buffer 的字段也不在）。dirty range 仍 104B，没有额外每页表。原 scoped proof 成本没有消失。
- slow lifecycle/history 按原少量 Pool registry 做保守 overlap 查找。首次导入增加一次 Vulkan/kbase 注册/map 与固定页状态排除；非 coherent 每次真实提交读仍有 atom Flush，不能省同步。
- 成功导入省掉该物理 VkBuffer 镜像分配，以及读取时的 dirty scan/compare/CPU→mirror copy；**本版仍保留 64KiB shadow 的预留/元数据分配**，不声称所有额外 CPU 内存已经消除。shadow 不作为 active imported 内容的正确性证明。
- 退回是低频慢路径：原 quiescent drain、镜像分配、live CPU 内容重建镜像/shadow、unpin/恢复；不能把这些成本忽略。没有增加全局每帧等待或 ALL_COMMANDS barrier。

## 功能验收与边界

本轮官方 LVP `llvmpipe LLVM19.1.7`，Khronos validation + sync validation，原异步翻译/完成线程：

- borrowed 原 NVN fixture：**9054 checks**，真实 UBO shader/DrawArrays/readback pixels，CPU prepare/direct writes，同一导入复用、writer 外层退回、历史普通 image/buffer-texture/event/command-memory 拒绝、late image、初始 PROT 零 GUP且不重试、Finalize 与首次 import 的确定性并发、同地址/slot 新 lease、VMA hook 退回。
- dirty lease/原 owned/scoped unit：**197 checks**，包括真实 PG_BUSY protector 与前缀 rollback、非法范围/slot/lifetime、ABA；原两项预期 abort 仍保留。
- 原 owned NVN fixture 的 **2371 checks** 保留通过。
- 两编译 gate ON、运行不选实验：原完整 suite **22035661 checks** 通过。
- 两编译 gate OFF：原 memory visibility **447763 checks** 通过。
- NDK 28.2/API29/ARM64：原 `native_vulkan_test` 和完整 `libdmhost_android.so` 编译/链接通过，不执行游戏模块。

LVP 实际 memory flags 是 **0xf/coherent**；本轮不据此宣称 Mali noncoherent/ART signal chain/普通 appUID 通过。旧的独立 owned Mali fixture 已在 0xb 上通过 2371 项，旧 import→mprotect→CPU 改写后 GPU stale 的原失败也完整保留；两者都不能替代**本次 borrowed** 真机验收。

范围刻意限于一个完整小 Pool。大 Pool 的内部纯 UBO/VBO 窗口、多 import node、跨窗口/跨 Pool alias、借用存储早于 Pool 有效期失效的实际游戏行为均未获生产验收。现有调用日志缺每个 Pool 的地址/extent，不能声称游戏存在或不存在符合这 64KiB 条件的 Pool。没有真实游戏零拷贝、FPS、带宽或发热收益结论。

新 unit 首次把 borrowed 检查放在故意耗尽 dirty 时钟的旧 scoped 负例之后而失败；保留原失败日志，调整测试顺序至耗尽负例之前，没有重置或放松生产时钟/断言。首次缺系统头构建失败以及误用上次可执行文件的输出也保留，均不计入验收。
