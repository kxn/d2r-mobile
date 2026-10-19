# dodola fork 与当前 Android 后端的优化差异

核查日期：2026-10-18。**大部分主要优化双方已经具备；没有发现一项尚未移植、且已有同条件证据能把我们直接带到60FPS的通用优化。** 真正值得继续评估的是分层脏页查询；另有几个明确的兼容性边界，适合小范围补齐。不要据此替换当前已验证的同步和资源架构。

## 固定比较对象

- 外部：实际浏览 [dodola/d2r-mobile](https://github.com/dodola/d2r-mobile)，独立 clone 后逐项读运行时代码，固定 [main `dd0e3c764fe2f797794eb7431ea0af9d81754c5f`](https://github.com/dodola/d2r-mobile/tree/dd0e3c764fe2f797794eb7431ea0af9d81754c5f)，截至此点86个提交。克隆在 `.local/research/dodola-audit-20261018`，没有改本工作树的 remote、分支或源码。
- 我方已测基线：APK **`5e8f874af906ef43506f94d1cca43cc93bd51b91d4b84f9bedf40eeeedb16418`**，native **`a4fbc91863852e3b120f45c5c395b6d337225c86cdeee40de8d542314f1c724d`**，冻结源码 [writable-schema-v1-candidate/sources](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources)。以下“我方”均按这份快照查验，**不是5e38/WSI旧候选，也不是工作树新NEON代码**。
- `7c83112ee69d88bd9087435813df70ef7523bb5093f05696364823fd399fd177` 在上述基线上增加了默认关闭的 hybrid specialization；不把它尚未完成的原游戏性能实验算作已测收益。
- 本审计没有运行外部脚本、构建、ADB或真机采集。外部测试结果是作者报告，源码结构是本次独立核对；这两类证据分开。

## 已经有同等能力，或我方范围更细的部分

| 项目及外部提交 | 实际外部实现 | 我方5e8实现／差异 | 结论 |
| --- | --- | --- | --- |
| 同步、提前提交、异步完成：[7ccbb3f](https://github.com/dodola/d2r-mobile/commit/7ccbb3f38402549be7d00f854ac61aee26de0ac9) | 四类待处理stage/access集合；附件生产者可只阻塞后续fragment侧；达到默认48draw后，仅在pass/query外早提交；两个submission槽＋完成worker | [buffer依赖](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk.c:1571)、[逐aspect/mip/layer图像依赖](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_image_dependencies.inc:65)、[分块](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk.c:346)、[完成worker](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk.c:796)均已具备；buffer还核物理对象及generation | **同等目的，我方粒度更细**。外部四类集合不能称为逐资源hazard，也不应拿其函数名推断更先进 |
| Image usage＋首次storage升级：[4375a3f](https://github.com/dodola/d2r-mobile/commit/4375a3fe035e8e44563cd3e6788126aac30672fc) | 无IMAGE提示不预加STORAGE；首次绑定时新建图像、全mip/layer复制，旧分配保留到纹理Finalize后drain | 我方[实际用途与升级](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_texture.c:562)已存在，还区分实际attachment、mutable view需求；按提交引用退休旧对象 | **已实现**。不能把对方17.53%当作我方尚可获得的新增空间 |
| 跨提交view/pass/framebuffer缓存：[e123a30](https://github.com/dodola/d2r-mobile/commit/e123a30f08bb9f192827e14f99eaca82e7ae6d02) | 每槽保留不可变对象；满阈值在安全begin回收，销毁纹理前drain两槽 | 我方[render对象缓存](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_render_objects.inc:1)完整key、hash索引、引用计数及view退休失效；不要求所有对象按固定两槽全drain | **已实现，生命周期更细** |
| Dirty upload、GPU-owned读、cached writeback、临时buffer | 精确range上传缓存＋页代数；GPU写覆盖时跳上传是默认关闭实验；cached staging按fence后invalidate；临时buffer回收 | 我方[使用/覆盖页分离](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk.c:632)、[逐页上传代数](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk.c:2254)、[写回](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk.c:2547)已有；CPU-known内容还可免GPU staging，不会把旧CPU数据覆盖GPU结果；VMA负责分配 | **四类已有**。新的真正差距是下一节的块级脏页查询，不是“还没用mprotect” |
| per-mip布局：[84e6146](https://github.com/dodola/d2r-mobile/commit/84e614658a425c3d32ef0529eaa62e473883430f) | 窄场景按mip布局，复杂用途复制回退 | 我方[mip/数组策略](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_texture.c:204)还包含有界sample-only数组、深度只读与存储/transfer pin回退 | **已有且覆盖更多** |
| 清屏合并／丢弃死store：[be0188b](https://github.com/dodola/d2r-mobile/commit/be0188b831678ca9119ddede8d00ab07262ef9fa) | 有界前瞻到完整clear，保守遇draw/dispatch等退出；未实现shader反射的首draw全覆盖证明 | 我方[延迟pass与store规则](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_draw.c:460)、[首draw覆盖](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_cover.inc:1)已存在；支持的设备上只读DS还用STORE_OP_NONE | **已有，我方更多证明路径**。逻辑省读写字节不能冒充DRAM |
| shader control、binding plan、descriptor和PSO命中 | 2-way control缓存仍比较完整0x880；binding plan命中后重查当前资源；状态/PSO last cache默认实验 | 我方[descriptor内容历史](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_shader.c:162)、immutable shader对象、prepared PSO、dynamic UBO与driver常量复用均已有；命中不省资源依赖 | **已实现相同方向**，不能仅看到对方“缓存”二字就再加一层 |
| 持久化PSO：[b8da25f](https://github.com/dodola/d2r-mobile/commit/b8da25f142299a6243e1d3ce4d5f944e7fb010a4) | 有界文件、身份校验、坏缓存回退、原子替换；在渲染锁内取snapshot，worker只写文件 | 我方[缓存加载/worker](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_pipeline_cache.c:57)也已有；snapshot在独立worker，专用锁串行化cache的Vulkan访问 | **已实现**；对方没有完成通用预热系统，不能把文档建议当已交付代码 |

我方当前还有原生ASTC格式路径、完整加工/部署哈希链和已有Uniform特化。外部这份Vulkan像素format映射以BC解压和有限格式为主，未见ASTC映射；不能把原Metal的ASTC能力误写成其Android已具备。也未见它交付了新的混合精度主线；只读SSBO标记、`spirv-opt -O`、强制EarlyFragmentTests均是保留证据、未采用的实验。

## 再核GPU等待与通道往返

没有看到被我们遗漏的更强并行或flush策略。外部运行时实际是：四个stage/access桶最终发全局memory barrier；early submit保持已有pass边界；完成worker等fence后仍需持全局G.lock发布副作用，与记录互斥；store前瞻只保守证明后续完整clear。它没有额外的并行GPU队列、通用subpass/input-attachment融合或已交付的通道重排序。相反，我方已具备按物理buffer／图像子资源追依赖、真实query/event生命周期、首draw全覆盖与精确mip不交证明、先完成副作用再通知等待者且driver reset不持资源锁。这些是源码边界对比，不能据此声称我方整帧一定更快。

所以，不能把已有三项同步方案再换成外部四类桶，也没有理由据此次审计放弃当前HDR与CPU热点主线。下列候选按当前证据均属于**P2：独立减负或兼容性补强**；没有发现应立即插队的新增P1 GPU优化。

## 真正值得借鉴或补齐的地方

### 1. 分层脏页代数查询：通用CPU候选，P2

外部 [nvn_dirty_summary.h:27](https://github.com/dodola/d2r-mobile/blob/dd0e3c764fe2f797794eb7431ea0af9d81754c5f/dm/gfx/nvn/nvn_dirty_summary.h#L27) 每64页维护一个单调generation上界。上界不比上传stamp新时跳过整个块；有新写入则回到原精确页检查。它不缓存某次查询答案，也不从GPU完成号推断CPU未写。

我方 [nvn_dirty_since:208](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/nvn/nvn_dirty.c:208) 已有整个range的`latest`快拒绝，但同range其它地方有写入后，未变化的较大子范围仍可能逐页扫描。块上界可以补这段空隙；不应把已有range级快拒绝重新实现一遍。

成立条件与代价必须一并保留：

- 在实际range仍完整跟踪、stamp/地址合法时才使用，部分首尾块也只作“可跳过”证明；不确定则精确扫描。
- **先发布块及range上界，再发布页generation**。所有写故障、宿主write-prepare、GPU回填和range重用都须覆盖，不能只改查询侧。
- 写侧增加原子max/CAS、块数组和cacheline竞争；原range `latest`已有时可避免重复一次全局max。小范围继续直接扫，防止额外索引开销高于收益。
- 外部此策略默认关闭，但这不证明机制无价值。其合成测试有page读数减少，不是游戏涨帧证据。
- 当前5e8 Mali的官方CPU归因中，`nvn_dirty_since` self为约 **0.3508% instructions / 0.7997% cycles**，各自event分母，非毫秒或可预测提速上限；见 [官方基线分析](/home/dl/d2r-mobile/.local/logs/writable-schema-v1-mali-independent-cpu-analysis.json)。可减少工作，但不应替代当前更大的热点主线。

建议：作为独立候选，复用现有官方CPU采样，验证读侧减少是否大于写侧CAS/缓存成本；保持并发写／保护／range重用负例，不必新造计时器。

### 2. Polygon offset enable位：通用正确性小缺口，P2

外部 [7eb212c](https://github.com/dodola/d2r-mobile/commit/7eb212c33eccc5f41cc03c7a8414b5da50f5b9a7) 对仅支持的FILL模式只看offset使能的FILL位；独立保留factor/units/clamp，并检查clamp feature及有限值。[实际策略](https://github.com/dodola/d2r-mobile/blob/dd0e3c764fe2f797794eb7431ea0af9d81754c5f/dm/gfx/nvn/nvn_vk_depth_bias.h#L15)。

我方 [draw.c:1218](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_draw.c:1218) 用`offset_en != 0`，没有按当前polygon mode区分POINT/LINE/FILL位。因此只启用其它模式的bias也可能影响FILL。**这是源码语义差异，尚未证明当前营地触发，更不是FPS优化。** 建议以当前模式选对应bit，并补GPU深度值测试；不要把对方只支持FILL的限制照搬进我方。

### 3. Sampler格式与能力契约：部分缺失，兼容性优先

外部 [sampler policy](https://github.com/dodola/d2r-mobile/blob/dd0e3c764fe2f797794eb7431ea0af9d81754c5f/dm/gfx/nvn/nvn_vk_sampler_policy.h#L20) 区分整数／普通浮点／深度comparison；整数用nearest和整数border，不可过滤普通格式作合法回退；还限制aniso与LOD bias。

我方 [texture.c:2109](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_texture.c:2109) 已clamp最大aniso，但不支持aniso时直接拒绝；LOD bias原样送驱动，未见按实际采样格式选整数border／nearest变体。值得补成**资源格式决定的通用策略**，view格式变化要进入缓存身份，不能全局降过滤质量。当前实际HDR sampler审计没有发现异常aniso，因此没有证据这是现有GPU带宽大头。

### 4. 顶点格式及深度缩放fallback：有价值的能力补强，不能算当前提速

- 外部 [44ad876](https://github.com/dodola/d2r-mobile/commit/44ad876a5e27a1d99f1bddb863ca4e634cd290ba) 为不支持的RGB顶点格式拆成RG＋R，scaled整数使用整数fetch＋精确转换，并把适配计划纳入module/PSO身份。[实际vertex_module](https://github.com/dodola/d2r-mobile/blob/dd0e3c764fe2f797794eb7431ea0af9d81754c5f/dm/gfx/nvn/nvn_vk.c#L1849)。我方[格式检查](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_draw.c:1167)仍主要直接使用设备格式，否则拒绝；32位scaled还需要特别核对shader数值类型，不能只映射成UINT/SINT。借鉴原理时优先离线生成与严格格式计划，不直接抄运行时SPIR-V改写器。
- 外部 [37aaf28](https://github.com/dodola/d2r-mobile/commit/37aaf284a0b691966c494c8c9a24604f7e09ea18) 在原生blit不可用时以深度draw＋8次stencil掩码draw作最近邻缩放；我方[copy_t2t](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/vulkan/nvn_vk_texture.c:1198)遇深度缩放／格式转换仍拒绝。这个fallback可能很昂贵，且外部作者报告其营地没有触发，不能说加入后会涨帧。

两项优先用于真实新场景触发的能力缺口；保留全部mip/layer、scissor、query、资源写依赖和绑定恢复测试，不能为了当前场景覆盖率盲加复杂路径。

### 5. 其它小差距与不宜照搬的部分

- 对方PSO缓存身份额外包含quirk/feature/subgroup/shader ABI；我方当前持久缓存主要按device/driverVersion/api/pipelineCacheUUID分离。可借鉴成更清楚的版本隔离；**不能由此声称我方错误复用了shader**，我方每次仍传实际SPIR-V/layout/完整specialization给driver，opaque cache不是按裸game key取成品PSO。
- 对方将系统驱动兼容策略集中到不可变profile/quirk表，比散落开关易审；但其skin修复还限定两个游戏shader key。我们的ballot/helper/inline选项已经进离线工具链指纹，不应为迁移而增加新游戏key。集中管理可做，具体兼容变换须有对应设备失败证据。
- 上游 [86b1315](https://github.com/dodola/d2r-mobile/commit/86b1315a84cec67cfef0b3eab125dbc3c4489cc4) 新增image stride，fork在merge后消费；我方[记录器](/home/dl/d2r-mobile/.local/build/writable-schema-v1-candidate/sources/dm/gfx/nvn/hle_nvn.c:751)目前只有copy row stride。上游提交明确D2R不调用该入口，列为上游兼容同步，不作为本次FPS主线。
- fork有离线安装、持久driver/画质设置等面向用户的流程；我方已有driver loader／生命周期／加工验证，但当前偏开发控制入口。可借鉴产品流程，和渲染器净性能分开。
- 不照搬其自定义pass计时／采样器。现有官方Streamline/PA/CAM、simpleperf、Perfetto能覆盖的问题继续用工具。
- 不恢复我们已确认黑块的半精度方案；fork也没有交付可消除这项失败的新证据。只读SSBO／SPIR-V体积缩小／EarlyFragmentTests的旧实验不构成新的已证提速。

## 外部性能数字到底说明什么

| 作者报告 | 硬件／驱动、画面及场景 | 测量方式与边界 |
| --- | --- | --- |
| Image usage：19.2307→22.6028FPS，+17.53% | Adreno642L，TNA-AN00，系统driver8，原比例1.0、720p营地；同角色/区域/坐标 | 重启交错4轮，每轮约60秒SurfaceFlinger；无新PSO，thermal2／电池40–41°C；未测硬件压缩计数器、未证热稳态。[原报告](https://github.com/dodola/d2r-mobile/blob/dd0e3c764fe2f797794eb7431ea0af9d81754c5f/docs/ANDROID_IMAGE_USAGE_2026-10-16.zh-CN.md) |
| 附件缓存：17.8603→19.2857FPS，+7.98% | 同Adreno642L系统驱动／720p营地 | 同PID六个有效60秒样本，排除有新PSO窗口；SF实际呈现，CPU simpleperf定位；未证热稳定。[原报告](https://github.com/dodola/d2r-mobile/blob/dd0e3c764fe2f797794eb7431ea0af9d81754c5f/docs/ANDROID_RESOURCE_RETENTION_2026-10-16.zh-CN.md) |
| GPU-owned读：22.700→22.854/22.881FPS | 同系统驱动／原shader与比例／固定营地 | 温度/时钟/draw量未全控，逻辑上传检查减少成立，微小FPS差未证明；默认OFF。[原报告](https://github.com/dodola/d2r-mobile/blob/dd0e3c764fe2f797794eb7431ea0af9d81754c5f/docs/ANDROID_POOL_OWNERSHIP_2026-10-16.zh-CN.md) |
| Mali-G610营地28.56FPS；fixed60样本31.281FPS中位 | ROD-W09，Mali-G610MC6；比例1.0；fixed60样本包含操作，非固定同场景A/B | README历史观测；不能与系统642L三项改善叠加，更不能推到我方G710。长期温度/功耗未证 |
| Adreno830 fixed60样本52.832FPS中位，最高窗口59.874 | PPG-AN00＋指定Turnip sysmem，90秒操作样本 | 新硬件、不同driver与节拍，77个窗口且场景变化；不是所有游戏稳定60FPS。[README实验范围](https://github.com/dodola/d2r-mobile/blob/dd0e3c764fe2f797794eb7431ea0af9d81754c5f/README.md#android-support--experimental) |

README的scale1不等于手机原生像素。其详细HDR报告是1280×720；新75%性能模式明确960×540的3D、1280×720的UI，而且尚未真机验收。[性能模式记录](https://github.com/dodola/d2r-mobile/blob/dd0e3c764fe2f797794eb7431ea0af9d81754c5f/docs/ANDROID_PERFORMANCE_MODE_2026-10-17.zh-CN.md)。我方现有官方API中主HDR实际为2772×1240：[捕获证据](/home/dl/d2r-mobile/.local/logs/current1502-gpu-hotspots-evidence.json)。像素数约为720p的3.73倍，**不是“成本或性能应差3.73倍”的预测**；这足以说明原FPS数字不能直接排名，也不能通过降低尺寸冒充本项目原生30FPS目标已完成。

还有我方已经完成的独立尺寸诊断可供校准：[官方汇总](/home/dl/d2r-mobile/.local/logs/resolution-current-summary.json)中，同一已验证APK/缓存、原营地和scale1，2772×1240的前后样本为25.749/25.468FPS、约90.0/90.5M GPU pixels/frame；1610×720诊断为36.836FPS、35.3M pixels/frame，原world tick均约25Hz。draw量略有变化，不能把差额全归因像素，更不能折算成fork的净代码收益；中间低尺寸明确不计入native30目标，之后已恢复原尺寸并保存停机。这里有真实内部目标与官方counter证据，而不仅是Surface尺寸。

此轮最值得带回的技术点是块级脏页索引及更完整的格式能力边界；现有精确同步、异步完成、资源复用和原生工具分析继续保留。没有新证据支持放弃当前HDR/翻译线程主线，也没有实施或部署外部代码。
