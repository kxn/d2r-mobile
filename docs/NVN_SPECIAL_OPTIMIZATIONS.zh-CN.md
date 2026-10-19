# 原 Metal NVN 中的专用优化与 Android 对应

本清单按上游 `afad65c895386a96dfb863f2317e41d61222a5a9` 的
[nvn_mtl.m](../dm/gfx/nvn/nvn_mtl.m) 核对。仅列渲染及紧邻的宿主显示策略；
自动喝药、掉落提示等原有玩法增强不属于 NVN 后端，此处不混列。
下表的“已对应”表示存在实现及所述验证，不代表所有场景或性能已经验收。

## Metal 优化移到 Vulkan 的技术评估（2026-10-16）

**应迁移优化目的和成立条件，按 Vulkan 重写执行方式；不能以 Metal 源码逐行对应为性能验收标准。**
Apple GPU 与当前 Mali 都适合减少分块渲染的外部内存往返，所以多数通道带宽优化有意义。
但 API 的内容有效性、同步、资源驻留和参数绑定规则不同。下面的“值得做”是源码/架构评估，
不是已经测得帧率收益；本节更新了后续实施顺序。

### 从源码和手机读到的事实

- 当前手机实际报告 **Mali-G710 MC10 / Vulkan 1.1.177**。只读 `cmd gpu vkjson` 没有报告
  `VK_EXT/KHR_load_store_op_none`、`VK_QCOM_render_pass_store_ops`、`VK_KHR_dynamic_rendering`
  或 `VK_EXT_external_memory_host`；有 timeline semaphore 和 descriptor indexing 扩展。
  后两者仍须查具体 feature、创建 device 时启用，不能以扩展名称代替可用功能。
- 目前手机仍是已验收的事件索引版。末 20 条稀疏记录：每帧绘制中位 **2100**、通道 **63**，
  已完成 GPU 时间中位 **90.56 ms**（78.15–102.62 ms）；宿主绘制编码约 **21.92 ms**、
  池同步约 **6.83 ms**。这些不是同步采样的完整帧轨迹或受控 A/B，等待与 GPU 执行有重叠，**不能相加**。
- `nvn_vk.c:mtl_sub_begin` 每次先 `mtl_finish`，之后才复用暂存、查询、描述符和命令对象。
  这保证现有生命周期正确，却限制 CPU 录制与前次 GPU 工作重叠。不能直接删掉等待；
  要先按真实完成代号隔离所有在途资源，包含池镜像与 GPU→CPU 回写。
- `nvn_vk_shader.c:vk_shader_bindings` 每次调用都分配/更新 descriptor set；常量副本已有缓存，
  **不等于 descriptor set 已缓存**。`nvn_vk_draw.c` 每个物理通道创建 render pass/framebuffer，
  最终 load/store 变化还可能创建另一个 render pass。pipeline 已缓存，不能混称所有对象都复用。
- `nvn_vk.c:vk_barrier_at` 的通用后备仍是全局、全阶段、宽读写屏障。已有按实际读依赖延后结束通道的机制，
  但真正需要屏障时还可按生产者/消费者和子资源收窄，不能只删屏障。

可复核记录：本机 `.local/logs/android-metal-vulkan-optimization-audit.json`、
`android-metal-vulkan-audit-current.log`、`android-vulkan-optimization-capabilities.json`。
旧的 GPU 分段日志可辅助定位，但通道跨度含依赖/重叠、标签只是通道首个片元着色器，不能据此定罪某个游戏 shader。

### 逐项决定采用什么 Vulkan 写法

| 原机制与源码入口 | 对 Vulkan 的意义 | 应用方式、限制及优先级 |
| --- | --- | --- |
| 起始 clear / `first_draw_covers` 免读旧附件 | **高**，少一次外部读入；与游戏规则无关 | 用 `loadOp=CLEAR/DONT_CARE`。原静态着色器事实和 `nvn_cover.h` 证明可复用，API 绑定/可见性须重写；不能把任意全屏 draw 当作完整覆盖。新增覆盖路径已过 host/Mali GPU 对照及真实换区、面板、存档回归；尚无受控提速结论。 |
| `sdx_decide/sd_finish` 同提交写回前瞻 | **高**，未来整块清除前不必保存 | `storeOp=DONT_CARE` 只用于已证明不再需要的子资源。Metal 可在通道末改 store action，当前 Vulkan 用次级命令缓冲推迟开启主通道，这是合理适配；也可前置规划，需比较额外 CPU 开销。已有 GPU/游戏证据，尚无受控提速证据。 |
| `pass_ds_need` 深度/模板按需挂载 | **高**，省掉不使用的附件及读写 | 整个组合附件不用时不挂载，现已对应。只读深度/模板时优先匹配只读用途/布局；只用其中一个平面时，按设备能力和 Vulkan 格式/布局规则实现，不能照搬 Metal 独立 texture 槽。 |
| 同目标续画、`hoist_try` 提前纯 clear、`defer_*` 延后独立绘制 | **有意义，但需算净收益**，目标是减少物理通道切换 | 用命令依赖图/前瞻合并兼容工作，clear 合入 load，保持 NVN 的可见性、query/event、别名及读写顺序。不是把 Metal encoder 调用换名字；当前只有基本续用和起始 clear。候选少时及时退出，避免前瞻 CPU 超过 GPU 节省。 |
| `fuse_try/fz_p2_draw` 同像素 tile 读取 | **潜力高、实现成本也高** | Vulkan 1.1 的主要候选是 render pass + subpass + input attachment + 区域依赖；仅对原闭式证明可成立的同像素读取生成离线 SPIR-V 变体。任意纹理采样、过滤、偏移/邻域读取不能改成 input attachment。驱动是否真合成一次 tile 工作仍须看计数器；不能因为合成一个 API render pass 就宣称成功。 |
| `udc_on` 已 LOAD、未写附件省写回 | **目的有意义，原 DontCare 写法不可直接用** | Vulkan `STORE_OP_DONT_CARE` 即使没有 draw 写过也允许丢弃旧内容。未使用则不挂附件；只读且后续仍需内容时保留 STORE，或在支持并启用相应能力的设备上证明整趟未写后用 `STORE_OP_NONE`。**当前手机没有该能力，不能照搬。** |
| `xv_*` 跨提交历史学习 | 账本用于跟踪有效性有意义；**历史预测不等于正确性证明** | 原试用期根据历史决定未来不写回，源码也明确说明误判只能发现、无法挽回当前帧。基础 Vulkan 路径继续 STORE；仅在能证明完整生命周期/未来覆盖时做跨提交省略。原学习式丢弃不作为待补的默认通用能力或兼容性验收要求。 |
| `g_args_dirty/bind_args`、`g_fx_dedup`、反射缓存 | **高**，减少 CPU 翻译/驱动工作 | Vulkan 用 descriptor set 缓存、动态偏移/分配环、管线与动态状态去重、render pass 对象缓存。key 必须包含资源代号/视图/范围及布局；池重置、资源退役、GPU 可写资源不能误命中。不要复刻 Metal argument buffer/argument table 的内存布局。 |
| `blit` encoder 屏障/资源 usage 声明 | **高**，减少过度同步并保持阶段并行 | 按 NVN 读写建立 Vulkan stage/access/layout 依赖。Vulkan 1.1 的旧 barrier API 已可精确表达，无须强制升 Vulkan 1.3；保守后备保留。Metal 的隐式排序/usage 或 barrierAfterEncoderStages 不能一比一替换。 |
| arena、deferred release、在途 allocator ring | **高**，既影响内存也影响并行 | 保留原栅栏寿命原则，按 Vulkan fence 或已启用的 timeline 值组织每提交的命令池、描述符池、上传、查询与副作用。当前串行生命周期正确；多在途须作为完整改动验收，不能单独移除 `mtl_finish`。 |
| `pool_res` 按需驻留、Metal shared/no-copy | 省内存/少拷贝目标有意义；**API 手段不通用** | Vulkan 以 allocation、mapping、HOST_COHERENT/非一致内存维护及 external-memory 支持为准。当前手机没有 external host import，不能假定任意游戏指针可零拷贝。继续脏页/代号增量同步；后续评估分配器和更细范围。 |
| `MTLStorageModeMemoryless` / 瞬态 tile 存储思路 | 仅对完整生命周期在物理通道内的临时附件有意义 | Vulkan `TRANSIENT_ATTACHMENT` + 合适的 `LAZILY_ALLOCATED` 内存只是有条件候选；游戏后续有 sampled/storage/copy/跨提交通道用途的图像不能直接改为瞬态。先证明生命周期再分配，不能由“看起来是 render target”推断。 |
| 可选外推、面板冻结、bloom 降级、跳 FXAA、零源描边省略 | 原策略可独立评估，**不是 Metal API 本身加速** | 已有专用分支按原规则/开关隔离。加亮与跟手图也不算提速。降低画质、隔帧重投影和冻结场景不能掩盖普通 NVN 路径的开销；未移植部分继续见下表。 |
| 自适应比例、30/60 档与出图链 | 目标通用，实现依赖显示系统 | 继续原控制器；Android 用 Surface/交换链、显示刷新提示和实际呈现时序实现。Metal display link、present allocator 与 API 节拍不直接搬。目标 60 不代表实际 60。 |

以上 Vulkan 判断依据 Khronos 的
[附件读写指南](https://docs.vulkan.org/samples/latest/samples/performance/render_passes/README.html)、
[StoreOp 规范](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentStoreOp.html)、
[subpass 指南](https://docs.vulkan.org/samples/latest/samples/performance/subpasses/README.html)、
[屏障指南](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html)及
[描述符管理指南](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html)。
设备上的收益和优先级判断是结合本项目源码的推断，不引用其他示例的提速百分比作为本游戏收益。

### 实施与验收顺序

厂商工具已搭建，当前采集与开销隔离方式见 [Mali 真机性能分析](MALI_PROFILING.zh-CN.md)。

1. 保证显示正确，再由当前 Android/Mali profile 选择优化工作；上表仅作参考，不作为必须依次补完的清单。
   首绘全覆盖免 LOAD 已完成真机功能回归，保留原始 shader 与加工链完整来源；净提速仍未证明。
2. 在同场景、相同比例/设置、接近温度下对照；正确性运行开同步校验/像素比较，性能运行关闭它们。
   同时记录完整呈现间隔分布、CPU 录制、GPU 完成、通道拆分原因及 load/store 字节估计；估计值不冒充硬件带宽。
3. 优先削减已观测到的开销：Vulkan 对象/descriptor 复用、按资源收窄屏障，以及可证明的通道带宽省略。
   先做独立开关的单项对照，避免一次改多项无法归因。
4. 再处理 clear 提前/绘制延迟、同像素 subpass 合并；并行提交先设计完整在途资源环，覆盖所有真实完成副作用。
   这些工作互有影响，按实测净收益调整顺序。
5. 无法证明、设备不支持或只在某 Apple 驱动上观测成立的行为，保留正确的通用后备并写明差距；
   不新增相机/地图/游戏地址特判，不用游戏 binary 补丁补偿底层问题。

**因此“对应 Metal”应定义为 NVN 语义一致、优化条件可追溯、Vulkan 实现正确且净收益可测，
不定义为每个 Metal 技巧都必须开启。**

## 依赖该游戏或原移动端界面的分支

| 项目 | 原实现具体做什么 | 上游默认 | Android Vulkan 当前状态 |
| --- | --- | --- | --- |
| 隔帧运行时外推 `rt_*` / `nvn_rtex.inc` | 完整帧与外推帧交替；读取相机矩阵，将历史颜色/深度重投影，补洞；识别十个蒙皮着色器，另画人物、界面、半透明特效和局部后处理。相机读取在这条可选路径中。 | **关**，`DM_RT_AB=1` 才开；原注释记录 A17 卡顿和选角重影。30 帧省电档全画。 | 已单独对应到 `nvn_vk_runtime.c`、`nvn_vk_extrap.c`。GPU 像素/深度/模板及营地、移动、进出区域、面板、后台恢复有证据；完整画面对照、更多场景与受控性能仍待验。**默认关**。 |
| 面板世界冻结 `frz_op` | 原宿主冻结世界推进；先完整画世界，再保留该结果，只画面板/界面。不是单纯隐藏仍在运动的世界。 | **关**，`DM_PANEL_FREEZE=1` 才开。 | 已对应 `nvn_vk_freeze.c`；真机面板、卖出/买回、比例变化强制重画、后台保存恢复已验。**默认关**，复用原冻结门。 |
| 调色表加亮 `bright_lut` | 命中计算着色器 `552f45c348a61b86` 后修改 3D LUT；原宿主读取区域、昼夜亮度，给出平滑曝光系数，UI 在后面合成。属于画面风格适配，不是提速。 | 动态策略生效；局外/不需加亮时系数约 1，跳过改写。`DM_BRIGHT_K` 只是测试覆盖。 | 已对应 `nvn_vk_bright.c`，调用原 `dm_bright_k`；独立数值/GPU 比较及真实营地 LUT 已验。 |
| 泛光从 1/4 尺寸起做 `bloomq_*` | 按 `2938…` / `019c…` / `cc26…` / `2e2d…` 四个着色器键识别链；跳过原 B0 层，合并截亮与降采样直接生成 B1，后续合成改读 B1。会改变采样和泛光结果。 | **开**，须满足 TBDR/链识别条件；`DM_NVN_BLOOM_Q=0` 关闭。 | **A+B1 合并候选已实现，Mali 营地正确性已验，默认关**。独立 `nvn_vk_bloom_quarter.inc` 在省 A 前有界证明完整同提交金字塔与合成链，按原四样本/阈值/c1 系数直接生成 B1，D 临时改读后恢复绑定。**使用原 C 着色器/UV 完整上采样物化 B0，临时关闭其原加法混合**，保证未知或下个提交的读者仍可取得 B1 衍生的有效 B0，不能宣称已实现 Metal 的完整省 C 收益。详见文末候选边界。 |
| 跳过 FXAA | 识别 `ebcfcd1fa0a77dbc`，省掉抗锯齿绘制，让后续 `52aac1f2306b8fc5` 的锐化/放大改读 FXAA 输入。以抗锯齿质量换开销。 | **关**，实验开关 `DM_NVN_NOFXAA=1`。 | 可选中心采样模块已测，默认仍关闭该选项；保留原绘制/UV/采样器/Alpha，不等同于原 Metal 的纹理转发实现。两GPU开关/像素验证通过、Mali营地同步检查正常。原生对照Mali帧率持平，高通约+1.65%；算术/纹理工作下降，但LS基本不变。 |
| 描边零输入跳过 `zskip_*` | 原事实表指定 `4a819818b8e489c5` 描边模糊着色器；证明采样全零且系数有限时输出全零，再结合状态条件省掉无效工作。不是任意“黑贴图都不画”。 | **开**，受 TBDR 总开关约束；`DM_NVN_ZSKIP=0` 关闭。 | 已对应到独立 `nvn_vk_zero_draw.inc`，不扩展上游游戏事实表。按实际格式区分 RGB / RGBA 零分量，完整清屏建立证明、实际写入失效；必须无深度/模板写、无逻辑运算/目标混合、查询结果在 reset 前不被报告。逐 mip 与全量拷贝传播证明，消去零到零复制；两 GPU 像素/同步检查通过。同进程原生 A/B/B/A：Mali 24.997→25.879、高通18.446→19.726 FPS，含场景/温度趋势，不能把复制单项收益独立归因。 |
| 拖货跟手图 `ghost_note/ghost_flush` | 按原货架格网、四边形和纹理使用频率挑出物品图标，平移视口重放同一笔绘制，让图标跟随手指。属于触控显示适配，不是提速。 | 拖动时由原 UI 请求，平时不额外绘制。 | 已对应 `nvn_vk_ghost.c`；GPU 图标选择及真实商店拖动/买卖已验。 |
| 平板 UI 投影偏移 `ui_mat_patch` | 原 iPad 全屏布局把 16:9 UI 放到底部；只给识别出的 UI 绘制复制投影常量并加偏移，同步改剪裁；全屏压暗另处理。**这也是原代码改矩阵的一处，但改的是 UI 投影。** | 有 UI 矩形回调且矩形非全屏才生效；普通 iPhone/Mac 无改动。 | **完整通路未对应**。当前 Android 固定 16:9 留边，不启用此平板布局；跟手图注册回调不等于已实现全部投影/剪裁逻辑。 |

## 原宿主显示策略：不是 NVN API 语义

| 策略 | 原行为/默认 | Android 状态 |
| --- | --- | --- |
| 自适应 3D 比例 | 原移动端配置 0.75；流畅优先时按掉帧下降，默认下限 0.60；界面与文字不降分辨率。`q3d=1` 画质优先回到配置比例。 | 复用 `hle_oe.c` 原控制器；真实营地 0.75→0.70→0.65→0.60 已观察。会影响 3D 清晰度。 |
| 30/60 帧档 | `eco_fps` 默认关，即目标 60；开后目标 30，匹配显示节拍。 | 原设置及取图节拍已对应，真机同进程 60→30→60 已验；Android 屏幕刷新率提示仍待补。**目标帧率不等于实测帧率**。 |

## 通用 GPU 优化：与游戏规则无关，但仍有移植差距

2026-10-17 补充：优化保留与整帧涨帧分别判断。成立条件可证明、确实减少运算或资源往返、
新增成本可接受且视觉/稳定性没有明显退化的实现应保留，不因当前瓶颈掩盖 FPS 收益而撤回。
当前 GPU Profile 用于安排实施优先级和核验工作量，不能把“暂未涨帧”写成“没有价值”。

| 原 Metal 机制 | Android Vulkan 当前对应 |
| --- | --- |
| 管线/着色器缓存、重复状态/参数复用 | 已有通用管线、着色器和常量副本缓存；按实际资源写代号失效。Metal 参数编码器与 Vulkan 描述符机制不同，不能按代码逐字搬。 |
| 相同目标连续绘制合为一个通道、起始清除合入 load | 已有基本连续通道复用及起始 clear 合并；依赖、拷贝、查询需要时结束通道。 |
| 深度/模板按需挂载 | 已有整提交命令前瞻；证明整趟不用时省掉组合附件，GPU 开/关对照通过。只用其中一个平面时仍保留两者，单平面省略待补。 |
| 同提交整块覆盖的写回前瞻 | 已通过次级命令缓冲延迟确定 storeOp；颜色/深度/模板独立证明，采样/拷贝/局部清除前保留内容，GPU 开/关对照通过。已有按纹理事件分桶/二分索引，包含存储别名和描述符代号失效后备；校验模式逐项比对扫描器。可能过滤命令的原冻结/外推路径仍保留写回。 |
| 更多 load 省略、clear 提前、延迟绘制、同像素可读 tile 合并 | **未完整对应**。新增首绘覆盖路径已过 host/Mali GPU 与真机回归；可运行不代表同等带宽/性能。原 `DM_NVN_RT_RANGE` 窄候选额外续用默认关。 |
| 未写附件省写回、跨提交 XV 账本 | **须按 Vulkan 语义重新决定实现**，不能简单列作应照搬的缺口。当前手机不支持 STORE_OP_NONE；历史学习不能保证未来无读取。详见上面的技术评估。 |
| 脏页/资源代号、在途资源延迟回收、上传/暂存复用 | 已有 Vulkan 对应；围绕真实完成栅栏与内存可见性验证，不依赖人物、相机或地图。 |

`DM_NVN_SHMAP` 指定的是原离线着色器键→阶段映射表，**不是替换游戏着色器的优化开关**。
原 BC 解码、移动端纹理包/ASTC 等资源加工也应另算工具链/格式能力，不能与玩法优化混为一谈。

### 结合已有 Mali Profile 的本轮评估（2026-10-17）

基于已有官方 Streamline 完整营地报告及 Frame Advisor 的 523 帧资源图，未为重新分析另采基线。
完整营地报告约 57 个通道/帧，外存写入约 276 MB/帧；这说明减少中间附件落地有价值，
不等于已经证明外存带宽饱和。旧帧中的逐查询分段已经由当前累计查询实现消除，不能再次计作待优化项。

| 方向 | 当前证据、适用条件与动作 |
| --- | --- |
| 纯清除提前 `hoist_try` | 旧帧纯清除通道较少。只有清除目标不被跨越工作访问、与原通道附件无冲突时才能提前；保留为待移植通用能力，不预报大幅涨帧。 |
| 独立绘制延后 `defer_try` | 透明 MRT 链中存在目标/深度切换，但尺寸、深度及资源读写会限制合并。需要沿用原独立性证明和状态/查询前缀规则，不能直接交换绘制顺序。 |
| 同像素融合 `fuse_try/fz_p2_draw` | 后处理存在连续全尺寸附件链。已用 spirv-cross 检查末段片元：部分直接读取插值 UV，部分同纹理多偏移读取。后者不能改成 input attachment；前者还须原 `uv_proof`、子资源/采样格式/尺寸以及 Vulkan 子通道依赖证明。优先继续实现有证明的边。 |
| 原编译器表达式传播 `XLATE_DEBUG=0` | 原项目有此可选优化，默认 `XLATE_DEBUG=1` 禁用。已定位移植后人物黑块的根因：把读取可变局部量/函数输出的表达式移动到了值改变以后。现仅传播可证明稳定的纯表达式，保留存储、导数、子组操作等位置；回归先复现错误再通过。整捕获帧所有阶段替换后 RGB 完全相同；官方回放 L2 LS 读取/帧约降 7.03%，GPU cycles/帧约增 1.23%，FPS 约增 2.84%。已从原包重译全部 6931 个着色器并部署，不使用游戏键白名单。回放收益不能冒充真实游戏收益。 |
| 原放宽浮点舍入 `XLATE_REDUCED=1` | 上游默认开，Android 已启用。原始热点着色器重编 ON/OFF：`NoContraction` 为 9/1377，ON 与原已部署版本逐字节相同。这是允许部分运算合并/放宽舍入约束，不是统一 FP32→FP16；没有在上游找到另一个独立通用的寄存器溢出消除开关。额外 `spirv-opt --relax-float-ops` 试验曾导致屋棚/材质黑块与亮度错误，仍保留为待修实验，不能与原已开启的选项混淆。 |
| 新增 C1 常量折叠（不属于原项目） | 实现和全库候选保留，默认关闭有明确额外成本依据：官方回放 B-A-B-A 的 GPU cycles/帧增约 1.79%、L2 LS 读取/帧增约 12.63%；不是因 FPS 未上涨而丢弃。不能把静态加载条数减少等同于寄存器分配后的 GPU 工作必然减少。 |
| 原 BC3→ASTC 4×4 老机型纹理包 | 复用原 `astc_pack.py`、`casc_overlay.py` 和覆盖层生成器；本机 Mali 没有 BC 硬件采样，原后备会展开纹理。完整包已转码并部署 27215 张、102128972 块，thorough 档，分辨率及 mip 不变。原生 GPU 已验证线性/sRGB ASTC 拷贝到旧 BC3 后备数组的 mip/layer，真实营地人物、材质、HUD 正常。Android dumpsys 图形内存快照从 4351464 KiB 降至 2715216 KiB（不是逐格式分配归因）；官方实测约 16.96 FPS，但运行温度/频率不同，不能把与前次 14.075 的差全部归因于纹理。 |
| 常量复用、已知 CPU 数据免回读、无效遮挡查询省略 | 已有正确性和真实游戏验证；前者避免读取映射 GPU 内存并复用相同 c0/c2，后者保留 GPU copy 和 fence 发布、对可证明数据免重复回读。三项改为默认保留，不以即时 FPS 为门槛；遮挡查询只省略在报告前必被重置的结果，仍提供原路径对照。 |

本机完整证据索引：`.local/logs/optimization-retention-decision.json`，
官方计数对照：`.local/logs/shader321-ast-official-abba-comparison.json`。
最新精度核对：`.local/logs/upstream-reduced-precision-proof.json`；表达式优化对照：
`.local/logs/safe-ast-official-comparison.json`、`safe-ast-official-pixel-comparison.json`；
ASTC 部署前真实营地 `.local/logs/safe-ast-storage-original-official-summary.json` 为约 14.075 FPS，
部署后 `.local/logs/astc-original-official-summary.json` 为约 16.96 FPS；两者都是
2772×1240、3D 比例 1.0，尚未达到 30 FPS，不能把温度/频率不同的运行直接当隔离代码提速对照。
原包来源/产物哈希在 `.local/build/android-astc-full/provenance.json`，手机全部覆盖层文件哈希
和旧覆盖层回退路径在 `.local/logs/android-astc-full-deployment.json`。
常规 Vulkan 构建入口 `scripts/dev/build-vulkan-shaders.sh` 现在默认保留
`XLATE_DEBUG=0`、`DMSH_VK_STORAGE_BIAS=1`，并在未指定外部 `XLATE` 时构建当前翻译器及其
着色器库依赖，避免 Program.cs 指纹正确却误用旧 AstOptimizer DLL。显式环境变量仍可选择
参考路径，原 Metal 默认值不改。未指定额外选项的完整构建已得到与手机相同的 `a9484d707e30`
缓存，6931/6931 验证通过。
单帧回放结果不等于当前真实游戏帧率；MOC 的 r56 静态估计也不代替手机 r32 驱动实测。

## 诊断开关，不算产品优化

`DM_NVN_SKIP_FS`、`DM_NVN_SKIP_SMALL` 会故意丢绘制；高亮、视觉尺子、通道 dump、
同进程 A/B、GPU 时间查询用于定位和对照。不能拿这种缺画面的结果证明兼容性或提速。
Android 的 GPU profile、同步校验和原创夹具也属于验证工具。

主线仍是原 NVN 资源、着色器、固定状态、绘制、拷贝、同步与呈现语义。
新增通用修复不增加游戏着色器白名单、相机猜测或游戏 binary 补丁。

2026-10-17 补充：按访问范围同步缓冲的候选中，`ghost_note` 的 CPU 几何证明改用
`vk_pool_try_read` 读取原 CPU 地址，待 GPU 写入时仍放弃证明，不等待、不重放陈旧数据。
原格网、纹理频率、挑选和额外绘制规则均不变。实例除数后备展开同样在原 GPU 完成
边界后读 CPU 地址；不再依赖“已把全部池上传到 GPU 镜像”这个隐含条件。这是平台
内存依赖修复，非新增游戏判断；宿主/真机回归包含强制实例后备和 ghost 实际像素。


## 2026-10-17：额外采样降精度不再部署

额外 `DMSH_VK_RELAX_SAMPLED_RESULTS=1` 使选角场景出现大块黑色遮挡；全局同步和
CPU 常量缓存的参考对照均未消除。相同 APK 仅关闭该选项后画面恢复并获用户确认。
2397个SPIR-V模块逐字节相同，4534个仅RelaxedPrecision装饰不同，所有元数据相同，
见私有 `selection-sampled-precision-regression-proof.json`。正确缓存为
`122643330366`，原 `2ee3b4b91974` 留作问题复现，不能默认部署。
这不关闭原项目 XLATE_REDUCED，也不撤销已证明安全的 AST/常量处理。之前局部场景
load/store资源节省证据仍有效，但不能代替全场景正确性；禁用原因是确认的画面错误。


2026-10-17同步登记补强：原brightness/LUT增强的算法、参数、shader选择均未
新增或修改；只在原compute发出后声明其写入的真实VkImage、mip及生产阶段。
这属于通用资源追踪的调用点适配，不是新增游戏专用优化。原增强内的既有
保守屏障仍保留；present/copy、clear等通用同步变更另见资源依赖文档。


## 原泛光 1/4 候选的实现与边界（2026-10-18）

`nvn_vk_bloom_quarter.inc`、`shaders/bloom-quarter.vert/.frag` 只采用原 Metal 的四个
片元事实键、阈值、c1 绿色/蓝色系数、源图实际尺寸、四次 linear/clamp 采样及 alpha=1。
阈值按当前 Vulkan uniform ABI 从本次 A draw 的真实绑定读取，并使用 `vk_pool_try_read`；
未知 GPU 生产值不触发等待，保留原路径。内置参数以 push constants 固定到每次实际 GPU 绘制，
不读取营地固定值，不修改游戏 binary，不引入场景或材质新键。

候选在省掉 A 之前，通过现有完整 submission 顺序索引最多检查 4096 条操作、64 次绘制。
使用纯状态影子核对 A→B1→后续降采样→逐级上采样→D 的金字塔资源身份、尺寸与依赖；
拒绝中途未知绘制、pool 切换、copy/update/dispatch/event/query 副作用、源图被清除、存储别名、
格式重解释、非零基级/层、不可证明的完整覆盖和非有限系数。
A/B1 的样本计数必须由原 reset/report 前瞻证明无外部读者。

直接生成 B1 的内置 Vulkan 绘制使用当前资源访问追踪、精确屏障、真实完成后的 descriptor /
framebuffer / image-view 生命周期，以及同一持久化 pipeline cache 的外部同步锁与脏标记。
原厂捕获确认 C 实际使用 `CONSTANT_COLOR + ONE` 加法，D 还保留只读 stencil；
若省 A 后仍原样运行加法 C，会读取已经删除的 A 内容，不能以放宽证明伪装正确。
因此 C 用原始着色器、UV 和系数完整上采样物化 B0，但仅在该笔绘制临时关闭目标 0 的混合，
绘制后恢复原 color state 并失效管线状态缓存。D 则完整保留原有 stencil/混合等绘制状态。
这使后续未知读者和跨提交读者仍有 B1 衍生的有效 B0，
无需原 Metal 的跨帧纹理转发预测。只有 D 的实际输入、B1 view、将使用的 sampler 身份与参数
仍与规划一致时临时替换纹理绑定；失配则正常读取 B0，绘制后恢复原绑定。
无关 descriptor slot 的注册不构成失效，不能用全局统计 epoch 代替相关资源身份。
A 被省略之后，真正相关的源图/输入/目标身份违反已证明链时仍是硬错误保护；默认启用前须继续
覆盖场景切换和资源重建，不能仅凭营地一次命中认定完成。

机制上省去 A 的半尺寸截亮绘制及其 B0 输出写入、B1 对该输出的读取；B1 改为直接对原源图
执行四样本截亮/合并降采样。新增成本是有界 CPU 链证明、每种输出格式的一个内置 PSO、
每个实际替换 B1 的一次 descriptor 分配/写入和 push constants。B0 分配、已有清除与 C 上采样物化成本
依然保留，因此不宣称已省去全部 B0 内存、上采样或中间附件成本。
未经过真机官方 Profile 的候选对照，不报告 FPS 或硬件带宽收益。

原创 GPU 用例通过原 NVN 录制和同一 Vulkan 执行层运行，包含完整链的图像结果、下一提交的未知
B0 读者、缺 D、未知中途绘制、A 后清源图、局部 B1、非有限阈值/系数、有效 query 报告、
inline UBO 更新、GPU 生产阈值、mip/格式视图和两种 pool 切换。
另有真实加法 C + 只读 stencil D 的视觉对照：D 对照 B1 结果、stencil 拒绝的半屏保持原内容，
下一提交的未知读者也取得物化 B0；局部 C 不得强行覆盖，保持原路径。
Host lavapipe + 异步提交 + Khronos 同步校验的完整 suite 通过 **671939 checks**，
无 `VUID` / `SYNC-HAZARD`，不是 Android 真机或完整游戏的验收。
新增的 A 后清源图反例也暴露了既有 per-mip 布局转换来源阶段遗漏，已在通用
`image_dependency_source` 加入真实 `c->mip_scope`，没有扩大到全局屏障。
现成 GDB 另在 B1 入口注册无关 slot、改变全局 descriptor epoch，完整 GPU suite 仍通过；
该项是正确性故障注入，不是性能计时。
证据：`.local/logs/bloom-quarter-host-sync-validation.log`、
`.local/logs/bloom-quarter-unrelated-descriptor-gdb.log`、
`.local/logs/bloom-quarter-fixture-manifest.json`。候选使用 Android `--bloom-quarter` 对照，
默认关闭；原 Metal 默认开不变。


### v3 真实链的工作量边界

对当前 Mali `3b0e470fc91d` 缓存的四个原始 SPIR-V 使用现成 `spirv-cross` 反编译，
并结合既有原厂 gfxrecon 执行状态记录核对：

| 区间 | 原路径 | v3 候选 | 可以证明的工作变化 |
| --- | --- | --- | --- |
| A+B1 覆盖像素 | A `1386×620=859320`，B1 `693×310=214830`，合计 `1074150` | 仅 B1 `214830` | 该两步逻辑着色像素 −80%，不代表整帧或硬件 invocation −80%。 |
| A+B1 纹理采样表达式 | A 每像素 1 次 `texture`，B1 每像素 4 次 `textureLod`，合计 `1718640` | B1 每像素 4 次，合计 `859320` | 该两步采样表达式 −50%；linear 过滤有多 texel 足迹/缓存/压缩，不能直接当成 DRAM 字节数。 |
| 截亮/亮度运算 | A 每像素一次，合计 `859320` | B1 对四样本分别算，最多 `859320` 次 | 主截亮计算次数并未随片元数 −80%，不预报同等 ALU 节省。 |
| 最终 C | `859320` 覆盖像素，每像素四次采样，并与 B0 加法混合 | 同量上采样，临时关闭混合，以 B1 结果物化 B0 | 保留 `3437280` 次采样表达式和完整 B0 输出；省去旧 B0 加法读取，覆盖证明还允许免旧内容 LOAD。不能计作已跳过 C。 |
| D 合成 | 每个通过 stencil 的像素一次 B0 采样 | 同样一次 B1 采样，完整保留 stencil/混合 | 被读图像工作集由 `3437280` B 降至 `859320` B；实际采样次数不降，不能推导总线读字节减少四倍。 |

A 被省略的 `859320` 次 R11G11B10 片元写，对应约 `3437280` B **逻辑输出量**。
但 A 前的 B0 clear 目前没有消除：只清除的通道仍可能 STORE 同一张 B0；因此不能宣称已经
省掉等量最终外存写回。C 物化仍占用 B0，其图像分配和输出写回也保留。
这些边界是本候选与原 Metal 完整跳过 C 的区别，实际 GPU 读写/周期必须由官方计数器对照。

反编译素材留在 `.local/build/bloom-quarter-<原键>.glsl`；既有实际状态来自
`.local/logs/mali-camp523-executed-pass-state-verified.json`，其输入为原厂
`gfxrecon-convert` 的 API JSONL，不把 API 区间时间当独占 shader 成本。
v3 APK `55462666ca9a…` 已在 Mali 营地真实原生 `2772×1240`、固定 3D 比例 1 验证：
A+B1/C/D 完整链命中 72 次、fallback 0，Khronos 验证消息为空，已按原生命周期 fresh save 2 后停机。
该正确性结果尚不证明多区域覆盖、净 FPS 收益或稳定至少 30 FPS。


### v3 普通同进程 ABBA（2026-10-18）

冻结 APK `55462666ca9a…`，每台设备在同一 PID、同一营地坐标、原生输出/固定 3D 比例 1，
关闭 GPU layer 后用 SurfaceFlinger TimeStats 取得四段各 20 秒的应用窗口。
A=候选关，B=候选开；两台数据独立统计，不把高通与 Mali 的帧率混成版本回归。
开窗口均确实命中 A+B1/C/D 全链且回退 0，关闭窗口没有省略/替换。

| 设备 | OFF 1 | ON 1 | ON 2 | OFF 2 | OFF 均值→ON 均值 | 可以支持的结论 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Mali，PID 29130 | 28.331 | 27.909 | 30.204 | 30.857 | 29.594→29.0565，−1.82% | 不能报告提速；两相邻开/关比较分别 −1.49%、−2.12%，存在轻微回退迹象，但同开关自身有约 8–9% 时间漂移，不能将小差异稳定归因。 |
| Adreno 735，PID 28927 | 20.380 | 20.476 | 20.648 | 20.595 | 20.4875→20.562，+0.36% | 本轮近似持平；两相邻比较 +0.47%、+0.26%，不足以证明稳定收益。 |

Mali 同一 OFF 的前后变化为 +8.92%，同一 ON 的前后变化为 +8.22%，大于候选开关均值差。
HAL **Current temperatures** 的 skin 从 47.916℃ 升到 53.470℃，status 全程 3；
CPU/GPU 从约 72.548℃ 升到 81.321℃。这不是冷机条件，不能混用上方 Cached temperatures。
高通本轮 skin 36.076→37.819℃，GPU 各传感器约 51.4–54.2℃，实际 HAL status 为 0，
cooling 设备 `gpu` 的数值始终 8；该驱动状态索引没有给出频率映射，不能当作 MHz 或直接断言锁频。
这些具体窗口没有同时取得 CPU 调度/频率轨迹，不能据温度快照断言降频或 CPU/GPU 相位是原因。
Mali 后段升温但 FPS 反而更高，也不支持单靠温度作因果解释。

证据：`.local/logs/bloom-quarter-v3-{mali,735}-abba-r2-result.json` 及各窗口
`thermal-before/after.txt`、`timestats.txt`，汇总
`.local/logs/bloom-quarter-v3-abba-analysis.json`。首次编排的 OFF 断言写错而中断的运行不计入此对照；
两台有效测量都已通过原生命周期 fresh save 2 后停机。
官方 LWI/Streamline 工作量与带宽对照独立进行，不拿开启采集层后的 FPS 替代这里的普通运行结果。
候选当前仍默认关闭，既没有宣称稳定涨帧，也没有把已减少的逻辑工作量判为不存在。


### v3 官方 Arm 工作量对照

同一 PID 29947 取得 OFF/ON 各 12 秒 Streamline capture，官方 Performance Advisor 对各自
2–10 秒片段按工具的帧边界归一；以下优先使用工具直接报告的 **每帧** 数值：

| 官方指标/帧 | OFF | ON | 变化 |
| --- | ---: | ---: | ---: |
| GPU cycles | 27106035.9 | 27086462.1 | −0.07% |
| Fragment cycles | 18506313.9 | 18529194.8 | +0.12% |
| Shader arithmetic cycles | 8867036.5 | 8880527.9 | +0.15% |
| Shader load/store cycles | 5610825.0 | 5643727.9 | +0.59% |
| Shader texture cycles | 4140356.4 | 4135999.6 | −0.11% |
| 外部读字节 | 301289930.7 | 302602785.3 | +0.44% |
| 外部写字节 | 184083862.5 | 185916703.4 | +1.00% |
| 外部总字节 | 485373793.2 | 488519488.6 | +0.65% |
| Render passes/frame（官方 timeline 均值） | 52.06125 | 53.02375 | 约增加 1 趟 |
| Compute dispatches/frame | 11 | 11 | 不变 |
| Draw calls/frame | 2352.585 | 2350 | −2.585，包含轻微场景工作波动，不能全归因 |

这组整帧实测没有证明纹理单元或外部流量显著下降。对官方 raw 计数按其已报告帧率积分作近似
每帧归一，L2 texture beats 约增 8.44%、external texture beats 约增 0.95%；这是桶/帧边界
近似，不能把 raw beats 擅自换成未知单位的 DRAM 字节。GPU queue endpoint stall 也不等于
CPU fence 等待，不能借此宣布 CPU/GPU 互等已经清除。

候选还存在明确额外成本：v3 `bq_capture()` 会先结束 B1 原有 pending clear-only pass，
再单独开始内置 B1 pass，导致多一趟；A 前的 B0 死清除也仍保留。它们有机会抵消省去的着色工作。
因此后续先修正这两个已被完整链证明覆盖的死清除边界，再评价完整候选，而不是把带额外往返的
v3 结果写成原 Metal 泛光方向没有收益。

采集工具状态本身也发生明显漂移：OFF 的 PA FPS 23.9、GPU utilization 72.3%，
ON 为 16.1 / 99.6%；OFF boundness 有 40.8% unknown，ON 为 100% fragment。
它说明两次捕获的运行条件不同，不能将这些分类外推到无 layer 的普通运行，更不能把工具场景
SurfaceFlinger 的 OFF31.332→ON16.007 写成候选实际让帧率减半。
每帧 cycles 基本不变，也不支持该工具状态变化来自每帧 shader 工作突然翻倍。
没有这两个区间的实际频率轨迹，不能直接确认 DVFS、CPU调度或提交相位的因果。

完整官方文件在 `.local/profiles/bloom-quarter-v3-mali-arm-run{1,2}-report/` 的
`timeline.csv`、`raw.csv` 与 `advisor/*.json`；整理结果
`.local/logs/bloom-quarter-v3-arm-analysis.json`。本次采集已 fresh save 2 后停机并恢复全局 layer 设置。

### v4 死清除边界修正（待真机对照）

完整 A→B1→C→D 证明通过后，允许取消 A 前 B0 或替换 B1 前的 pending clear-only job。
取消只针对尚未向 primary 编码的同一单颜色附件、无绘制、无 depth/stencil、无活动查询的清除；
有绘制、其他附件、已编码或不匹配视图的 job 仍沿原路径结束。clear 的 secondary 只结束录制而不执行，
job、RenderPass/Framebuffer cache 引用和 command buffer 都留到原 submission 的真实 GPU 完成后回收。
不回退 `V.passes` 身份，避免历史资源依赖把后续 pass 当成原 pass。

这减少实际 clear-only Render Pass 和对应 store/reload 机会，未消除 C 的 B0 物化，也没有声称
额外的 tile 往返必然等于某个 DRAM 字节数。保留前述 v3 测量作为旧实现证据，v4 尚需独立验证。
owned GPU fixture 新增 A/B1 pending clear 命中，以及带 draw、MRT、depth/stencil、已编码 copy
的保留规则；验证其他附件/拷贝结果和跨 submission 未知 B0 读者，不把取消次数代替原厂带宽数据。

### v4 验证和普通同进程 ABBA（2026-10-18）

冻结 APK `c43de27aeaa50c040e9b8f3c149a73ff1123633f04ffc9ce692ca8d99ce34bc6`，
ELF `0fee1be8…`，339 个输入。默认 descriptor budget 和 8-set 小池的 host 异步/同步验证
均为 **696759 checks passed**；Mali/Adreno owned GPU fixture 分别为 697537/696761。
新增用例在保留其他附件绿色、前置 draw、depth/stencil 的后续 stencil 测试、已编码 copy 的
magenta 结果以及跨提交 B0 未知读者上通过，未扩展未支持的深度 ReadTexels 路径。
Mali 原生游戏验证链命中 101 次，A/B1 前死清除各取消 101 次，回退 0；该次原生验证无新增
validation 消息，结束后 fresh save 2 并停机。

这证明 v3 两个尚未编码的 clear-only primary render pass 确实不再执行；它不等于已经用
硬件计数器测得某个外部流量降幅。v3 官方整帧约增加一趟 pass 的数据只描述 v3，不能直接
改写成 v4 的官方 pass 数。C 仍完整物化 B0，后续未知读者的兼容性边界保持不变。

v4 的 Mali 同 PID 32662、原生 2772×1240/固定 3D 比例 1、无 GPU layer、四段各 20 秒：

| OFF 1 | ON 1 | ON 2 | OFF 2 | OFF 均值→ON 均值 |
| ---: | ---: | ---: | ---: | ---: |
| 28.491 | 27.964 | 28.278 | 31.013 | 29.752→28.121，−5.48% |

两侧均开启本轮 CPU descriptor handle retention，因此这组只比较泛光候选开关；不能把
CPU 优化贡献算给泛光。ON 两窗分别完整命中 570/551 条链，A/B1 死清除各同数取消，回退 0。
相邻 ON/OFF 差异分别 −1.85% 和 −8.82%，本轮有回退信号，不能报告提速。
OFF 自身前后漂移 +8.85%，ON 自身 +1.12%，缺少同窗口 CPU 调度/实际频率轨迹，
尚不足以确认回退大小稳定或把原因归为提交相位。

HAL Current skin 从 47.260℃ 到 53.369℃，CPU/GPU 从 70.948℃ 到 81.476℃，
status 全程 3；这是持续升温的设备条件，但后段 OFF 更快，不能据此单独解释因果。
v3 与 v4 不是两版本交错运行，CPU descriptor 实现、PID、时间与热状态也不同，
不能把两轮百分比 −1.82%/−5.48% 的差当成 v4 死清除修正本身造成的版本回归。
没有新的 v4 外部字节/纹理周期捕获，因此不能沿用 v3 计数宣称 v4 节省了实际带宽，
也不能将已证明减少的逻辑片元、采样表达式和死 pass 当成不存在。
候选仍保留可对照实现；本轮没有证明泛光是当前整帧的主要瓶颈，也没有稳定 FPS 收益。

证据：`.local/logs/bloom-quarter-v4-mali-abba-result.json`、各窗口 `thermal-before/after.txt`，
汇总 `.local/logs/bloom-quarter-v4-abba-analysis.json`；host 明细
`.local/logs/bloom-quarter-v4-host-sync-validation.log` 与 `bloom-quarter-v4-host-result.json`。
ABBA 结束已通过原生命周期 fresh save 2 并停止，未让 Mali 空跑。

### C 原位置线性 blit 候选（待真机验收）

原 Metal 直接省去最终四偏移采样 C，让既有 D 读 B1。本候选继续相同四个 shader 规则，
将未知读者可见的近似 B0 明确定义为 C 时刻 B1 的标准 LINEAR/CLAMP_TO_EDGE 放大，
并在原 C 命令位置通过已有 `vk_texture_copy_t2t` 兑现，不维护跨提交代理，不保存 C 的
Uniform/顶点，也不要求等于原四采样平均。D 仍使用原有 B1 临时映射；后续未知 B0 读者
和下一提交读取都得到已定义图像。

生产仅允许同格式 R11G11B10、单 mip、单层、单采样、非别名，要求实际格式支持
BLIT_SRC、BLIT_DST 和线性过滤。C 的 Samples Passed 贡献必须由原提交索引证明无消费者，
否则继续原 C shader 物化；不能用不产生遮挡贡献的 blit 替代活跃查询。链前瞻只新增允许
无内存输出的 Samples Passed ResetCounter，其执行位置不变；ReportCounter 不跨越。
仍使用原通用纹理布局/资源依赖与真实提交完成生命周期。若 C 前存在尚未编码、无副作用
的纯 B0 clear，复用 v4 完整覆盖证明取消，避免先落地一个死 clear-only pass。

预期变化是省去四采样 C shader、顶点/Uniform/PSO 绑定，增加一次标准全图 blit 以及其
transfer/layout 依赖；驱动可能用 shader 实现 blit，不能将逻辑上的单次双线性定义冒充
已经降低某个百分比的硬件纹理周期或 DRAM 流量。画质可能较少柔化、亮点边缘不同；
物化后再次采样还有额外滤波和格式量化，必须按实际画面验收。

Host lavapipe 的 R11G11B10 optimal features 为 `0xd583`，缺 BLIT_DST（`0x800`），
因此必须回退，绝不伪造能力强行录制。owned GPU fixture 可明确使用其拥有的 C shader key
和真实支持的 RGBA8 格式执行同一 blit 路径，原游戏 C key 被显式排除在此测试覆盖之外；
生产 RGBA8/RGBA16F、多 mip、混格式仍按门槛回退。测试格式在 fixture 结束清除。
这只能验证实际 GPU 路径与线性放大定义，R11 真机支持与净收益仍须单独核查。

更远的 lazy 路线保留为研究：如果下帧 B1 写入早于 B0 的完整替换，为保留旧 B0 内容就必须
在 B1 写入前兑现旧 blit，可能每帧仍执行一次；不能假定延迟本身能省整个 C。此次候选没有
引入该跨提交生命周期，也不根据营地或预测下一帧决定资源是否可丢。

候选 host 验证：29 组 OFF/ON 用例，异步提交 + Vulkan 同步验证 **872825 checks passed**，
同步提交 + 同步验证 **775423 checks passed**，两轮无 Validation/VUID/SYNC 错误；
含最新通用 planner/alias metadata 用例。owned 支持格式实际执行 C-blit，并对 B0 全部
1024 像素、三个颜色分量与独立 CPU 线性放大定义比较；活跃 C 查询负例保持 C+D 的
5120 samples。R11 能力不足真实回退，不能把 owned RGBA 的通过写成 R11 真机通过。
明细 `.local/logs/bloom-c-blit-host-result.json`；未采集真机帧率或带宽，不预报收益。
