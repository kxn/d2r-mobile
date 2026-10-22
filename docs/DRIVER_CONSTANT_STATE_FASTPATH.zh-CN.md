# Driver c0/c2 在 key 构造之前复用

基底为 `52181dc6b7cbdb7df7168d62201a84dd27d05672`。此候选只改变 NVN→Vulkan 宿主生成常量表的查找顺序，不修改 Shader、游戏模块、GPU 资源依赖或默认画质。

## 旧路径与边界

现有 `constant()` 每次先读取 S，构造 c0/c2 sparse 或完整 normalized key，随后才尝试当前提交的 last/content cache。现有 last/content cache 和 sparse schema 都保留。

本机保存的原始 `d2r-mobile/d2r-mobile@85133c27f97f81f9ca5ba333975f1c1178675a2f` 的 `nvn_vulkan_uniform.inc` 与 `nvn_vulkan_shader.inc` 显示，原项目在 c0/c2 的 key 构造前调用 `uniform_driver_current()`；其全局 state_valid 随同步批次重置。本候选借鉴提前判定的位置，不复制该全局常量存储生命周期：这里的 last 指针和版本标签属于既有 `vk_shader_submission`，GPU 与 CPU effects 完成后才回收其 arena。

## 完整失效合同

- c0：所有 stage 的 SSBO 地址64位、size低32位；仅 CS 还包括 UBO 同样字段。graphics UBO 不属于 c0。绑定结构体的 stage/index、size高32位不是 GPU ABI 常量字节。
- c2：texture/sampler 前128槽的低32位，image 前8槽的低32位。所有高32位、image8–31原本不进入 c2。
- 版本数组在 S 外，避免 ghost 整体保存/恢复把版本倒退。`vk_state_op` 的实际相关字段变化推进对应 stage/slot。原全量 S 赋值前后通过 `vk_state_restore` 保守失效全部12项。
- production 的全部 S 相关写入点：普通 state_op；ghost 三处整体切换/恢复；bloom shadow 对 live S 的相关绑定、shadow 整体恢复、临时 FS texture 替换及 after_draw 还原。其余同名 S 直接写入只在既有测试中。
- 整体恢复不比较完整 S，而是全部失效；bloom shadow 的 live 绑定也保守失效，避免新增复杂快照。

每次 `constant()` 仍先选择实际 reference、schema 与 range。DSP 的 module authority、uniform alignment、span-reference 和 arena-reference 判断仍在提前命中之前。只允许 non-reference 且 stage/slot 上下文中的 last 存在、版本相同、schema 指针相同、effective range 相同提前返回；schema/module 元数据在 shader 构造后不可变。reference 路径不走新命中。

失效后执行原 key 构造和 last/content cache。原 exact hit/content hit/new copy 发布时记下当前版本；recycle 在原资源释放之前清 last 和版本。新 context 首次没有 last，不能借用另一个在途 context 的表。版本溢出维持 fail-fast，不允许回绕误命中。

## 不可省略的工作

texture view、Pool 解析、CPU publication、GPU writer、布局/descriptor 消费、read/write effects 与 fence 生命周期均位于外围原 binding 路径，本候选不跳过。Shader/schema 相同也不能据此跳过真实内存读取依赖。

## 工作量预算

旧六个886 FP窗的 exact-PC 自身归属中，能直接命名为 sparse key 的可观察子集约0–0.180% NvN 用户期；带 key 或 UBO fallback 的混合 inline 自身约0.089–0.449%，不能全算可省。它们不是整个 key 构造的准确上界（inline 归属有限），也不能与共同 history hash 的1.4–1.8%相加；后者并非 driver-only。更旧0.91–1.24%预算不能当成本次52181的现值。

机制上，只在上述命中时省 sparse-word gather 或完整192/384/1056B normalized key 组装及 last memcmp。MISS、真正状态变化、GPU 常量读、外层资源消费均不省。新增成本是相关绑定操作的字段比较/版本推进、constant 的几个标量判定，以及保守 restore 失效；没有新 cache、分配、开关、原子统计或计时器。

元数据新增全局96B和每 shader submission96B；原最多16上下文时合计上限约1632B。实际 x86 host DWARF 显示 submission46880B、S19800B；S 本身布局未变。新函数/变量 hidden，不增加公共接口。尚未做 Android 实际 ABI/机器码与真机 CPU 验证，不宣称净毫秒/FPS收益。

## 资格与限制

最终源码普通 clang host、ASan+UBSan host 构建通过。12个原GPU用例（LVP、Core+SYNC、公共锁、LP_NUM_THREADS=2）通过：4 cold 内容同步/异步；4原DDK/DSP+PixelLocal开关；4原driver-span同步/异步。这些实际检查整数 graphics/compute c0/c2字节、ignored字段、sparse schema切换、module/range/reference、同提交A/B/A、生产 whole-restore helper、生产 bloom temporary override/after_draw restore、跨提交 pending arena 内容与回收。ghost 的完整游戏筛选流程并未由该宿主 fixture 执行；它的三处生产 S 写入通过同一 restore helper 作源码闭合。

新 bloom 测试入口与整段 driver key fixture/helper/body/dispatch 均以 `DM_VULKAN_TEST_EMBED && !DM_ANDROID_APP` 排除。实际原 host 编译命令加 APP define 的五个相关 TU 预处理已核排除；这不冒充 Android NDK APP 资格。保留首次数组类型选择错误的失败日志及之后所有构建/测试记录。既有 VVL warning 保留，最终12用例没有 Validation Error/SYNC-HAZARD，也没有执行真实游戏模块。

本轮无 Android canonical/RAW、设备访问或 Shader cache 修改。原公共 GPU 资格之外，真实游戏性能及稳定性须由 root 统一合并候选后验证。
