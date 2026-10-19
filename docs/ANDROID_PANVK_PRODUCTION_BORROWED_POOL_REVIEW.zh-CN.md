# PanVK 生产接入：allocation 所有权与 NVN Pool 借用期分开论证

基线 `0104c1b4157e0112157b009c826a51e08d5d49e8`。只读审计源码及现有日志；没有改主线、分配器、游戏 binary 或设备。此前独立 AppUID10279 的64KiB原NVN功能验收保留，不能扩写成游戏已零拷贝。

**更正先前过强的限制：不知道 malloc 来自哪个分配器，不是 Vulkan 导入天然不可能的理由。** 自有 allocation token 是当前 fixture 为隔离风险采用的更强前提，而非外部 HOST 内存接口的规范要求。生产可接入性应考察借用的内存范围、有效期、访问权和引用闭包，而不是强求把游戏所有 malloc 都改成我们分配。

## 规范事实、当前代码、尚未证明

Vulkan 的 [VkImportMemoryHostPointerInfoEXT](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryHostPointerInfoEXT.html) 允许应用继续通过原 host pointer 访问，并要求应用同步访问、在导入对象有效期内保持范围有效且可访问。它没有要求原存储由本后端 malloc 创建，也不能假定导入对象自动保活所有外部存储。对齐、类型支持、范围和平台拒绝仍按该接口检查。这个事实支持借用 lease，并不替代 NVN 生命周期前提。

当前 NVN 源码 `MemoryPoolBuilderSetStorage` 收到既有 ptr/size，`MemoryPoolInitialize` 保留该 ptr，`GetBufferAddress` 返回同一地址。上游 Metal 直接以 page-rounded pointer 建 Shared NoCopy Buffer、`deallocator:nil`，Finalize 后再按原提交完成退役对象。它没有 allocation-origin token。这是本项目已采用的**借用实现事实**。

本次没有拿到可公开核查的 Nintendo 完整 NVN 生命周期规范；仓库的精简 `nvn.h/protos` 不含 storage-valid-until-Finalize 的完整说明。因此不能把“合法应用必须在 Pool 使用期保持存储有效”伪装成已经引用到的 Nintendo 条款。它是与现有 HLE/Metal 实现一致的接口契约前提；实际 SDK 规范确认与游戏合法使用仍须分开写。若该契约成立且关闭过程覆盖所有 queued/recorded/inflight 引用，Pool Initialize→Finalize 的借用期可以承担 import 的范围有效期，**不必另外证明 malloc 来源**。

| 项目 | 当前0104已证明 | 仍缺什么 |
|---|---|---|
| ptr与范围 | 原ptr稳定传递，注册表拒绝逻辑重叠/溢出，pool identity区分生命周期 | 不是物理VMA/COW稳定性证明；不是malloc所有权证明 |
| 借用结束 | 原Finalize调用CPU exposure→dirty remove→pool del；后者finish并等资源refs | 普通分支exposure可直接跳过；新import必须先发布closing、无条件串行结束该候选lease，关闭与新import并发不能漏 |
| readonly | 当前operation收集UBO/VBO/SSBO/indirect等实际范围与writer位 | 没有永久Buffer-only Pool属性；一次UBO bind不证明其他范围/未来用途；应是可撤销的窗口许可 |
| 图像别名 | TextureInitialize能算出mem/layoutsize，并有原CPU exposure入口 | 全部历史/延迟logical image、buffer texture、snapshot/raw escape须从cold startup登记；现exposure只用于部分CPU_NO_ACCESS池，不等于完整alias历史 |
| 页保护 | dirty模块管理NONE/BUSY/PROT；独立owned排除/Scoped API已有实机/host证据 | 当前0104没有生产borrowed排除协议；其他HLE heap/alias权限变化也没有与import协调 |
| 生命周期引用 | 原submission pool refs、CB reset、CPU effects、完成worker是现有基础 | 需覆盖未翻译/已录制地址及并发Finalize，不能仅看已提交GPU fence |
| 同步/缓存 | 原队列、效果发布和writer依赖存在；独立0xb测试证明Flush/退回的受限合同 | 不因此允许并发同byteCPU/GPU覆盖；共享GPU writer未实现，正常不确定用途必须提前退回 |

不能将“上层非法提前释放”与“后端自己破坏借用”混为一谈。若NVN契约明确禁止Pool还在用时free/move backing，后端不必拦全局malloc来容忍此类非法调用。但后端自己的 `nvn_dirty_protect` 是合法应用看不见的额外mprotect；它已在旧试验出现CPU新字节/GPU旧字节，必须有排除/恢复协议。另 `hle_svc.c` 的heap shrink、Map/UnmapMemory、Map/UnmapPhysicalMemory是不同路径；若它们合法影响仍被借用的候选范围，需要先结束lease或拒绝候选，不能靠Pool identity假定物理页永不变。本次不运行任何guest固定地址诊断，也不改这些路径。

## 真实分配路径盘点

| 路径 | 可以严格说什么 | 对本路线的意义 |
|---|---|---|
| `vk_buffer_create`→VMA→VkMapMemory | 宿主创建并持有真实Vulkan分配，map/handle/lifetime明确 | 已经CPU直接生成GPU可读内容；再次HOST import没有guest mirror可省 |
| support/zero/c1 constants、driver arena | 直接写上述mapped Buffer，GPU只读，提交资源保活 | 是真实生产正例，但已经是现成零拷贝/子分配路径，不是待导入guest候选 |
| temp upload/readback | 明确Vulkan对象，但用途可能是transfer目的/GPU写回 | 已有适合的map/缓存维护；不是本只读import入口 |
| NVN Pool SetStorage | 明确借入ptr/len、可绑定按范围的pool lease | **可考虑的生产入口**，不是因缺malloc token而绝对排除；目前条件/alias/closing闭包仍不完整 |
| `nv::SetGraphicsAllocator` | 只保存游戏allocator回调，没有实际pool storage工厂调用 | 不是已存在的GPU-first返回指针边界，不能推断替换后所有分配用途 |
| `StandardAllocator::Allocate` | 调原SDK分配器，只补OOM日志；通用堆 | 不以此拦所有malloc，不按调用栈/大小分类图形数据 |
| SVC heap/alias/stack | 宿主控制大范围地址/保护与映射登记 | 大范围宿主mmap不等于其中所有页是纯GPU只读，更不等于可pin整堆 |
| hostseg、snapshot、线程/解码scratch | 宿主内存可以定位，但CPU元数据/通用数据或无NVNGPU用途证明 | 不因“由宿主分配”就导入，不能省掉不存在的GPU镜像 |

0104现有普通native.log有7次SetStorage/PoolInitialize的启动调用计数，但没有逐Pool ptr/extent/用途参数记录；没有allocation token或能匹配其producer/free的数据。current1502已有官方API库存中，419430400B Buffer同时出现UBO、SSBO、VBO、IBO、copy，另744488960B Buffer是SSBO/VBO/IBO/copy混用。这只能否定把整个大Buffer当纯UBO的推断，不能证明每个对齐内部窗口都不合格，也不能把Vulkan object ID直接当guest地址。已知7Pool/注册MiB是地址extent，不是独占/RSS或可导入容量。

## 最小可证方案：Pool-borrow lease，而非malloc重定向

此为设计，**不放松现有owned API或实现入口**。

1. 冷启动实验开关先于首个Pool生效。由原Pool registry登记 `(base,size,pool_identity)` 的borrow lease，metadata只是借用契约凭证，不宣称allocator ownership。无法确认合法借用有效期或平台范围可访问，就不导入。
2. 只考虑整个候选window及其所有system pages/cache atoms均在该Pool内部、满足HOST import对齐的范围；不向Pool外取整。按原dirty页状态预约PG_NONE，遇PROT/BUSY不解保护、不GUP重试。使用标准类型/能力检查，失败sticky回原mirror。
3. 由原NVN逻辑创建入口记录排除区间：全部TextureInitialize（含buffer/texel）、event/query/CPUeffect、不可分类raw escape，以及实际buffer writer。先从一开始就保留sticky历史；已有image-only pool虽然没有VkBuffer/shadow，也不能被“fresh backing”当成从没用过。
4. 首次实际只读UBO/VBO operation才尝试有界线性window。复用现有pool解析/pin，不再每draw全表扫描。相同地址去重保留writer/unknown并集。历史不合格窗口不试；合格仅获**当前可撤销只读许可**，不要求未来永远只读。
5. 后续任何图像/texel/未知访问或GPUwriter先在外层撤销许可、结束旧GPU/CB/effects引用、重建mirror+shadow、unpin后恢复dirty保护，再执行原操作。不能在descriptor/pool_address/完成worker内部补深层等待。history+registry版本让稳定非重叠图像保持O(1)准入/Scoped proof；原无地址global证明仍保守。
6. Finalize首先把lease置closing，禁止新import/borrow；用原translation队列的owner job按准确identity关闭，覆盖“刚才看未import、随后别线程开始import”的竞态。CPU dirty range remove及storage归还必须在unpin/exclusion-release之后。候选是否已import不能作为跳过关闭job的唯一条件。
7. 实施前最小验证是**两个角色的owned借用实验**：存储由独立调用方创建，后端仅拿Pool借用；地址不变，晚image/queuedwriter/并发closing/slot复用/范围边界分别验证，外部存储仍由调用方在Finalize返回后释放。它可以验证借用状态机，却仍不能代替真实游戏与所有SDK VMA模式验收。当前不申请设备、不执行该实验。

真正待补的是合法NVN借用期证据、按窗口动态alias/history和closing/页保护闭包。独占allocation token可以作为一种更强选项，但不应成为唯一技术路线或把它的缺失描述成Vulkan规范硬限制。暂时不能枚举出已严格合格的实际游戏window；这不等于所有window都不能共享。
