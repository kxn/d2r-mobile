# 原生 Vulkan 后端

执行原 nvn_obj.h 命令记录、hle_nvn.c 对象/录制/翻译队列及 ARM64 跳板，
沿用 mtl_* ABI。游戏指令由手机 CPU 直接执行，不含 CPU 解释器、JIT 或模拟器 CPU 后端。
完整里程碑和未完成项见 [移植计划](../../../docs/ANDROID_NATIVE_PORT_PLAN.zh-CN.md)。
原游戏/移动端专用分支的用途、默认开关和移植状态另见
[专用优化清单](../../../docs/NVN_SPECIAL_OPTIMIZATIONS.zh-CN.md)。

## 已验证范围

PHP110 / Mali-G710 MC10 / API 36 / 实际 4 KB 页已运行三份真实游戏模块，显示原移动端中文选角与营地。
英文营地与商店面板也已在真机验证；语言沿原自定义设置切换，同 PID 切 HOME、原后台存档与 Surface 重建恢复后继续外推，生命值不变。
游戏模块不变。
触屏移动/瞄准击杀、回城、世界交互、物品拖放/买卖、技能绑定、保存重载有实机证据。
原宿主 35 个增强模块直接链接，原 v4 HUD 已显示真实数值。仍未达到完整可玩验收：
更多技能/地图/自然掉落、完整增强功能、帧率、长期稳定性与发行均待完成；当前原生固定3D1.0受工作条件影响约7–16fps，尚未达到30fps。

最近实验候选的Linux lavapipe/真机分别通过507392/507394项原NVN/GPU检查；默认关闭的新路径仍须独立性能验收。数量包含逐像素/通道比较，不代表独立玩法场景数。Linux 与 APK 使用 Khronos 同步校验；
Mali CLI 无校验层，必须与 APK 证据区分。覆盖：

- 像素/计算/遮挡、GPU 完成后写回/事件/计数记录、内部提交不推进游戏栅栏。
- 常量快照、未对齐存储、图像写入、BC 后备、半精度、视口绕序/剔除、缩放拷贝。
- 描述符池扩容、管线/视图/暂存复用、纹理/替换采样器在途资源回收。
- 时间戳查询跨块、提交尾报告、真实 GPU 时间戳及环槽失效。
- 原截图队列的延迟、裁剪、PNG 输出；另用 Pillow 解码核对 8×6 的全部 48 个像素。
- 原 Metal stepRate 对应的实例除数 2/3、非零 baseInstance、管线缓存隔离。
  支持设备用 VK_EXT_vertex_attribute_divisor；当前 Mali 没有此扩展，按真实完成栅栏读一致映射缓冲并展开属性数据，
  仍保留一笔原始 instanced draw 和原 InstanceIndex/BaseInstance。强制后备测试还覆盖同提交 GPU 更新源数据。
- 呈现丢弃在访问纹理/Surface 前返回、不制造游戏栅栏；用于后台存盘期间继续游戏拍。

6931 个游戏离线 SPIR-V 模块通过 Vulkan 1.1 校验：6927 个原翻译结果加四个原内建计数核。
新增原创夹具着色器另放测试目录，不计入游戏缓存数量。

## 构建

```sh
scripts/dev/build-android-native.sh --touch --tlsdesc
scripts/dev/build-vulkan-shaders.sh
python scripts/dev/build-android-apk.py --tlsdesc
```

部署见 [Android 外壳说明](../../android/README.zh-CN.md)。CLI GPU 夹具可用
scripts/dev/test-android-vulkan.sh；APK 的 --ez run_vulkan_tests true 才覆盖包内真机同步校验。
无预加载原生 TLS 路径见 [TLS ABI](../../../docs/ANDROID_TLS_ABI.zh-CN.md)。

## 实现与边界

着色器构建复用原提取器、job_for、事实表和 dmsh_xlate；按阶段分描述符组，补图像能力/只读声明，
逐模块验证。指纹覆盖翻译链、元数据 ABI 和工具版本。运行时对象变化不使离线着色器缓存失效。
当前 6931 模块中的所有反射绑定 count 均为 1，描述符数组仍显式未实现；数组纹理不等于描述符数组。

GPU 写回和事件只在真实栅栏后发布。内部读回、呈现与顶点展开提交不推进游戏 NVN 栅栏。
原写保护脏页代号驱动一致映射池镜像，Android 带标签地址先规范化。
常量和只读未对齐存储副本按实际 GPU 写代号复用，暂存缓存有 256 MiB 上限。
管线按实际静态状态（包括实例除数）缓存；纹理沿原快照提交代号和八次宽限期回收。
连续绘制保留相同目标的渲染通道，依赖/传输/查询按实际需求切分。

Android 以负视口高度保持 NVN 绕序，恒等 Surface 变换优先；等比留边，先裁源图防越界采样。
默认输出采用Android Surface原生像素（本机2772×1240）；原3D控制器仍可从0.75自适应至0.60。
受控对照使用固定3D1.0；输出覆盖与固定比例只用于诊断，不计作画质降低带来的优化。
GPU 耗时取实际 TOP/BOTTOM 时间戳，尊重 validBits/period、跨提交累计到原帧号；宿主等栅栏不是 GPU 读数。
实际呈现时间来自驱动扩展。未校准的 GPU/宿主时钟跨度、硬件指令/周期等不冒充有效测量；ELF 内存标为 RSS。

仍须对照原 nvn_mtl.m 的已实现分支逐项补齐：更多格式/视图/拷贝、运行时外推的进一步验收和图形诊断。
后几项增强路径仍不完整；链接宿主模块不等于实现了它需要的 GPU 工作。
原 Metal 本身未实现的操作须另列，不能把泛化 NVN 功能表当作原实现覆盖。未知实际操作明确失败，不静默跳过。
私有证据在 .local/logs/，当前 GPU 主记录为 android-vulkan-background-save.log 和 vulkan-background-host.log；
游戏观测 game-state.jsonl 含原玩家/HUD/交易/绑定，只读观测不等于所有游戏功能已验。

## 格式视图与 GPU 诊断

沿原 tex_upgrade_pfv 延迟升级：首次换格式时重建 MUTABLE_FORMAT 图像，GPU 拷贝全部 mip/数组层，旧视图与存储等真实完成后退役。普通纹理保持不可变格式。视图用途限制为采样、存储或附件，sRGB 采样视图不会继承不支持的存储用途。按视图格式清除保留 sRGB 编码。GPU 夹具覆盖同提交先写后升级、sRGB 编解码、原始字节、恢复普通视图，以及两层三 mip 升级时其他内容保留。兼容类按 [Vulkan 规范](https://docs.vulkan.org/spec/latest/chapters/formats.html#formats-compatibility)；延迟重建沿用原后端，也避开 [Khronos 指南](https://docs.vulkan.org/guide/latest/extensions/VK_KHR_image_format_list.html) 提到的普遍开启可变格式的性能代价。

--ez gpu_profile true 可开启实际 GPU 查询，按首个片元/计算着色器和大小归组，默认关闭。区域耗时可能包含依赖与重叠，不能当独占着色器时间相加；原生 GPU 夹具已在同步校验下覆盖。真实营地记录为 android-gpu-region-game.log、android-gpu-region-camp-state.json；计时工具不等于帧率已经提升。

图像着色器写用途现在沿原 tex_desc，仅由 NVN IMAGE 标记启用；存储图像夹具显式声明该标记。该改动收窄了过宽的用途声明，手机回归及真实营地正常；目前不宣称已取得受控性能提升。最新 GPU 证据为 android-vulkan-buffer-bytes.log、vulkan-buffer-bytes-host.log。


## 原拖货跟手图与纹理清除

Vulkan 已对应原 ghost_note/ghost_flush：按原四边形/货架格网命中，取最小矩形和
反射贴图使用频率最低的图标绘制，平移视口重放，再恢复状态。GPU 夹具检查图标选择及像素。
Android 的原位暗化与原触控布局共用 Canvas 缩放，手指进入留边时撤掉旧图。
真商店拖动、松手及边缘行为独立于 GPU 夹具检查；已验证拖动时收起原说明框，
以及沿原宿主交易路径出售卷轴再拖回背包买回，金币 0→1→0。购买前恢复货架按下点，
未直接修改玩家/物品内存。

对应原 clear_tex 的完整 XY 区域清除，可按视图的基级/层和 zoffset/depth 选数组层及
3D 深度面。整数视图清除保留原始位；float/ui 清除值按附件格式做数值转换；深度和模板
采用原来的独立参数槽。GPU 回读覆盖未改动的 mip/面、有符号/无符号值、整数格式重解释
以及深度/模板两方面。局部 XY 和带分量掩码的纹理清除，在原 Metal clear_tex 与本后端
均未实现。体纹理切片附件依据 [Vulkan 的 3D 图像兼容 2D 视图规则](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html)。


## 原调色表加亮

nvn_vk_bright.c 对应上游 k_lut_msl/bright_lut，在计算着色器 552f45c348a61b86 之后
执行，曝光策略直接调用未改动的原 dm_bright_k。复制刚生成的 3D 调色表，沿用原 PQ/逆 PQ
及高光保护，再在色调映射前写回；界面仍在色调映射后合成。Push 常量逐派发保留曝光值，
副本、描述符及被替换纹理均保留到 GPU 完成。不支持的 LUT 类型保留原表并记录诊断，沿原 Metal 行为。

宿主 GLSL 由 builtins.cmake/build_builtin.py 离线编译，并按 Vulkan 1.1 验证。
Linux/Android 的 SPIR-V 和头文件一致；记录源码/生成器/产物哈希与编译器/校验器版本，
APK 包含构建清单。此内置核独立于 6931 个游戏派生着色器，同 PID 切 HOME、原后台存档与 Surface 重建恢复后继续外推，生命值不变。
游戏模块不变。
无格式 storage 写入先检查特性和支持的浮点格式，依据 [Vulkan 着色器接口规则](https://docs.vulkan.org/spec/latest/chapters/interfaces.html)。

GPU 测试逐通道对照双精度 CPU 参考值和独立三线性采样，覆盖 8 位/半精度、非整线程组尺寸、
选定 mip 与其余级保留、alpha、高光保护、曝光跳过/上限及同提交两次重新生成原表。
真实英文营地命中 32³ RGBA16F LUT；k=1/k=4 截图和同步校验运行确认了原游戏路径。
该运行没有 LUT/同步错误，仍有既有呈现路径的 Surface preTransform 性能提示。
--ef bright_k 1..4（启动脚本 --bright-k）仅转接原 DM_BRIGHT_K 诊断；默认启动走原动态策略。
不据此声称帧率改善。


## 原冻结世界绘制

nvn_vk_freeze.c 对应原 frz_op 的世界冻结分支。原 mtl_world_frozen/mtl_host_latch/
mtl_host_apply 握手按提交锁存，窗口身份来自原 NVN WindowInitialize。首帧完整绘制世界，
跳过界面模板绘制；以后保留世界结果，只画界面及末尾合成，跳过原世界段的目标/清除/绘制/
贴图拷贝/计算。状态变更、事件、计数器、缓冲操作及纹理上传继续执行。无法识别帧结构
（没有纯深度趟）时什么都不跳；关闭面板后恢复完整绘制。

图形层不根据 Android 面板自行推断冻结，也不替代原宿主世界冻结握手。--ez panel_freeze true
（启动脚本 --panel-freeze）仅启用已有 DM_PANEL_FREEZE；默认与上游一样关闭。
直接复用既有 10–13 四个观测门，无新增游戏改写规则或 binary 变动。运行时外推见下节；
默认常态仍全画。原强制全画请求沿用提交锁存，现在按请求的完整帧数刷新冻结世界，
重画时仍去掉界面模板。这是对原比例/目标变更提示的必要补强，无新增游戏改写；一帧多次提交只消耗一帧请求。

GPU 测试覆盖真实 NVN 窗口、模板遮挡、面板缩小、嵌套 OP_CALL 列表、世界计算/拷贝/清除跳过、
事件/时间戳/上传继续执行、解冻及无法识别的帧结构。原 DM_NVN_TEST_FRZ_NEG 负对照按预期失败，
缩小面板后露出旧红色世界像素。Linux/Mali 同步校验通过。真实游戏冻结/解冻时怪物快照及玩家生命
一致；物品移动、后台提交和同进程冻结面板恢复有独立实机证据。game-state.jsonl 只读发布
world_freeze 状态。
同步校验下，真实商人出售/买回使金币 0→1→0，期间世界拍号不变；关商店后继续走拍，原世界快照及生命一致。
Android 已在现有前端桥补强慢帧查询等待与方向脉冲时长。证据汇总：android-vulkan-freeze-verification.json。
GPU 测试在冻结时把 16×8 世界目标替换为 8×4，验证两帧强制重画及下一冻结帧保留正确像素；
修复前读回旧红色像素。默认仍与上游一样关闭、调试显式启用。
真实游戏启用同步校验，入营地立即打开物品栏，在 0.75 比例冻结；未改动的原控制器随后依次降到
0.70/0.65/0.60，每次完整重画四帧，含初始完整帧共十三帧。原世界拍号不变，物品提示继续更新，
关物品栏后恢复走拍且原快照、生命一致。证据：android-vulkan-freeze-resize-verification.json。

## 字节范围缓冲与原帧率策略

缓冲拷贝及已录制的常量更新保留原字节范围，支持未对齐的偏移和长度。常量更新把录制时快照经暂存缓冲
拷入，不使用 vkCmdUpdateBuffer；零长度不发 GPU 命令。保留范围/重叠检查、屏障及完成后 CPU/影子写回。
沿原 mtl_sub_copy 及 [Vulkan 1.1 拷贝规范](https://github.com/KhronosGroup/Vulkan-Docs/blob/v1.1.130/chapters/copies.txt#L95)。
GPU 测试覆盖 128 种源/目标对齐与长度组合、同池/跨池连续传送、整池护栏、录制后源数据生命周期、
恰到池末尾的拷贝、GPU 写回后的 CPU 修改。修复前两份运行分别在旧拷贝/常量对齐检查处失败。

常量/存储描述符的对齐暂存也保留字节范围：只对最终 Vulkan 描述符偏移满足设备对齐，
不再把 4 字节限制误加到 GPU 拷贝的源偏移和长度上。沿原 Metal 按 GPU 地址绑定的行为，
不读取常量的游戏含义。新增 16 组常量/只读存储绘制覆盖偏移 0–3、长度 17/19，
同提交更新后的三个屏幕区域核对旧/新数据与缓存失效；32 组可写存储测试覆盖源/目标
偏移 0–3、长度 257/259，连续两次计算加一次 GPU 拷贝，比较整个 64 KiB 数据区及护栏。
修复前分别在 uniform/storage 对齐检查处失败；lavapipe 与 Mali 的包内同步校验均通过。
证据：vulkan-binding-bytes-before-{uniform,storage}-fix.log、vulkan-binding-bytes-host.log、
android-vulkan-binding-bytes.log。夹具不执行游戏模块，真实游戏回归另记。

nvn_vk_pace.c 沿原 dm_fps_setting/DM_FPS 策略与 nvn_pace_hz 整除刷新率档位。Android 没有 Apple
显示节拍对象，因此原 NVN 取图等待直接采用目标帧率；DM_PRESENT_HZ 与游戏出图间隔仍优先。
Linux 和原生 ARM64 策略测试覆盖缺省、30/60 覆盖、无效覆盖、实时设置、进程锁存环境及八种刷新率，
各 105 项，独立于 GPU 检查数。game-state.jsonl 发布 target_fps、pace_hz 及原 NVN 取图节拍实际使用的
period_ns；目标 30 帧不等于实际已经渲染到 30 帧/秒。
手机实际点按/滚动原自定义页并点击应用，同一 PID 验证目标及周期 60→30→60 往返。只用恢复世界后的
新鲜只读观测：原暂停菜单会停掉服务端观测，旧快照不能证明当前设置。Android 屏幕刷新率提示仍需另行对应。
汇总证据：android-vulkan-buffer-pacing-verification.json。原游戏模块与改写规则不变，最后普通启动保持英文与 60 档。


## 原运行时外推

nvn_vk_runtime.c 对应原 rt_* 通道/状态规则；nvn_vk_extrap.c 与离线 GLSL 对应
nvn_rtex.inc 的 rx_fs1。双精度逆矩阵和十个蒙皮着色器键与上游一致。保留原界面保护、
蒙皮深度/阴影/世界绘制、半透明特效标记、区域后处理、LDR 历史平移及相机/比例回退；
历史不足时全画。--ez runtime_extrap true（启动脚本 --runtime-extrap）转接原 DM_RT_AB=1，
默认与上游一样关闭。

Vulkan 保留 NVN D24S8，而 Metal 将其升级为 D32S8；两种原生格式均已识别和 GPU 验证。
支持模板导出的设备用原单趟方式，Mali 用 R8_UINT 洞标记加第二趟仅写模板的绘制。两者均
保护本帧界面，除原 0x80 洞位外保留所有模板位。历史/描述符/帧缓冲延迟到真实 GPU 完成后
回收。通用 vk_pool_read 仅等待与读取范围相交的排队写入（含事件/查询记录），然后读取
原 CPU 池；无关缓冲更新不再让相机常量读取提交/等待。外推专用模板策略完整留在
nvn_vk_runtime.c，通用绘制只消费最终管线状态。

夹具用独立标量三次采样参考核对颜色/深度/模板，覆盖平移、小数位移、单位/界面洞、
全洞回退、大世界坐标。模拟原通道顺序另验隔帧、旧单位去除、相机跳变、精确强制完整
帧数、客体栅栏不因内部提交增长。它检查模拟后处理前的不透明结果，不代表游戏最终合成。
lavapipe 两种模板路径通过同步校验，Mali 两趟路径的 D24S8/D32S8 也通过；强制无法创建
外推管线时仍完整绘制（272295 项）。

真实游戏已识别 D24S8，在营地与区域 2 实际编码重投影；经原触控移动、1→2→1 换区、
英文物品栏开关，以及同 PID 从 30 帧完整绘制恢复 60 帧外推均通过。截图已查看；更多
场景及受控图像/性能对照仍待完成，未宣称帧率提升。证据为 android-vulkan-extrap-verification.json、
android-vulkan-extrap-d24.log 及 android-extrap-d24-{game.log,eco30-state.json,eco60-state.json}。
同 PID 切 HOME、原后台存档与 Surface 重建恢复后继续外推，生命值不变。
游戏模块不变。


## 通用 NVN 对应

非索引 QUADS 现沿原 draw()/g_quad_ib：共用不可变 32 位索引，每四顶点转成 0,1,2,0,2,3，
first 作为 baseVertex。检查原 65536 四边形容量，不足四顶点的尾部不产生图元。带索引
四边形在原 Metal 与本后端均未实现。十一组 GPU 像素用例覆盖零/不足计数、多四边形、
非零 first/BaseVertex、两个剔除方向、与普通索引三角形混用及后续普通非索引三角形。
修复前夹具在 topology 7 处失败；测试只用自有着色器，不执行游戏模块。

vk_pool_read 在回放期间提供一致的只读 CPU 访问，不再按全局 GPU 写代号等待。串行 begin
已等完前提交；当前缓冲写回、事件、查询回调的范围决定所读字节是否必须等完成。begin
后刚修改的 CPU 值从原池读取，不读陈旧镜像。GPU 测试覆盖半开边界、其他池、无效范围、
CPU 修改、连续 GPU 拷贝、事件/查询发布及游戏栅栏不变。初版遗漏事件依赖，已保留该负
对照在范围修复前的失败证据。真实可选外推调用在一次稳定营地采样中完成 360 次读取，
均无相交等待；前后顺序采样的温度不同，不能宣称已证明受控帧率提升。

原 Metal OP_CLEAR_COLOR 本身也未实现部分颜色掩码的附件清除（nvn_mtl.m 记录诊断并跳过）；
Vulkan 对该未支持用法明确失败。这项不能误列为已经完成的 Metal 能力，也不应为补它
绕去修改游戏渲染逻辑。

两个执行层均分派 39 个已录制操作码，但源码分派齐全不等于语义全覆盖。更多格式/标志/
子类型仍须核查。Vulkan 已有深度/模板需求及同提交整块覆盖的写回前瞻，更多 load 省略仍在补；
跨提交预测、未写平面省写回须先按 Vulkan 有效性/设备能力决定，不能盲搬 Metal DontCare。
详见专用优化清单中的技术评估。多项原图形统计仍为占位值。这些通用/诊断缺口优先于继续增加游戏专用
优化。边界见 AGENTS.md 与移植计划；上述修改未改变游戏模块或改写规则。证据为
android-vulkan-quads.log、vulkan-quads-{export,mask,fallback}-host.log 及
android-vulkan-nvn-generic-verification.json。

## 通道深度/模板需求前瞻

nvn_vk_plan.c 对应原 pass_ds_need/ds_scan。mtl_sub_index 在提交开始按原 OP_CALL/OP_JUMP/
OP_END 及所有句柄展开顺序，再一次逆扫累计绘制状态，避免逐通道重扫余下列表；回放逐条
核对地址，顺序不符、嵌套/操作数越界或索引不可用就保留附件。mtl_exec_rest 的余句柄
信息已经包含在整提交索引中。DM_NVN_TBDR=0 仍关闭本项。

证明整趟没有深度测试/写入和模板测试时，颜色绘制及起始颜色清除不挂深度模板附件。
中途清除按原保守规则处理；只用深度或模板之一时目前仍挂组合附件并保留两方面内容，
尚未对应 Metal 的单平面省略。后续同目标绘制若需要之前省掉的附件，先结束旧趟再重新挂上；
管线缓存按实际附件签名隔离。原可选策略产生的最终深度模板状态也参与当前需求判断，
通用前瞻不识别游戏人物、相机或着色器键。

同一 GPU 的开/关对照覆盖 D24S8/D32S8、起始清除、后续才开启测试/写入、嵌套调用/返回、
跳转、后续提交句柄及同目标重设；共 40 组场景逐像素核颜色、深度、模板。关闭优化时
计数不增加，开启时仅证明无用的趟省略附件。lavapipe 与 Mali APK 同步校验通过。
真实游戏首次启动已记录省略路径实际命中；功能回归独立记录于
android-vulkan-pass-depth-verification.json。证据：vulkan-pass-depth-{host,mask-host,fallback-host}.log、
android-vulkan-pass-depth.log。Vulkan render pass 的写回策略由
[附件描述](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentDescription.html) 提前确定，与 Metal 结束时
可调整不同；需求前瞻本身不等于写回省略，后者的独立实现见下节。没有受控帧率提升结论。

## 同提交写回前瞻与延迟通道记录

对应原 sd_finish 的规则：本提交后面先整块清除同一附件子资源，且此前没有读者，则旧内容
不用写回。颜色、深度、模板分别判断；采样按整张纹理保守处理，附件则区分 mip/数组层。
片元/计算的读取来自原离线反射；拷贝两端、清纹理、先作附件绘制、局部清除和未知情况
均保留 STORE。提交结束仍未定的一律写回，不做跨提交预测。DM_NVN_TBDR=0 关闭。

通用绘制先录 Vulkan 次级命令缓冲，实际结束时才选择最终 render pass 的 storeOp，主缓冲再
开始该通道并执行次级缓冲。管线/帧缓冲与继承通道保留同一附件签名，依据
[Vulkan 通道兼容规则](https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-compatibility)
只改变 storeOp；查询、时间戳、顶点/索引/描述符状态也记录在正确的缓冲中。
次级缓冲及两份通道对象等真实 GPU 完成后回收。已完成的当前绘制不重复算作未来读者；
原跟手图等额外绘制不能把尚未执行的原操作错误标为完成。

原冻结/外推模块若可能过滤后面的操作，就通过独立边界声明该提交不能使用覆盖证明，
保留所有写回；通用层不读取相机或游戏状态。这些可选模块的进一步写回优化尚未对应。
写回查询已改为原 Metal 思路的按纹理事件分桶/二分索引，细节见下节。

GPU 夹具共 60 组同进程开/关对照，覆盖整块覆盖、局部清除、先采样/拷贝、其他 mip、
不同数组层/相同数组层、嵌套调用与后续句柄，以及 D24S8/D32S8 只清深度/只清模板/全清。
逐像素核对颜色与两个平面，并核对实际省掉的 store 数；部分覆盖和其他子资源保留旧值。
Linux lavapipe 两种模板路径与 Mali APK 同步校验均通过。真实营地也已命中省略路径；
功能验收见 android-vulkan-store-verification.json。日志为 vulkan-store-{host,mask-host,fallback-host}.log、
android-vulkan-store.log。测试数量包含逐像素检查，不代表独立玩法数，也不代表帧率已提升。

## 写回事件索引与逐项交叉核对

nvn_vk_store_index.inc 对应原 sdx 的事件分桶和稳定散列：提交开始只构建一次各纹理的
读取、附件使用、颜色/深度/模板清除事件；通道结束按回放序号二分定位后续事件，
不再逐次重放所有未来状态。不同 mip/数组层分开；共享/部分重叠存储的不同快照通过
按内存地址排序生成的别名邻接表查需求。未知读取或池边界作为保守的全局需求。

纹理描述符登记/失效和池范围变化推进代号；构建或查询期间代号改变，就退回原扫描器。
回放顺序不符、资源/事件容量超限及原可选命令过滤路径仍保留保守行为。
DM_VULKAN_STORE_REFERENCE=1 选择旧扫描器作功能对照；DM_VULKAN_STORE_CHECK=1，
或开启 DM_VULKAN_VALIDATE 时，逐项比较索引与扫描器，差异明确失败。
普通运行不做双份查询。只读统计记录构建、索引查询、原扫描操作、访问事件、对照、
差异与回退次数；原扫描器保留作诊断和失效后备。

原 60 组 GPU 开/关场景继续通过。另有 10 组纯数据流检查，**不执行这些流的 GPU 绘制**：
同存储/部分重叠快照、无关拷贝、未知池边界、建立索引后重登记描述符。64 条无关状态
命令的用例要求扫描器至少检查 64 条，而索引最多检查两条相关事件；该计数不含索引
构建成本，也不是端到端提速测量。lavapipe 两种模板路径和 Mali 包内同步校验均通过；
强制原扫描器的独立运行也通过。日志：vulkan-store-index-{host,mask-host,fallback-host}.log、
vulkan-store-reference-host.log、android-vulkan-store-index.log。真实游戏同步校验同时做
交叉核对，证据见 android-vulkan-store-index-verification.json。仍不宣称受控帧率提升。

## 首绘完整覆盖时省略附件读入

`nvn_vk_cover.inc` 对应原 `attr_read/first_draw_covers`，直接包含未改动的
`nvn_cover.h`。只证明物理通道的首笔实际绘制：位置来自反射证实的单个 float2/3/4 属性，
按原规则补齐分量，支持 first、16/32 位索引和 baseVertex；GPU 刚写过的数据先经真实完成
与回写再读。读取在本次绘制分配描述符/暂存之前，避免内部等待切换提交后持有失效对象。

仍检查实际 mip/layer 尺寸、视口、剪裁、剔除、深度裁剪、混合、颜色写掩码、a2c、逻辑操作、
深度/模板拒绝、discard、sample mask 和目标存储别名读取。无法证明就保留 LOAD。
只有已证明覆盖的颜色附件改为 `loadOp=DONT_CARE`；显式 CLEAR 不被覆盖，MRT 逐附件判断。
利用既有次级命令记录，在开启主 render pass 前选择最终兼容描述；不改游戏 shader 指令。

离线缓存 V2 增加原 `dmsh_build.raster_facts` 输出的保守事实；MSL 仅用于复用原反射提取器，
不编译或执行 Metal。V1 缓存仍能读，缺事实时不省 LOAD。链指纹包括原提取器、Vulkan
生成器/ABI 和 spirv-cross；元数据也单独记录 SHA-256。当前缓存 `vk-3a0ffe903acf` 的
6931 份 SPIR-V 与此前 `vk-f4ac07bbf82c` 全部相同：2344 份 VS 中只有 5 份能证明位置，
其余全部保守退出；不能声称优化了每个全屏绘制。

`deploy-android-shaders.py` 复核当前链、上游几何/提取器、逐份元数据重生成及内建核，
可另核 SPIR-V 与基线相同。手机核完 20792 个文件的完整哈希才切换目录，保留旧缓存；
不触碰 NSO、存档或 RomFS。新缓存已这样部署。

原创 GPU 夹具新增 30 种条件 × 优化开/关 × 默认/哨兵加载，共 120 次绘制场景，
逐像素对照并核实际省略数，覆盖 MRT、显式 clear、mip/layer、first/索引、剪裁/混合/深度/模板、
discard/sample mask、旧元数据、GPU 更新后的位置及首笔不覆盖而后续覆盖等边界。
Linux lavapipe 与含同步校验的 Mali APK 均通过 480145 项累计检查；14 项原几何单测也通过。
外部采集模式 host 为 480142 项，关闭自定义时间查询但保留 NVN 查询/事件/栅栏语义。
这些数量包含逐像素检查，不是独立游戏流程数。

原 LOADPROBE 的两种常数/NaN 哨兵选择已有对应，夹具验证默认及第一种常数；
第二常数/NaN、原通道末自动数剩余哨兵的诊断通路尚未完整验收，不能算已补齐全部诊断。
真实同步校验运行至少 1800 次免 LOAD，营地 1→2→1、英文物品栏、后台保存已过，
39615 次索引判断与参考扫描器一致，无同步校验错误。截图是场景回归证据，
不代表真实动态游戏帧做过确定性逐像素双渲染比较。模块和改写产物不变。

证据：`android-vulkan-load-cover-verification.json`、`android-load-cover-shader-deploy.json`、
`vulkan-load-cover-{host,mask-host,fallback-host}.log`、`android-vulkan-external-profile-regression.log`。
完整上屏间隔/厂商计数器方法见 [Mali 性能分析](../../../docs/MALI_PROFILING.zh-CN.md)；
目前不宣称这项 load 省略已带来受控 FPS 提升。


## 显式 Vulkan 编译配置

原翻译器的 subgroup 宽度缺省为 32。Vulkan 构建可按设备实测属性设置
`DMSH_VK_SUBGROUP_SIZE=16|32|64|128`；本次 Adreno 650 实测为 64。
`DMSH_VK_BALLOT_HELPER=1` 在原调用位置调用共用 DontInline ballot 辅助函数，
保留谓词、执行 scope 和返回掩码，规避复现的旧 Adreno V502 编译失败：七次 ballot
能编译，八次触发 `No UGPR spill support`。这是缺省关闭的显式驱动兼容选项，
不按着色器键或游戏状态分支；不启用此前拒绝的 AST 表达式优化。

两个选项均进入 Vulkan 缓存指纹、清单和每份反射记录。构建和部署使用相同环境，
修改翻译器源码后先重建：

```sh
source scripts/dev/env.sh
dotnet build dm/gfx/tools/dmsh_xlate/dmsh_xlate.csproj -c Release --artifacts-path "$PWD/.local/build/dotnet-adreno-ballot"
export XLATE="$PWD/.local/build/dotnet-adreno-ballot/bin/dmsh_xlate/release/dmsh_xlate"
export DMSH_VK_SUBGROUP_SIZE=64 DMSH_VK_BALLOT_HELPER=1
scripts/dev/build-vulkan-shaders.sh
```

Adreno 证据：包含分支内 ballot 的 8192 个真实 GPU 输出字与 CPU 参考一致。
完整 `vk-d0941f1dfe7c` 缓存的 6931 个模块逐个通过校验，原部署脚本全量核对手机
20792 件产物的哈希。该修正越过了启动计算管线错误，但 6 GB 平板随后在主菜单阶段
触发 Android LOW_MEMORY，完整画面/帧率仍未验收。此设备配置与旧 Mali 缓存分开，
不能把宽度 64 的缓存交给未验证 GPU 使用。

遮挡查询现对应原 Metal 的可见性槽范围：连续 render-pass 绘制在 samples 报告、reset
或 pass 结束时分段。每份报告保留独立前缀，包括同通道重置与跨提交累计。Mali 同进程
ABBA 在 2772×1240、比例 1.0 下平均上屏间隔 118.21 → 106.17 ms，未改变游戏、shader
或绘制内容。`--ez query_per_draw_reference true` / `DM_VULKAN_QUERY_SPAN=0` 保留旧逐
draw 查询作为对照；夹具、原游戏校验、完整上屏记录及限制见带日期的 Mali profiling 记录。
