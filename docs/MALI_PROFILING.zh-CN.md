# Mali 真机性能分析

当前设备：PHP110 / MT6983 / Mali-G710 MC10 / Android API 36。
本流程只分析真实 ARM64 原生执行，不使用模拟器 CPU，也不修改游戏模块。

## Load/Store 来源的证据边界（2026-10-17）

**不能将 LS 总量直接归因为寄存器溢出。** [Arm G710 计数器指南第 9 章](https://documentation-service.arm.com/static/64d495994a92140bb31d7b55)
说明该单元覆盖 buffer、image、shared storage 和 stack；普通纹理采样与 framebuffer
写回另有通路。LS 高还可能是顶点/常量/存储缓冲、存储图像或共享内存的正常访问。
SPIR-V 的 Function `OpStore` 只是局部变量赋值，不能按条数当作机器码实际 spill。

复用已有官方单片元着色器对照，无新采基线：只替换 `5c1ca4e7c3e4657c` 的 321 次
绘制模块，B-A-A-B 的 LS L2 读取/帧降 46.15%、LS 写入/帧降 36.87%；像素、片元 warp
及 varying issues 差异都小于 0.1%，最终 RGB 一致。原模块两项 SSBO 都只读，
无 `OpImageWrite`/atomic，`OpStore` 指针仅为 Function/Output。这支持存在显著的
编译器生成临时存储流量，但不能把全部读取（还有 UBO/SSBO 等）称为 spill。
MOC r56 对同一对模块估计 stack spill traffic 396→172、stack alloca 0；
手机为 r32，当前扩展列表不含 `VK_KHR_pipeline_executable_properties`，尚无实际驱动
机器码/栈统计的直接归因，不能把离线预测当作真机 spill 字节数。

证据：`.local/logs/ls-source-single-fs-existing-evidence.json`、
`ls-source-hotspot-write-proof.json`、`ls-source-current-vkjson.json`。
后续优化须区分显式资源访问、编译器临时存储和缓存行为；减少源码临时量或改变 SPIR-V
字节不自动证明机器码少访存，更不自动证明整帧达到 30 FPS。

## 当前执行规约：先用已有工具（2026-10-17）

遵守 `AGENTS.md` 第 12 条：工具能解决的问题，禁止自行重复实现。本轮新增的分段／
屏障计时、热切换及相应夹具已撤回；12 个运行时／测试文件按之前精确哈希恢复，
启动脚本同时恢复。记录归档于 `.local/archive/self-timing-attribution-20261017/`，
不再以这些打点报告作为原厂工具归因的主要证据。手机恢复到全部源码输入核对过的
`6b2c7221…` APK，`--external-profile` 关闭自定义 CPU/GPU 计时。

官方 gatord 9.9 重新采集 20 秒后，由 Streamline 自身的 `with-metrics` 模板导出
队列利用率。`streamline-return-scheduling-report/timeline.csv` 的 2068 个 10ms
分箱均值为片元 57.24%、顶点 14.91%、计算 6.01%；这支持查队列串行／依赖，不能
定位某条屏障的独占耗时。原始采集为 `.local/profiles/streamline-return-scheduling.apc`。
同次 CPU 栈已匹配实际加载的宿主、libc、Mali ELF 哈希后由 Streamline 解析，见
`streamline-return-scheduling-symbols.apc/symbol-inputs.json`、`streamline-return-cpu-report/`。
`dm_thread_enter` 是客体入口 tail branch，不得将其样本当作 32 字节宿主桩的成本。

本次设备枚举明确报告 GPU frequency counter 不可用。Streamline 随附的
`enable-mali-timeline-events.html` 要求驱动 r40p0+（部分 r36p0 提供预览）；本机 r32
已跑通硬件计数器与 CPU 栈，不等于逐 pass GPU 调度事件已跑通。先核对官方工具的
报告与入口，不得因能力差异自行扩写计时系统。

## 工具选择

### 官方回放的 RenderDoc 抓帧：已确认内存限制（2026-10-17）

完成原 HOME 新存档（asked=1 commits=2）后，停止真实游戏，改抓 Arm 官方
GFXReconstruct 回放器，避免同时运行两个大进程。RenderDoc 1.45 确认选择 Vulkan，
并收到抓帧进度，但 PID16313 在 11:04:09 被系统明确以 LOW_MEMORY 杀死。
官方 Perfetto 记录采样峰值 RSS 11.42GB、swap 2.92GB，Android exit-info 独立确认
内存不足。没有有效 RDC，也没有逐事件耗时。这次无线 ADB 正常，不能据此解释
此前调试服务停滞的原因。

未修改的官方 gfxrecon-optimize 将捕获从661987985减至378839504字节，仅删除
未用初始化数据。用官方回放 --screenshots 1 导出的原始交换链图比较，2772×1240
全部RGB字节与参考相同，SHA256 0e229cea5a24b434…；先前包含系统状态/导航栏及
不同显示区域的 screencap 比较已拒绝，不能当作等价表面比较。
溯源为 `.local/build/mali-camp523-official-optimized.provenance.json`，逐像素证据为
`.local/logs/mali-camp523-official-optimized-swapchain-comparison.json`。

精简捕获配合官方 softMemoryLimit=200MB 再试，PID17855 在11:06:59仍以LOW_MEMORY
退出，采样峰值RSS11.67GB。该选项只将部分捕获数据写磁盘，不是整个进程的内存硬限制。
两次均为明确失败，不能冒充pass耗时；继续用已跑通的Streamline/LWI与Frame Advisor，
不新增计时器。官方Perfetto文件为 `.local/profiles/renderdoc-replay-system-{second,third}.pftrace`，
分析为 `.local/logs/renderdoc-replay-{second,third}-perfetto-analysis.txt`。
厂商atrace文本有解析错误，另有一个ftrace事件不可用；内存计数器、Android日志与
独立exit-info共同支持内存不足结论，不能扩展成GPU驱动崩溃结论。

整理旧版官方API转换结果：2844个image、15664个buffer、94次内存分配，共4.658GB。
image需求合计2.099GB，其中RGBA8 UNORM/SRGB约1.826GB。仅属旧捕获的结构，
不等于当前APK驻留/带宽，更不能直接归因为7帧；buffer含别名，不能与分配重复相加。
见 `.local/logs/camp523-official-resource-memory-summary.json`。
原调试设置已逐项恢复一致，随后重新启动原生分辨率真实游戏。

也实际运行了工作站官方回放器，只用lavapipe和2772×1240的Xvfb，不占硬件计算GPU。
`-m remap`不能映射原Mali内存类型；官方`-m rebind`可以渲染，但宿主RGB平均绝对差1.10、
最大212，不宣称像素一致。用官方`renderdoccmd vulkanlayer --register --user`注册
宿主捕获层后，屏幕窗口路径仍在初始化时断开，日志含设备地址未启用错误；另一次
GDB运行明确以回放器VK_ERROR_OUT_OF_DATE_KHR错误退出，未证明SIGSEGV，
也未证明设备地址日志就是断开根因。官方离屏/帧边界路径因缺VK_EXT_frame_boundary
被工具明确拒绝。没有有效宿主RDC或手机远端计时。
证据见 `.local/logs/renderdoc-capture-host-official-replay*.txt`、
`gfxrecon-renderdoc-lavapipe-gdb.txt`、`camp523-host-lavapipe-pixel-comparison.json`。
诊断GUI/回放器均已退出。手机普通游戏PID18594已恢复原营地/HUD、无注入层、
原生2772×1240/固定3D1/async+stream，并再次完成原HOME两次提交存档与同进程恢复。
这不是新的帧率提升结果。

### 工具复核发现的兼容性修复（2026-10-17，非帧率优化）

`ReadTexelsStrided` 的旧实现会把 GPU 暂存中的行/切片填充复制到 CPU 目标，
并把目标的奇数字节步长误当成 Vulkan buffer-image 步长。新增回归先复现第 12 字节
填充被改写（128 而非哨兵 215）；现沿原 Metal 紧排读回、逐行复制，仅发布实际数据。
CPU 发布在真实完成回调内完成，内部等待返回前保留暂存和回调上下文。
主修复与测试为 `nvn_vk_texture.c`、`native_vulkan_read.inc`，不涉及游戏 binary 或 shader。

旧镜像夹取采样测试的 UV 落在 texel 边界。Adreno 实测返回邻接 texel (3,7)，
而用例期待 (4,8)；现改用明确的镜像 texel 中心，仍逐像素严格比较同一预期值，
不放宽容差。随后 Khronos 校验层明确报出 `VUID-vkCmdDraw-mipmapMode-04770`：
NVN 无 mip 过滤错误地创建 LINEAR mipmap mode，在 Adreno 的不可线性过滤 D32
格式上非法。现按原 Metal 的 NotMipmapped 对应 nearest mip mode 和 LOD=0，
回归额外覆盖非零 LOD clamp 仍读取基础级的原行为。不是强制降低游戏采样质量。

最终 APK `639d5e34…` 的 244 个源码输入、APK 与包内宿主 ELF 哈希已逐项核对。
完整回归：lavapipe 508933、Mali 508938、Adreno 505995，两个真机均无校验错误。
实际 Mali 进程 13540 已回到英文原营地，原角色经验 54、生命 55/55、原 HUD 与绑定
均就绪；2772×1240、固定 3D=1.0、external-profile、async+stream，未注入 GPU layer。
原 HOME 保存 `asked=1 commits=2` 后同进程恢复。此处没有新的性能受控对照。
6 GB 骁龙平板的新真实启动在菜单后被系统以 `LOW_MEMORY` 终止，exit-info 记录
PSS 4.5 GB；不能把 GPU 夹具全过当作平板真实游戏已通过。Mali 原营地 meminfo
约 7.36 GiB PSS，其中 Graphics 约 4.15 GiB，是后续工具抓帧/内存审查的实际依据。

Wi-Fi RenderDoc 注入重现了 ADB shell 停滞；没有收到有效 RDC，不能报告逐事件耗时。
用户重启后新端口为 `10.10.10.44:41937`，GPU layer 设置已恢复，perf_harden=1。
现有持久退出/崩溃记录未保存足以解释 10:30 故障的证据；重启原因为 userrequested。
另已退出一个遗留的本任务 RenderDoc 探测 GUI，并清理其转发。不得将此直接认作根因。
继续使用已成功的官方 LWI/Streamline；下次重现前先由 Perfetto 记录系统服务/内存事件，
不以自制计时补洞。

### 官方 LWI 逐帧报告已跑通（2026-10-17）

旧记录的 “Cannot find any frames” 已解决。官方 `streamline_me.py` 的
`--lwi-mode counters --lwi-api vulkan --gpu-timeline no` 可在本机 r32 驱动上加载
`VK_LAYER_ARM_LWI`，提供帧边界和 API 计数；它不依赖 r40 的 GPU 调度时间线。
原脚本的 headless 参数转发把 `run_game` 当作 gatord 参数，故本次保留已加载 layer
的真实游戏进程，再用现有 `capture-mali.py` 附加采集；未修改官方工具或游戏代码。

`--preset advisor` 仅为官方 gatord 选择 stock G710 Advisor 所需计数器，
补全 atomic、varying、primitive、fragment task/warp 输入，不实现采样或计时。
采集 `.local/profiles/streamline-official-lwi-advisor-full.apc` 共 30 秒，PID 11149，
APK `6b2c7221…`，2772×1240、3D=1.0、原营地。官方复现分析：

```sh
.local/tools/Arm_Performance_Studio_2026.5/streamline/Streamline-cli -pa -t json,html --clip-start=2000 --clip-end=28000 -d .local/profiles/streamline-lwi-advisor-full-clipped .local/profiles/streamline-official-lwi-advisor-full.apc
```

报告 `performance_advisor-261017-102710.json/.html` 已生成，无缺失 series 警告。
本次完整区间 JSON 缺部分汇总项，而 CSV 有值；用官方 clip 排除采集首尾后，JSON
汇总完整。其原厂结果：7.2 FPS、GPU 利用率 80.7%、片元受限分类 100%；每帧约
49.12M GPU cycles、28.12M fragment cycles、10.05M arithmetic、7.95M load/store、
5.03M texture、667.93 MB 外存流量，约 2346.5 draws。周期是硬件指标，不能相加冒充
独占耗时；外存指标是系统 GPU 范围；100% 是瓶颈分类占比，不是片元占满 GPU。
跨 pass 的 rendered pixels 和 overdraw 不能直接替代某个 draw 的冗余像素分析。
这仍未证明某条屏障或某个 shader 的独占成本，也没有达到 30 FPS。

首次较短捕获曾在同一进程内由约 130 ms 变为约 64 ms，不能当优化收益；后两次
30 秒采集仍约 7.2–7.3 FPS。设备有热限制，比较必须控制频率/温度和观测开销。
采集后已确认原 HOME 保存 `asked=1 commits=2`，恢复全部原 GPU layer 设置和
`security.perf_harden=1`，正常新进程 PID 12265 的 maps 已确认无 LWI/Timeline layer。

| Arm 官方工具 | 用途 | 采集边界 |
| --- | --- | --- |
| Streamline + gatord | CPU 调度/调用栈、GPU 活跃度、片元/顶点工作、纹理与外部内存流量、频率随时间变化 | 首选性能定位工具。硬件计数器仍需定期采样；CPU 调用栈和更多计数器会增加开销，按需启用。 |
| Performance Advisor | 从带帧边界的 Streamline capture 生成 HTML/JSON 报告 | 需要官方帧标注/LWI；并非仅有硬件计数器就能生成完整每帧报告。 |
| Frame Advisor / RenderDoc for Arm GPUs | 定位某帧的 API 调用、附件、依赖、绘制与实际像素 | 抓帧会扰动执行，用于正确性/结构检查，不能拿抓帧时速度作为正常性能。 |
| Mali Offline Compiler (`malioc`) | 按 G710 静态分析离线生成的 Vulkan shader：周期、寄存器、潜在溢出等 | 不需要运行手机；结果是编译器估计，不等于真实通道独占耗时。 |
| libGPUCounters（原 HWCPipe） | 独立探针确认 Arm 驱动硬件计数器能否访问 | 已用**未修改的 Arm 官方示例**确认 shell 和应用自身权限均可读取。不是游戏内的新采样器，也不作为逐帧归因工具。 |

来源：[Arm 工具总览](https://developer.arm.com/tools-and-software/arm-performance-studio)、
[Streamline 真机工作流](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/ams/streamline/)、
[Performance Advisor 工作流](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/ams/pa/)、
[官方 libGPUCounters](https://github.com/ARM-software/libGPUCounters)。

AGI/APA/Perfetto 可以补充 Android 系统/帧调度信息，但不替代 Arm 的硬件计数器分析。
手机当前 `perfetto --query` 有 SurfaceFlinger FrameTimeline 和 ftrace，未列出 GPU counter/renderstage 数据源；
这个限制**不等于 Mali 计数器不可读**，独立 Arm 驱动探针已经读通。

## 固定安装与来源

```sh
python3 scripts/dev/install-arm-performance-studio.py
```

安装 Arm Performance Studio **2026.5 Linux x86-64** 到
`.local/tools/Arm_Performance_Studio_2026.5/`；从 Arm 官方服务器下载，固定校验发布的 SHA-256，
安装记录为 `.local/tools/arm-performance-studio-install.json`。
不改变系统 PATH、不安装系统驱动、不占用工作站计算 GPU。

libGPUCounters 预检固定为 **2.5.0 / 378c7b59e2dae87cbbffb0ac9cf96ed13c939823**，
NDK r28c 构建的原示例分别在 shell 和 `run-as com.d2rmobile.nativeport` 权限下取得 10 条非零样本。
证据：`.local/logs/android-mali-official-counter-probe.json` 及对应 shell/app 日志。
计数器是系统 GPU 范围，不能把其他应用或系统合成的工作都算给游戏；累计片元周期还可能跨多个核心求和。

Streamline 9.9 / gatord 9.9 已对真实营地进程完成 15 秒采集，且已用官方 CLI 分析并导出计数器 CSV。
当前预设是 10 个 G710 硬件计数器，关闭调用栈、官方 GPU timeline layer 与截图。
`captured.xml` 实际报告 GPU/CPU sample rate 101 Hz；CSV 导出分箱不代表更高的采集精度。
采集前临时将 `security.perf_harden` 从 1 设为 0，结束后已恢复 1，不需要 root。
首轮在应用身份下无法自行设置该属性的失败日志也保留，不能算采集成功。

复现（先按下节以 external-profile 进入游戏）：

```sh
python3 scripts/dev/capture-mali.py --seconds 15 --output .local/profiles/new-camp.apc
.local/tools/Arm_Performance_Studio_2026.5/streamline/Streamline-cli -analyze .local/profiles/new-camp.apc
.local/tools/Arm_Performance_Studio_2026.5/streamline/Streamline-cli -report -timeline -default-template basic -scale 100 -output .local/profiles/new-camp-report .local/profiles/new-camp.apc
```

工具附加到当前 PID，不重启游戏；临时权限在 finally 中恢复，保留采集配置与厂商 daemon 指纹。
当前证据：`.local/profiles/mali-camp-official.apc`、同目录 `mali-camp-official.capture.json`、
`mali-camp-official-report/timeline.csv`。CPU PID 预检曾产生不存在的提示，但同一 PID 实际存在，
日志显示 perf_event_open 成功；目前只验收 GPU 计数器，完整 CPU 调用栈/符号归因尚未验收。
这份记录没有 LWI 帧边界，所以不能冒充 Performance Advisor 的完整逐帧 HTML 报告。
Mali Offline Compiler 已对一份现有游戏派生 SPIR-V 生成 G710 JSON 分析，未改其字节。
RenderDoc for Arm GPUs 1.45 已安装并验证版本，设备单帧抓取/重放还未验收。

## 避免把观测开销当成瓶颈

```sh
python3 scripts/dev/android-game-launch.py --external-profile --report .local/logs/mali-launch.json
```

此参数（APK Intent `--ez external_profile true`）关闭自定义 Vulkan CPU 计时/统计、
整提交和分段 GPU 时间戳查询。`mtl_gpu_ms` 报不可用，不伪造 0 ms；游戏 NVN 的时间戳、
遮挡查询、完成栅栏和回写语义不变。真实输入/游戏状态日志仍保留，不能宣称完全无观测开销。
与 `--validate`、`--gpu-profile`、逐绘制 trace 互斥。
启动脚本会停止现有进程，因此有游戏运行时须先经原后台保存确认新一次 `commits=2`。

推荐分开运行：

1. 正确性：开同步校验、自己的 GPU 夹具/像素对照；不以其耗时衡量提速。
2. 计数器：关自定义计时与 GPU 校验，Streamline 只选所需计数器；先不抓截图和逐调用 trace。
3. 时间线/调用栈：对已定位区间加需要的 CPU 栈或官方 GPU timeline，记录额外开销。
4. 单帧：只在确有问题时用 Frame Advisor/RenderDoc，检查资源/绘制/依赖。
5. 验收：相同场景、比例、设置和接近温度下，比无厂商采集与有采集的呈现间隔分布；
   单独检查采集开销，再比较优化前后。低频打印不是无开销的证明，原厂工具也不是零开销。

优先看 GPU 空闲与队列重叠、片元/非片元工作、外部读写、tile 写回/恢复、纹理/L2 行为及频率，
据此决定是减少通道往返、收窄屏障、改善提交流水，还是处理 shader 翻译质量。
遵守 [Metal→Vulkan 评估](NVN_SPECIAL_OPTIMIZATIONS.zh-CN.md)，不新增游戏着色器白名单/场景特判。

## 本机采集开销短对照

```sh
python3 scripts/dev/measure-mali-overhead.py --report .local/logs/new-overhead.json
```

在同 PID、区域/位置/生命/3D 比例不变时，顺序取“关—开—关”三个 12 秒窗口。
每个窗口只在起止读取 SurfaceFlinger 的上屏历史，不在每帧用 ADB 查询；检测 128 条环形记录是否溢出，
忽略 0 和未完成的 INT64_MAX 时间戳，计算 actual-present 相邻差。列含义按
[AOSP FrameTracker](https://android.googlesource.com/platform/frameworks/native/+/51de481/services/surfaceflinger/FrameTracker.cpp)。

首次对照（PID 11411、营地、3D 0.60）：

| 状态 | 实际新上屏帧数 | 平均 fps | 平均间隔 |
| --- | --- | --- | --- |
| 采集关（前） | 86 | 7.136 | 140.140 ms |
| 采集开 | 85 | 7.080 | 141.244 ms |
| 采集关（后） | 85 | 7.100 | 140.843 ms |

开启采集时平均间隔比前后关闭均值增加约 **0.54%**。证据：
`.local/logs/android-mali-collection-overhead.json`，包含实际每帧时间戳、温控和状态读数。
这是一次顺序短对照，不是跨场景/温度的零开销证明；灯光、频率仍可能漂移，也不代表某项渲染优化提速。

后续 V2 缓存运行又做一轮：关 7.581 / 开 7.356 / 关 7.425 fps，开启时平均间隔增加约 1.99%。
见 `.local/logs/android-load-cover-official-present.json`。两轮差异说明不能把一次 0.54% 的估计当作固定开销。

## 从实际 profile 修复 Android 覆盖层

性能工作先保证显示正确，再由当前 Android profile 选择；Metal 优化表仅作参考，不先补完清单。
CPU 样本中 HWUI RenderThread 占包含调用约 6.93%；Android 触控/HUD View 在画面不变时也按显示节拍不断失效重画。
现保留相同频率的原输入/状态轮询，只在绘制内容变化时重画。仅排除不参与绘制的绑定快照年龄，
继续跟踪触摸、图标、语言、HUD 数值和拖动跟手矩形。绘制代码/资源、游戏模块和着色器未变。

同进程两轮 ABBA：PID 17754，营地 (5225,6289)，生命 55，渲染比例 0.60：

| 实现 | 采样时间窗数 | 等效平均 fps | 平均间隔 | HWUI 重画 |
| --- | --- | --- | --- | --- |
| 原持续重画 | 4 | 8.10 | 123.46 ms | 1732 |
| 绘制内容变化才重画 | 4 | 11.23 | 89.02 ms | 0 |

平均间隔下降 27.90%（等效吞吐提高约 38.7%）。这是保留完整实际上屏记录的顺序短对照，不外推到所有场景/温度。
每窗拒绝环形记录溢出、进程切换及位置/生命/比例变化；证据为 `.local/logs/android-overlay-abba.json` 和八份 SF/HWUI 原始报告。
已目视检查英文营地/背包；原背包开关、移动往返、后台原存档 `commits=2` 和同进程恢复通过。
这属于显示/输入回归，不冒充运动世界两次渲染的确定性逐像素等价证明。

这里的时间窗是测量区间，不是屏幕窗口。两组均保留同样的两个显示层：Vulkan
`SurfaceView` 游戏层，以及由 Android HWUI 绘制的透明触控/HUD 层；没有将游戏画面转成 GLES。
上述收益来自避免无变化的 HUD 持续重画，不能当作移除第二显示层的收益。
静态 HUD 缓冲仍需与游戏画面合成；可能由显示硬件承担，也可能回退到 GPU，须看真机
SurfaceFlinger/HWC 的实际决定。尚未测得这部分独立成本，也未证明合入 Vulkan 后更快。
参见 [AOSP SurfaceView 与显示层说明](https://source.android.com/docs/core/graphics/arch-sv-glsv)。

复现（true 为旧实现，退出恢复默认实现）：

```sh
python3 scripts/dev/measure-android-render-toggle.py --control overlay_continuous --report .local/logs/new-overlay-abba.json
```

移除空闲重画后已重新采集官方 Arm 计数器：`.local/profiles/android-overlay-dirty-triage.apc` 和 timeline。
系统级计数器不能把所有流量独占归给游戏。CPU 使用 Android 官方
`/system/bin/simpleperf record --in-app -p PID --duration 15 --call-graph fp -e cpu-clock:u -f 99`，
主机符号化前先核对 ELF 的 SHA。`.local/profiles/cpu-overlay-dirty.data` 共 1466 样本、丢失 0；
FP 调用链可能在客体汇编边界截断，但自身指令地址可用。`vk_draw` 自身样本约八成落在 1184 字节的串行逐字节哈希循环，
已保留源码映射和反汇编，作为下一项按字哈希实验的依据。这是 CPU 指令归因，不是独占 GPU 用时。

后续清理了同一 CPU profile 指出的诊断热路径：每次提交快照
`DM_VULKAN_TRACE_FRAME/DIR`，关闭诊断时纹理绑定不再查询这两个环境变量或构造
`sampled-*` 标签，绘制也不逐次解析目标帧号。诊断目录使用自有副本，提交内环境变更
不会使回读路径指针失效；下一提交重新读取配置。绘制、着色器、资源和游戏模块未变。
主机 lavapipe 与手机 Mali 的 APK GPU 夹具各通过 489161 项检查，包含帧号过滤、
提交间启停、提交内目录寿命及真实 GPU PPM 全像素回读；同步校验未报错。
证据为 `.local/logs/vulkan-trace-latch-host.log`、
`.local/logs/android-trace-latch-vulkan-test.log` 和带 APK 哈希的
`.local/logs/android-trace-latch-verification.json`。这仅证明当前夹具正确性，尚未证明真实游戏净提速。

同一新 APK 经 RenderDoc 注入后重新进入英文营地，PID 7961 的新鲜 `--attach`
报告及 `.local/logs/android-renderdoc-before-selection.png` 留存。此轮只有营地出图和原输入
ready 观测，不替代完整游戏回归。Arm RenderDoc 仍未成功取得可验收的 Vulkan `.rdc`：
设备在注入运行期间出现 ADB 通道停滞，拦截自动 `adb root` 后仍复现，尚不能认定根因是 root。
所有主机抓帧控制进程已退出。后续 USB 调试重启后，已恢复设备 debug 设置并完成无注入普通运行；
见下节，不能将这次恢复归因于单一变量。

这份历史 APK 的清晰度也有独立限制：当时 Android `front_game_res` 掌机档固定 1280×720，而原 iOS
前端可按原生屏幕像素输出。原日志 `.local/logs/android-gpu-region-game.log` 已记录
0.75 比例对应 960×540、0.60 对应 768×432，均先由游戏放大回 1280×720，
再经 Vulkan 线性缩放显示在手机上。降比例仅影响 3D；原游戏 UI 保持游戏输出尺寸，
Android 触控/HUD 是另一个显示层。不能把此低分辨率基线的帧率当作原生屏幕分辨率性能，
也不能据此排除其他画质问题。后续原生像素支持与固定比例实测见下节。

## 原生像素与提交等待审查

Android 显示策略已对应原 iOS：默认原生 Surface 像素、保留原掌机档比例及冷启动尺寸覆盖。
新 APK 的 PHP110 实测游戏/Surface 同为 2772×1240、固定 3D 1.0；营地/物品栏/实际移动/
原后台存档通过，完整管线键核对 769423 次且同步校验无报错。
证据 `.local/logs/android-native-resolution-validation-summary.json` 和 `android-native-resolution-*.png`。

无 RenderDoc/校验/自定义计时的四窗实际上屏：原生输出 9.065 fps、110.312 ms，HWUI 重画 0。
同 APK、桌面模式、固定 3D 1.0、相同营地的 1280×720 对照为 11.735 fps、85.215 ms。
两者是不同冷启动的短顺序窗口，含温度/场景变化，不能用此证明某项代码优化的净收益；
也不能与旧掌机 0.60 比例直接比较。原生像素数是该 720p 对照的约 3.73 倍。
再切回原生后的四窗为 8.953 fps、111.697 ms，帧间隔比首轮原生高约 1.26%；
完整 native→720p→native 记录为 `.local/logs/android-native-resolution-comparison.json`。
手机最终保持原生输出、固定 3D 1.0。

Android Perfetto + 官方 trace_processor 已记录实际内核调度，无应用注入或 CPU 调用栈抽样。
原生 7.989 秒窗口中，翻译线程运行 2.460 秒、睡眠 5.364 秒，142 个完整睡眠段均由 GPU
完成线程唤醒；720p 约 7.943 秒窗口运行 3.014 秒、睡眠 4.800 秒，189 段也由完成线程唤醒。
这不等于 GPU 独占耗时；当前驱动未提供 render-stage 轨迹，厂商 systrace 元数据还有解析警告。
查询使用采集前后核对的实际 Linux TID，避免 FPSGO 元数据导致推断进程归属错误。
源代码已确认 begin 全局等待、present 拷贝 CPU 等待；原 Metal 的 allocator/arena 按完成栅栏
保活资源，不在每次 begin 全局等完。资源寿命清单、镜像数据依赖和实施顺序见
[提交与资源寿命审查](VULKAN_SUBMISSION_LIFETIME.zh-CN.md)。尚未删除这些等待，尚无异步改造提速结论。

USB 与已授权的 Wi-Fi ADB 均可用；无线连接无需额外配对。恢复 debug 设置及无注入普通运行
证据为 `android-renderdoc-cleanup.json`、`android-native-resolution-normal-ready.json`。
主机 USB 日志中的多次 reset 包含主动重连与用户拔插，旧设备端 adbd 日志不足以定位根因；
不能认定为 USB 线、root 或同时开启 Wi-Fi 所致。当前普通运行和原厂采集期间连接正常。
SurfaceFlinger 快照显示游戏和 HUD 两层均由 DEVICE 硬件合成；只证明此刻未转为 GPU 合成，
不证明所有场景都无合成成本。证据 `android-native-resolution-surfaceflinger.txt`。

```sh
python scripts/dev/capture-android-scheduling.py --serial DEVICE --seconds 8 --output .local/profiles/new-scheduling.pftrace
python scripts/dev/measure-android-render-toggle.py --control none --scale 1 --rounds 1 --cpu-stats --report .local/logs/new-presents.json
```

## 管线查找实验

管线缓存本来已有哈希表索引，问题是每笔绘制仍重新拼出并哈希整个 1184 字节键。
按 64 位计算哈希后仍完整比较键，两种对照索引共享已编译管线。移除空闲 HUD 重画后，PID 20131
两轮 ABBA 为 87.98→84.59 ms（11.37→11.82 fps），间隔下降约 3.84%。证据：
`.local/logs/android-word-hash-abba.json`；对应 APK SHA256 为
`5b82d0bfd11643bf9a5e02d1d7510f7d843ec87d7f83b81deb5db16b74d02699`。
顺序短窗口不能证明所有场景收益，也不能据此推算多项优化叠加后的结果。

后续再用静态状态代号和实际附件/图元/模板上下文，直接复用已解析管线；上下文不变时完全跳过组键和哈希。
动态资源地址、纹理绑定、视口、剪裁和深度偏移值不使静态管线状态失效；跟手重放在切入/恢复快照时显式失效。
reference 模式每笔完整准备，且绕过新增代号比较，将失效检查本身的成本纳入净收益基线。
开关按提交锁定，切换时使已解析状态失效；check 模式重建完整键并在不一致时拒绝。它们与哈希实现开关分别对照。

首轮 Mali 核对因第 724 字节不同而拒绝复用：这是从栈复制进来的光栅结构尾部填充。
现将 Vulkan 明确状态字段复制到清零键中，排除 ABI 填充；保留首次失败日志，不以主机单独通过验收。
Linux lavapipe 和 Mali 已通过 488629 项 GPU 检查。最终真实游戏校验进程在营地→区域 2→营地、背包开关和
原后台保存 `commits=2` 中完成 130056 次完整键复用核对，零不一致，也无 Vulkan/同步校验错误。
证据为 `.local/logs/android-pipeline-net-verification.json`、`android-pipeline-net-validation-game.log`
及此前校验截图/日志；早期版本另通过 429413 次真实游戏核对。

最终净成本 ABBA 对照已将原直接复制的状态更新纳入基线：

| 管线准备 | 平均间隔 | 等效 fps | 翻译线程 CPU 核秒/秒 |
| --- | --- | --- | --- |
| 每笔组键/查表 | 84.81 ms | 11.79 | 0.4244 |
| 相同上下文直接复用 | 83.24 ms | 12.01 | 0.4187 |

间隔下降 1.84%，翻译线程 CPU 时间率下降 1.34%，属于本次小幅收益，不能当低帧率的主要解决方案。
四个启用窗口中 632086 笔直接复用、128739 笔查表，复用占 83.08%；所有窗口新增管线数为 0，空闲 HWUI 重画为 0。
整个进程 CPU 时间率增加 1.90%，同期吞吐增加 1.87%，不能把翻译线程的下降说成整个进程 CPU 下降。
证据为 `.local/logs/android-pipeline-net-cpu-abba.json`，含精确 APK SHA、TID、内核原始 CPU tick、完整上屏、状态和温控记录。
CPU 时间只在窗口边界读取；驱动辅助线程可能继承 `nvn-translate` 名称，因此预检选该名称中累计 CPU 最大的准确 TID。
较早仅对照准备过程的运行仍在基线中保留代号检查，应以最终净成本对照为准，不使用那些局部成本运行宣称净提速。

```sh
python3 scripts/dev/measure-android-render-toggle.py --control pipeline_prepare_reference --cpu-stats --report .local/logs/new-pipeline-net.json
```


## 2026-10-16：停止试探性改动，补齐工具归因

用户要求先取得厂商工具的帧/阶段证据，再继续优化。尚未部署的遮挡查询批量读回候选
已从主线撤出，保存在 `.local/build/query-batch-pending/candidate.patch`；其 host GPU 通过
不代表已测得性能收益。原 ASTC 工具仅在本机作营地六张纹理质量样本，未部署资源，不能
把离线压缩结果算成当前游戏帧率优化。

Arm Streamline 9.9 新采集包含真实营地 PID 4860 的 15 秒硬件计数和 CPU 栈：
`.local/profiles/android-r32-vendor-attribution.apc`。官方 CLI 已导出调用树，但即使附加
实际 APK 的 ELF，函数仍显示 unknown；符号解析尚未验收，不能用模块占比冒充函数瓶颈。
原始记录保留，派生分析目录单独存放。Arm Frame Advisor 2026.5 的本地 MCP 服务已启动并
发现抓取/分析接口，尚未完成真实帧抓取。新版 GPU Timeline layer 的驱动要求与传统 Mali
Timeline 不同：官方 libGPULayers 说明基础 Perfetto render-stage 从 r47p0 引入，带 layer
元数据关联需要 r51p0（部分 OEM 回移至 r49p1）；2022.4 的传统 Timeline 也要求兼容 r40p0。
所以旧版工具不能自动补出 r32p1 缺失的驱动跟踪接口，但 API 抓帧、硬件计数与 shader 静态
分析应分别检查，不能概括为 Mali 没有工具。

用户已换入 Xiaomi Pad 6 / pipa / 23043RP34C / SM8250 / Adreno 650 / Android 14 API 34。
USB 序列 a1971f20；已授权 Wi-Fi ADB `10.10.10.54:43179`。USB 曾写入超时，重连后改用
Wi-Fi；客户端和服务端均是 SDK ADB 37.0.1，NATIVE 后端。MTP 弹窗不是 ADB 版本诊断。
驱动 GLES V@0502.0（2021-10-04），Vulkan 1.1.128，shaderInt64/timeline semaphore/stencil
export 均不可用。相同 APK 的基础 GPU 回归在 sampler 替换采样像素处失败，需保留失败并
定位，不能当成全套通过。游戏模块 28 件与着色器 20792 件已用原部署工具验哈希；46 个
RomFS 输入约 9 GB 已全量核对。尚未验收真实游戏运行或该设备帧率。

Google 官方 AGI 3.3.3 已安装在 `.local/tools/agi/`，下载记录为
`.local/logs/agi-install.json`。仅连接指定平板且禁止本机 replay fallback。设备发现和
GraphicsSpy 层注入能工作；AGI 的 GPU profiling 预检明确失败：系统未开放所需 profiling
接口，KGSL sysfs 开关不可访问。不能宣称完整硬件计数器/时间线已通。框架 GPU profiling
和 API 单帧抓取须分别验收；后者仍在处理。

HyperOS 阻止 AGI 后台查询服务及应用列表：临时 deviceidle 白名单十分钟自动到期；只对
新安装的 AGI helper 将已观察到拒绝的 MIUIOP(10022) 从 ignore 改为 allow 后应用发现恢复。
完成采集后恢复该 op 的 ignore。中断 AGI 抓取未自动清理 GraphicsSpy 的全局选择，已核对
并把本次设置的四项恢复为先前 unset；每次捕获结束都必须检查，不能带着残留注入测正常帧率。
首次离屏夹具注入因同时开启校验而在 CreateInstance 返回 extension-not-present，0 字节
trace 不算成功。后续改用无校验真实游戏抓取，并将普通运行、抓帧、计数与像素校验分开。


## 2026-10-17：厂商工具实测，Wi-Fi 设备与启动兼容性

旧 MTK / Mali 手机通过 Wi-Fi ADB `10.10.10.44:40065` 工作，不依赖 USB。
Streamline 已有真实营地硬件计数与 CPU 栈捕获，但函数符号仍未验收，不能把模块占比或
采样相关性称为确定的绘制瓶颈。用户要求停止尝试 AGI；后续优先验证 Arm 厂商工具。

Qualcomm 官方 Snapdragon Profiler **2026.9** 已从用户下载包安装并通过 Wi-Fi 连接
Adreno 650 平板。包 SHA-256 为 `1a0c20a9ec220dc47a6ebaaab17db892f99e1946252e335db492b9db4338e55c`；
安装记录 `.local/logs/snapdragon-profiler-install.json` 登记全部组件。Mono 6.12、GTK#
3.24.24.38 及隔离的 LLVM 21 libc++ 运行库已就绪。官方工具的 GPU 插件实际读取计数器
时被内核以 `IOCTL_KGSL_PERFCOUNTER_READ errno 1 Operation not permitted` 拒绝；
`.local/logs/tablet-sdp-counter-permission.txt` 保留原始错误。连接和显示指标列表均不算
计数器采集成功；Vulkan Trace / Snapshot 也须独立验证。官网旧 FAQ 的 root 要求不能
取代实测，较新的官方发布说明与安装包手册支持 debuggable 非 root Vulkan 应用。

Arm Frame Advisor 2026.5 在 Mali 手机上的首次加载失败有明确平台原因：
`GraphicsEnvironment: Debug layer app 'com.arm.frameadvisor.replay' not installed`，
虽然工具 APK 已安装。Android 包可见性过滤隐藏了其调试层提供者。调试壳现为 Frame
Advisor、RenderDoc 和 Qualcomm ARM64 层声明精确 `<queries>` 包名；不请求读取全部应用。
修正 APK SHA 为 `cda2e59c0f7390360f440e08201678b3106eb73177c1a815b455525f81ed9102`。
Mali 全量 GPU 回归通过 492843 checks，包含 VMA 的 4104 个小缓冲与真实 GPU 复制；
`.local/logs/mali-vma-full-vulkan-test.log` 保留结果。修正后 Frame Advisor 已连接真实游戏
并推进 Vulkan 帧；完整单帧捕获、回放与营地阶段归因仍待验收。工具退出后检查并恢复
其 layer 选择，不能带着残留注入运行基线。

骁龙游戏启动另有独立兼容性问题，不与工具状态混淆：原计算着色器
`098a126f92bf533a` 在最小 Vulkan 程序里也触发旧驱动 `No UGPR spill support`。
缩减样例中七次 ballot 编译成功、八次失败。保持原谓词/调用位置的 DontInline ballot
辅助函数使真实着色器编译通过；64 线程 subgroup 的分支与多次 ballot GPU 测试校验了
8192 个输出字，全部正确。构建显式记录 `DMSH_VK_SUBGROUP_SIZE=64` 与
`DMSH_VK_BALLOT_HELPER=1`，从原输入重新翻译 6931 个 shader，逐个验证并核对手机
20792 件文件哈希，链版本 `d0941f1dfe7c`。未改游戏模块字节或玩法逻辑。

该修正使平板越过计算管线创建，但两次主菜单启动均被 Android `LOW_MEMORY` 终止，
系统记录应用 PSS 约 4.4 GB、物理内存约 6 GB。故平板尚未验收真实营地和帧率。
VMA 分配上限和无用 CullDistance 声明的 VUID 已修正，不代表整体正确性与性能已验收。


工具接通后的验收补充：菜单 Frame 100 已完成捕获、设备回放及官方 MCP 分析，
`replayWarnings=[]`。捕获路径为
`/home/dl/.local/share/Frame Advisor/logs/com.d2rmobile.nativeport_frame-100_20261016T162043Z_capture-1792167643155-1.fac`，
分析副本 `.local/logs/mali-menu-frame100-analysis.json`：Vulkan、57281 条 API 记录、
1066 次 draw、102 个 workload。该菜单帧不能代替营地性能瓶颈证据。

Streamline 宿主与 libc 的函数符号也已接通：精确匹配旧采集 APK 的 ELF 必须放在
`capture.apc/images/`，`session.xml` 的 `<image path="..."/>` 使用该目录内的相对文件名。
此前把 ELF 放在 capture 根目录或仅写绝对路径不生效。官方 GUI 的 Add image / Analyze
给出了有效布局，CLI 重分析后复现。派生采集为
`.local/profiles/android-r32-vendor-absolute-symbols.apc`，原始 APC 不动；函数/分线程调用树
在 `.local/logs/mali-streamline-complete-symbols/`，哈希和限制在
`.local/logs/mali-streamline-symbol-resolution.json`。私有驱动符号和原游戏匿名代码映射仍未
补齐；dm_thread_enter/run0 一类客体根节点不能按表面 self 百分比当成热点循环来优化。

Frame Advisor 的 Device data timeout 是等待推进/数据操作的期限，营地准备和低帧率注入
可能超过缺省 20 秒；官方 Preferences > Advanced options 支持增大该值。当前为 600 秒，
MOC 和 ADB 路径均设为本机已验证工具；抓取中的帧率和 gfxrecon 回放摘要不作原游戏 FPS。

Wi-Fi 营地抓帧：GUI 实时模式在真实英文营地抓到 Frame 523，输出为原生 2772×1240，
3D 比例固定 1.0，`external_profile=true`。抓帧前触发原项目后台存档并确认两次 commit。
官方 GUI 已保存 `.local/profiles/mali-camp-frame523-partial.fac`，ZIP 完整性检查通过；
官方 MCP 分析保存在 `.local/logs/mali-camp-frame523-analysis.json`。GUI 层级把该帧标为
522，统计 2356 次绘制、114 个工作负载；MCP 为 2353 个已建模绘制。这些工具口径不能
混用。精简渲染图为 64 个 render-pass 节点、6 个 transfer、3 个 compute，不等同于
完整的 114 个层级工作负载。

这是**部分捕获**：三次设备回放均在 `vkCmdBeginQuery` 崩溃，栈顶两层位于 Mali 驱动。
因此输出图像、overdraw 和回放网格尚未验收。原 API、shader、渲染图仍可分析，但编译器
估计值和绘制数量不能代替 GPU 耗时。崩溃栈、退出信息、主机日志为
`.local/logs/mali-camp523-*`。同机独立 Vulkan 程序在 render pass 内外的精确遮挡查询
均成功，尚不能把崩溃归因于后端的查询调用。只读检查原始 GFXR 后发现，工具在被捕获帧
之前的加载状态命令缓冲中生成了 49 组查询 begin/end；最小测试没有复现完整回放条件。
未修改运行时查询语义或游戏模块来绕过工具故障。

回放阻塞已独立复现并隔离：全新命令缓冲在没有记录过 reset/render-pass 操作的情况下
直接开始精确遮挡查询，在 Mali 驱动 PC `0x16c2d54` 崩溃。复用做过 reset 的命令缓冲成功；
在新缓冲里先 reset 查询池再 begin 也成功；新缓冲中先开始空 render pass 同样成功。
`.local/logs/mali-query-replay-{fresh-outside-crash,fresh-inside,fresh-reset}.txt` 记录区别。
这是独立 Vulkan 驱动复现，不是游戏补丁。

`scripts/dev/prepare-arm-query-replay.py` 实现限定的外部工具规避：将快照 reset-only
命令缓冲中已有的完整查询池 reset，移动到新 activation 命令缓冲。未知状态序列直接
拒绝，保留原捕获。文件长度、块数、首条恢复查询起的全部后缀不变，因此真实帧命令及
导出索引全部相同。输出 `.provenance.json` 记录输入、输出、脚本哈希。该营地原 trace
SHA 为 `ae06b439d77cfc6d6c6ce0f4a6e52df3d23e1cee68b5e91ed2529bd4b87358f2`，
派生 trace 为 `3f0d5b9887dee981dba13849e5618daeb519f26ee276154346506cb457dce5f7`。
未修改的官方 Arm 回放 APK 使用 `--validate` 完成派生回放，并导出 2772×1240 营地图像
`.local/logs/mali-camp523-replay.png`。原 FAC 仍是部分捕获，派生数据不能冒充未处理捕获。

后续回放已导出所选命令缓冲末次绘制的颜色/深度，以及原选择的 compute/transfer 资源，
约 882 MiB。这里只抽选诊断输出，所有真实帧命令仍然执行。
`.local/logs/mali-camp523-pass-end-dump-complete.txt` 记录完成；导出文件位于
`.local/profiles/mali-camp523-stage-outputs/`。启动后不发送按键，避免长时间同步回放期间
注入按键引发的独立 input-dispatch ANR。工具的回放 FPS、校验开销、导出速度均不作
原生游戏性能。逐 draw 图像、overdraw、完整 mesh 回放仍需分别验收。

资源导出一致性：draw 113285 的最终颜色附件，与另一遍不导出资源的回放截图，在
2772×1240 的每个 RGB 通道上完全相同，差异像素为 0。
`.local/logs/mali-camp523-replay-pixel-equivalence.json` 记录两者 RGB 哈希。全部 200 个
文件、923543711 字节的本地 SHA-256 在
`.local/logs/mali-camp523-stage-outputs-manifest.json`。这验证了两个回放路径的一致性，
不等同于与尚未取得的原始全分辨率帧回读逐字节一致。

新的普通运行 Wi-Fi 采集 `.local/profiles/mali-wifi-native-camp-r32.apc` 已附加真实营地
PID 21334，持续 15 秒，原生 2772×1240、比例 1.0。Frame Advisor layer 选择已清空，
自定义计时、校验、外推、冻结关闭，perf_harden 已恢复。原采集不动；派生分析
`.local/profiles/mali-wifi-native-camp-r32-symbols.apc` 使用与已安装 APK 精确一致的 ELF
`af1633c4aef26d6acac95ef817b8fb80fc4c4d79b8f4f4b5f159078315a7fd42` 及匹配的 libc/驱动。
官方 CSV 位于 `.local/logs/mali-wifi-camp-{functions,counters,metrics}/`。硬件/CPU 采样
均为 101 Hz。虽请求 GPU 主频，但 captured.xml 中没有该计数器；不能把原始周期总数
换算成按墙钟时间的 GPU 利用率。GPU 计数器仍是系统范围，r32 的完整 GPU 时间线与
逐 pass 耗时尚未取得。

游戏 frame-900 窗口记录上屏间隔 p50 115.65 ms、p95 132.18 ms，中位数约 8–9 FPS，
没有性能提升结论。新函数采样已解析 memcpy、纹理/缓冲查询和绑定工作；
dm_thread_enter/run0 仍包含未映射客体栈的归属，不能当成入口循环热点。原始
any-queue / GPU-queued 周期比约 0.6731，不是按时间计算的利用率。应结合帧图来选择
下一项可测实验，不能据此宣布某个 shader 或内存带宽就是瓶颈。CLI 偶尔在生成完整
报告后的 analytics 退出阶段崩溃，日志单独保存在
`.local/logs/mali-wifi-streamline-jvm-shutdown-crash.log`。

## 2026-10-17：经同进程对照保留按通道累计的遮挡查询

原厂抓帧显示约 2353 次逐绘制查询；Streamline 已解析 `vk_counter_draw_prepare` 下的
分配/驱动工作。原 Metal 使用每个 render pass 一个可见性槽，在 samples 报告/重置时
分段。Vulkan 现对应这个范围：首次绘制开始查询，samples 报告/重置或 secondary 渲染
命令缓冲结束前关闭，每份报告仍持有独立累计前缀。时间戳报告保留原命令位置；查询池
寿命和 CPU 发布仍由真实 GPU 完成决定。消除了逐 draw 查询开关/分配及大部分 256 槽耗尽
造成的通道切分，不跳过绘制、不改 shader/分辨率、不近似遮挡结果。

同一真实营地进程，APK `1dcbcf23c59d292c235818b2c6fdb6e4b0a606d70c0b590d124fe40c1afb707d`，
shader 链 `3a0ffe903acf`，固定 2772×1240、3D 1.0、external-profile、HWUI 重画为 0：
两轮 ABBA 平均上屏间隔 **118.2146 → 106.1723 ms**，缩短 10.19%，吞吐提高 11.34%，
约 8.46 → 9.42 FPS，尚非 30 FPS。每个上屏帧查询数由约 2430–2473 降到 48–51。
翻译线程 CPU/秒在吞吐增加时仍约 0.345 core-seconds/second，不能宣称整个进程 CPU
降低同样比例。完整上屏记录、状态/模式核对、温控和内核 CPU 时间边界在
`.local/logs/mali-query-span-abba.json`；画面为
`.local/logs/mali-query-span-{reference,candidate}-camp.png`，状态为
`mali-query-span-candidate-state.json`。

正确性：300 次绘制得到 76800 samples，报告前缀为 256 / 65536；累计模式仅用 3 个查询，
旧模式用 300 个。新增同通道报告/重置得到 256 / 256 / 512，跨两个提交累计为 512。
lavapipe 同步校验在两种模式均通过 492848 checks，异步累计模式通过 493129；Mali 原生
夹具通过 492848，APK 同步校验及原游戏英文营地校验也通过，未观察到 VUID。日志以
`query-span` 命名。第一次 APK 轮询读到了旧测试日志，已明确拒绝；有效 APK 验收先删除
旧测试日志，并同时要求新的 span 标记与通过 JSON，不能把旧结果算作本次通过。

新实现已作为默认；`--ez query_per_draw_reference true`、启动器
`--query-per-draw-reference` 或宿主 `DM_VULKAN_QUERY_SPAN=0` 可对照旧实现。切换只在
提交边界生效，测量脚本退出时恢复累计模式。未部署 GPU shader 优化试验：官方 MOC 对
捕获 shader 159 的 `-O`、`-Os`、SSA/local/scalar 变体均估计出更多溢出/读写开销。
MOC 使用 r56p1，而手机为 r32p1，这些只能用于否决/筛选，不能冒充真实驱动耗时。

默认累计查询 APK `0a88189a26b07ee7c286bee21f2b9882a4091efbcbfa638b587ef4d076933e9c`
在没有任何 query-mode Intent 参数时通过新的 APK 同步校验：492848 checks，有 span 标记，
并冷启动进入原游戏营地、确认默认累计模式。可回退 APK 保留在
`.local/build/android-apk/before-graphics-barrier.apk`。后续屏障实验单独受控，尚未验收收益。

屏障调查来自原厂捕获：该帧有 164 个 pipeline barrier，其中 160 个包含 HOST 目的阶段/
HOST_READ，159 个两侧均为 ALL_COMMANDS|HOST，证据为
`.local/logs/mali-camp523-barrier-audit.json`。第一项实验只去掉 pass 开始屏障的 HOST
目的域，保留全部 GPU 阶段/顺序、HOST_WRITE 来源，以及显式 NVN、传输、提交、回读处
原有完整屏障；CPU 可见效果仍等完整提交栅栏。这只是验证冗余 host-domain 操作的受控
假设，尚不能宣布它就是 GPU stall 根因。Vulkan [域操作规范](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
区分 GPU 消费与 host 可见性，[回读示例](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
在 CPU 消费前保留 host 可见性。宿主异步同步校验通过 493129，Mali APK 同步校验通过
492848，并确认执行 893 个 GPU-only pass 屏障；保留实验还须通过原游戏校验与帧率对照。

HOST 目的域单项实验已拒绝并移除调参代码。原游戏同步校验通过，但原生输出、同进程
ABBA 的平均帧间隔只下降 0.886%（`mali-render-barrier-abba.json`），不足以排除漂移并
验收有效收益。实验源码和 APK 哈希保存在 `.local/build/render-host-barrier-rejected/`。
已验收的累计查询优化继续保留。此结果只能排除 HOST 目的可见性本身是明显瓶颈，
不能排除过宽的 GPU 阶段依赖；后者仍须单独验证。

数据依赖审计（2026-10-17）：以下按生产者/消费者区分，不把 VS 普通输出误作 CPU 回读。
VS 输出经光栅化供 FS 使用；这里的缓冲写指显式拷贝/UpdateUniformBuffer、compute 或
带存储副作用的着色器。代码存在等待路径，不等于当前营地实际触发；新增计数分开记录。

| 数据流 | 当前兑现位置 | 粗糙之处及本轮处理 |
| --- | --- | --- |
| CPU 修改池 → GPU 顶点/常量/存储读取 | `vk_pool_sync` 在 begin/commit 比较脏页，更新一致映射镜像 | 只复制真正改变的页；仍按页保守判定在途访问，保留相交写保护；新增等待次数 |
| GPU Copy/Update/compute → GPU 缓冲读取 | 传输前和消费通道前的完整屏障；可写 shader 后 `vk_shader_writebacks` | 本轮记录所有 GPU 缓冲传输，未覆盖的写入强制完整阶段回退，不把它当 VS 输出 |
| RT/深度写 → 后续 FS 采样/附件操作 | 原来每个新绘制通道 ALL_COMMANDS→ALL_COMMANDS | 已改为有证明的 fragment/test/output 目的阶段；顶点图像读取、顶点存储写、缓冲传输及 NVN primitive/indirect 次序请求仍完整回退 |
| 后续 draw 顶点图像读取/存储写 | 新通道后第一次出现这类消费者时 | 若此前只保证 fragment 可见，恢复完整依赖；不能只看通道的第一次 draw |
| GPU 池缓冲修改 → CPU 游戏内存 | `vk_buffer_writebacks_flush` 在提交末截取修改范围，完成线程在 fence 后发布 | 仍保守截取全部登记范围；不能删除后就让 CPU 读旧值。统计范围数、字节数，以及内部提交造成的字节数，先确定实际规模 |
| NVN 查询 → CPU 报告 | 查询命令保留原位置；提交尾拷时间戳，fence 后发布报告 | 小结果异步发布；已验收按 pass 累计遮挡查询，不更改样本数。新增清零前未被报告的 draw 统计，尚未跳过任何查询 |
| 可选覆盖证明 → CPU 读索引/顶点 | 原 `cover_positions` 用可能等待的 `vk_pool_read` | 新增非阻塞读取；GPU 写尚未完成就保留附件 LOAD。原绘制照常执行，不为辅助证明强制提交/等待；独立 A/B 待验收 |
| 原有 `rt_*` 相机读取 → CPU 外推 | `nvn_vk_runtime.c:camera` | 属上游游戏专用外推，默认关闭；单独计数，不作为通用 NVN 的性能前提 |
| 原有商店拖动图标识别 → CPU 读矩阵/位置 | `vk_ghost_note` | 已检查待写范围，有风险即不识别；不等待；与通用绘制分开 |
| 不支持的实例除数 → CPU 展开实例数据 | `vk_draw` 的设备能力后备 | 当前后备会提交并等 GPU；支持扩展的 Mali 使用原实例绘制，不应将后备成本混入正常路径 |
| NVN ReadTexels → CPU 图像 | `vk_texture.c:read_image` 内部提交后等对应 fence | 这是明确的同步 CPU 读取 API；保留内容正确性。调试导图另列，常规 benchmark 不开 |
| 呈现图像 → SurfaceFlinger | acquire fence / GPU semaphore，swapchain 图像复用 | 异步路线已有独立验证；此前无明显性能收益，仍保留对照，不宣称单靠异步就提速 |
| GPU 完成 → NVN event / guest fence | 完成线程先发布 CPU 可见效果，再发事件/完成号 | 这是对外可观察边界，不能先报完成再补数据；仅内部提交不推进 guest fence |
| 资源复用/销毁 | 每份提交持有资源到真实 fence 和 CPU 效果完成 | 不能为提速提前复用 descriptor/查询/暂存，也不能只等最新帧的局部工作 |

阶段依赖修改通过宿主异步同步校验 493131、Mali APK 同步校验 492850，并进入原游戏营地
无观察到的 VUID。两轮同进程 ABBA：`mali-graphics-stage-warm-abba.json` 的吞吐 +4.30%，
独立确认 `mali-graphics-stage-confirm-abba.json` +2.02%。两轮均原生 2772×1240、固定 3D=1、
shader chain 不变、无管线编译、无 HWUI 重画。两轮 Android thermal status 均为 3，
绝对帧率有明显热漂移（约 9.3/9.7 与 6.6/6.8）；收益是各自同进程对照，不能将跨轮绝对帧率混比。
第一轮启动过早、窗口发生管线编译的记录 `mali-graphics-stage-abba.json` 已明确拒绝。
已保留该通用修改并设默认；完整屏障对照为 `--graphics-stage-reference`。

共享缓冲能力已用独立 Vulkan API 探针实测：`mali-shared-buffer-capability.txt`。
本机 DMA-BUF buffer 支持 IMPORTABLE、不支持 EXPORTABLE；OPAQUE_FD buffer 不支持导入/导出。
因此“把已分配 Vulkan 内存导出后 mmap 到原游戏指针”这条直接路线不可用。
这不证明所有共享分配方案都不可用；Android HardwareBuffer/平台分配后导入仍须另行验证，
不能据此承诺零拷贝。当前源码没有重映射游戏堆、没有改变游戏 binary。

营地依赖计数现已实测（`mali-dependency-audit-counter-delta.json`）：同步校验进程的
33.308 秒窗口内有 71 个游戏提交，每提交 2451988 字节/10 个范围的缓冲回写，内部提交
回写字节数为 0；覆盖证明实际等待、待 GPU 结果而放弃证明、上传冲突等待均为 0。
因此不能把“GPU 生成顶点导致覆盖证明等待”当成本场景低帧率原因。VS 普通输出无 CPU 回读。
同步校验窗口仅用于事件/字节计数，不作为帧率数据。

计数前瞻显示每游戏提交约 2357 次绘制的遮挡样本在报告前被清零、仅 1 次绘制需要计数。
曾实现严格同提交前瞻的无效查询省略，并验证跨句柄、嵌套及重复调用、跨提交累计、
报告/重置前缀与左右半屏实际 GPU 像素。宿主异步同步校验 493953、Mali APK 493674，
营地无观察到的 VUID。真实查询从约 47 个减到 1 个，但同进程、同 APK 的 8 窗 ABBA
帧间隔反而增加约 0.67%（`mali-query-liveness-abba.json`），没有验收性能收益。
该轮曾移除运行时优化和开关，源码/APK 保存在 `.local/build/query-liveness-rejected/`；本轮已按减少工作量原则恢复并默认启用。
保留前瞻统计和有意义的计数边界/像素回归。原厂 gatord 的 reference/candidate
occlusion 计数器捕获及 CLI 导出同时保留，不能将查询数量减少当成帧率提升。

原捕获的 Vulkan 缓冲传输另有逐调用审计 `mali-camp523-buffer-copy-audit.json`：
33 个 CopyBuffer，含 pool→pool 3 次/1048816 字节，pool→temp 24 次/5143604 字节，
temp→pool 6 次/1337380 字节。pool→temp 同时可能含对齐副本，不能全叫 CPU 回读；
temp→pool 也可能含 GPU 存储对齐拷回，尚不能仅凭该方向断言是 CPU 已知的 inline update。
下一项已验证候选把真正的 CPU 回写快照单独优先分配为 HOST_CACHED 内存，GPU fence
完成后经 VMA 按分配范围 invalidate，再发布 guest/shadow，上传仍维持原 coherent 策略。
Mali 已选中 flags=0xb（HOST_VISIBLE|HOST_CACHED|DEVICE_LOCAL、非 coherent），
APK 同步校验通过 493674，执行 473 次 invalidate；宿主缓存重用测试含 4 次 2MiB
GPU 拷贝、逐字节比较，总异步检查 493961。真实营地校验通过；性能 A/B 尚待完成。
依据为 Vulkan 内存类型和同步规范，以及 VMA 的 readback/缓存维护指南；没有省略 CPU
可见效果，也没有提前公布 guest fence。

缓存回读候选已验收并设默认。原生 2772×1240、固定 3D=1、同 APK/同 PID 的 ABBA：
`mali-cached-readback-abba.json` 吞吐 +5.63%，独立确认
`mali-cached-readback-confirm-abba.json` +2.82%。确认轮完成线程 CPU 占用由约
0.05360 core-s/s 降到 0.02777；每秒下降约 48%，这是 CPU 完成处理成本，不是 GPU
利用率。实际帧间隔分别下降约 5.33% / 2.75%。两轮绝对帧率受热漂移影响，不能跨轮混比；
APK SHA、测量脚本 SHA（确认轮）、所有实际上屏时刻及原始温度输出均在报告中。

Mali 原生异步回读/资源复用测试通过 493961 checks，实际 HOST_CACHED flags=0xb、
477 次 invalidate，含 4×2MiB 全字节结果与其他上传/旧 GPU 新 CPU 值的边界回归。
源码仍在真实 fence 完成后 invalidate/发布，再允许 guest event/fence 完成；
不会把 cached 回读缓冲当作 coherent 上传缓存复用。对照选项为
`--coherent-readback-reference`。此修改与 shader chain、游戏 binary、画质和绘制数量无关。

### compute 生产者与存储偏移依赖（2026-10-17）

普通 VS 的位置/插值输出经光栅化进入 FS，不能作为 CPU 回读或“GPU 生成顶点缓冲”的
依据。对原始 camp523 GFXR 按 API 调用审计，实际帧有 11 次 `vkCmdDispatch`；此前
精简渲染图的 3 个 compute 节点不是 dispatch 总数。通过捕获中 pipeline→module→
SPIR-V SHA→原 shader 包对上了 LUT combine、fluid sim update/cull/accumulate、
light bin depth bounds/bin/grid、terrain query 和原计数核；另有一个宿主 LUT 处理模块。
证据为 `.local/logs/mali-camp523-compute-audit.json`。这些名字说明已识别的实际工作，
不能仅凭名字宣称某个结果最终被 CPU 或顶点输入消费；仍须结合绑定范围追踪。

原始 CopyBuffer 审计中，5 个 temp→pool 操作存在此前相反方向、相同范围的复制，偏移
均不满足 Mali 的 64 字节 storage-buffer 对齐；其中一块 1310720 字节。候选方案将
Vulkan descriptor 起点向下对齐，离线 GPU shader pass 在经过证明的 uint32 runtime
array 地址上补偿 prefix。没有修改游戏 ARM64 binary、玩法、浮点运算或 draw 数量。
`OpArrayLength` 同步扣除 prefix，uint 索引加法溢出保留为越界；不支持的布局/指针流、
非整 word 的地址和超出 descriptor range 限制的绑定仍走原 GPU 对齐复制路径。
可写范围登记、shader 写后屏障、真实 fence 后 CPU 发布都保留。

工具链新增显式 `DMSH_VK_STORAGE_BIAS=1` 和 V3 元数据（mask、stage push offset、
slot count、ABI version），源码、工具版本和有效选项进入 chain。运行时仍默认参考
复制路径，实验选项 `--storage-offset-bias` 仅在提交边界切换；V1/V2 shader 保持兼容。
当前 V3 chain `9dad8209e657`，原 6931 个 shader/builtin 全部通过 spirv-val，5937 个
模块满足转换证明。对照 chain `7d4d057a8aa5` 从同一当前源码生成、关闭此变换；它与
已装 3a cache 的全部 4559 个 FS、22 个 CS 和 4 个 builtin 字节相同，VS/HS/DS 含此前
已完成的无用 CullDistance 声明删除，因此不能把它称为全量字节相同的 3a 重构。
手机全量验证 20792 个产物哈希后才发布新 cache，保留上一目录。

宿主 lavapipe 的 V2、V3 参考及 V3 直接绑定均通过 502173 checks。Mali 原生异步测试
通过 502173，APK 同步校验层通过 502175（零 VUID）；验证 runtime array 长度、
尾 0..3 字节、整池护栏、compute UBO 指针、compute 生产后 VS 消费、VS/FS 不同
prefix 与中途 GPU 更新后的逐像素结果。测试内参考复制 843 次，候选 372 次，直接
绑定 780 次；这是测试工作量，不能换算为游戏 FPS。实际营地 2772×1240 / fixed 3D=1
已通过校验并检查英文完整画面；实际游戏吞吐 A/B 尚在执行。

另做了输出尺寸诊断，未将降分辨率计为优化：相同 APK/3a shader/营地/固定 scale1，
原生 2772×1240 前测 10.2883 FPS，1610×720 为 12.2458 FPS，恢复原生后 10.2148 FPS；
每段均 4 个实际上屏窗口、HWUI 0 次、温控 status3，原始温度和 CPU 边界统计在报告。
像素数约降至 33.7% 但帧率只高约 20%，说明单纯像素负载不能解释大部分帧间隔，
不能据此直接断定 CPU 或 GPU 已满。1386×620 在角色选择出现裁切、未进入世界，
明确拒绝该次诊断，不记录游戏帧率。最终测试恢复原生尺寸。

同步审计另有限制：原 HLE 的 `CommandBufferWaitSync/WaitEvent` 空实现继承了上游
单队列、信号已在前序提交/初始化完成及现有屏障的前提。GPU-only 与 CPU readback
必须区分；若增加队列或进一步缩减相关屏障，需重新验证/实现这些等待，不能认为
NVN 等待在任何场景都可以省略。

对齐副本候选的同进程 V3 A/B 首轮吞吐 +6.68%，确认 +5.92%；但两侧 shader 都带地址
补偿，不能把这个数字直接称为相对旧 shader 的净收益。独立冷进程 V2 为 10.2067 FPS，
随后 V3 为 7.4049 FPS，存在性能/温控阶跃；首个 V3 冷测还因窗口内新建管线被拒绝。
这组跨进程比较不能验收，不能排除地址补偿带来的成本。**保持 shader 转换显式实验
开关，不设为默认优化**。完整判定见 `mali-storage-bias-performance-status.json`。

另一个有原厂 CPU 栈证据的候选针对 `vk_pool_sync`：Streamline 的翻译线程 330 个
有父栈样本中，内部呈现的 begin 路径含 73 个 pool-sync 样本，commit 另含 28 个。
原 begin/commit 同步遍历全池，任一页变脏后可触发整个大池的保护状态扫描。新候选
仅在提交时处理 `mtl_addr`/writeback 已登记的 GPU 缓冲访问页；图像上传仍通过自己的
源数据/dirty stamp，单纯呈现不访问池缓冲就不扫描池。按页保留上传代号，未访问页
的 CPU 改动不会被一次局部同步吞掉；旧全池代号与按页代号同时保留以支持提交边界
切换。所有已访问范围的上传与在途访问/CPU 回写等待仍走原检查。

选项 `--lazy-pool-sync` / `DM_VULKAN_POOL_SYNC_REFERENCE=0` 已验收并设为默认；参考
全池路径仍可切换。`pool_uploads` 分别统计全池候选范围、实际访问范围和调用保护的范围字节，
这些不是实际复制字节数。宿主 V2/V3、参考及局部路径均通过 502511 checks；Mali
原生异步通过 502511、APK 同步校验通过 502513，零 VUID。新增跨页回归证明：局部
上传不会清掉其他脏页的代号、图像类空内部提交不做池工作、全池/局部切换后字节
正确。原 ghost CPU 证明与实例后备展开改读 CPU 地址，消除对旧上传时机的隐含依赖，
原专用分支规则不变。原游戏校验、性能 A/B 与原厂复核结果如下。

按访问范围上传已验收并设为默认：`mali-pool-access-abba.json` 吞吐 +10.69%，独立确认
`mali-pool-access-confirm-abba.json` +13.36%；帧间隔分别下降 9.66% / 11.79%。翻译
线程每帧 CPU 从 36.49→28.31 ms、37.39→28.08 ms，均是窗口边界内核累计值，不是
GPU 时间。当前同机/同 shader V2 /原生固定 scale1 约 11.2 FPS，未达到 30 FPS。
参考开关 `--full-pool-sync-reference` 仍保留，切换时不会重置尚未访问页的上传代号。

原厂复核：原始 `.local/profiles/mali-pool-sync-{full,used}.apc` 与对应 capture.json
记录同 PID23459、APK1f221a…、V2 chain7d4d057a8aa5、相同营地/174 条已建管线和
完整模式/温度前后快照。匹配 APK 的 ELF 及原驱动/libc 加到副本 `*-symbols.apc`，
未修改原始记录。翻译线程样本 522→317，`vk_pool_sync` 的父栈样本 128→0，
`nvn_dirty_protect` 的父栈样本 86→1，符合消除全池扫描的预期；不能把采样数换算
为 GPU 利用率或游戏帧率。全池报告的工具 JVM 在写出有效 CSV 后发生 analytics 崩溃，
未将其算作游戏错误。第二次采集前首次 1 秒切换检查因状态尚未更新而中止；等待实际
模式为 false 后才重新采集，没有把失败步骤当成候选工具数据。

默认 APK（含游戏类别声明）同步校验 502513 checks、零 VUID，日志明确
pool policy reference=0、storage alignment reference=1。对齐 shader 转换仍为实验。
Android 此前将本包视为非 game，`cmd game list-modes` 明确拒绝查询；补齐
`android:appCategory="game"` 后返回 standard / [standard,custom]。未强行开启不支持
的 performance 模式。游戏配置显式禁止系统 backbuffer downscale 和 FPS override，
保持原 NVN 出图与 pacing；这是包装/观测修正，尚不宣称性能收益。依据为 Android
[Game Mode API](https://developer.android.com/games/optimize/adpf/gamemode/gamemode-api) 与
[intervention opt-out](https://developer.android.com/games/optimize/adpf/gamemode/gamemode-interventions)。

### 实际 compute 缓冲消费者与线程提示结果（2026-10-17）

新增只读工具 `scripts/dev/audit-arm-buffer-dependencies.py`，按 GFXR 编码解析 descriptor
更新、绑定、compute/draw 和 CopyBuffer，并检查原 dispatch→pipeline 对应及输入 SHA。
原始 camp523 的 11 次 dispatch 中，可依据已核对的 reflection 定位 6 个可写 storage
buffer 绑定；另有图像输出和宿主模块，不把它们误算成无写入。结果保存在
`mali-camp523-buffer-dependencies-verified.json`，没有修改 capture 或运行时。

| 生产者 | 已捕获的后续范围依赖 |
| --- | --- |
| fluid update，16 KiB | temp→pool→temp 后绑定给 fluid cull 和 accumulate |
| fluid cull，64 KiB | 后续 accumulate 的 compute storage 绑定，以及帧末复制 |
| light depth bounds，5 KiB | temp→pool→temp 后绑定给 light bin |
| light bin，5 KiB / 1280 KiB | 经 pool/只读对齐副本，在后续 675 次 draw 中绑定至 fragment set1 |
| terrain query，256 B | 80 B 子范围复制到另一 buffer，另有帧末 256 B 复制 |

这里证明的是已绑定的声明范围相交；不证明每个字节都被 shader 执行访问。复制传播
保守保留可能被后续写覆盖的来源；尚未解析图像别名/子资源、实际顶点索引访问范围，
更不包含 guest CPU load。因此不能从这些数据宣称 light buffer 必须给 CPU 读取，
也不能直接省略提交完成时的 CPU 可见性。普通 VS 输出没有作为 buffer 写入统计。

原 Metal 会声明线程工作期限，Android 原分支为空实现。本轮接入可选公共 NDK
Performance Hint：翻译线程以原 XJ_FRAME 周期创建会话，使用真实 monotonic 起点、
工作墙钟和线程 CPU 时间；支持时使用 AWorkDuration V2，GPU 时长明确填 0（未测量），
不把等待算成 GPU 执行。API 动态解析保持 API29 兼容，不改绑核、频率或温控。
真机 dumpsys 与运行状态均确认会话 active、16666665 ns target、持续报告、零错误；
游戏校验零 VUID，英文原生画面检查通过。GPU/宿主回归仍为 502513 / 502511 checks。

性能结果不支持默认启用：同 APK728e5f… / PID31908 / V2 chain7d4d057a8aa5 / 原生
固定 scale1，8 个 ABBA 窗口由 84.80569→84.68141 ms，吞吐仅 +0.147%，HWUI 均为 0。
首次尝试因窗口中新建管线明确拒绝，重新预热后才接受完整轮。保留显式
`--performance-hints` 实验选项，默认关闭；不能把接口调用成功当作加速。
完整报告为 `mali-performance-hint-warm-abba.json` / `mali-performance-hint-result.json`。

另外，默认按范围上传的原生正常出图实测为 11.483 FPS；异步候选冷进程为 11.959 FPS，
跨进程差异不足以验收异步默认。Perfetto 内核事件实测能取得此机 GPU frequency：一次
10 秒采集中为 214–383 MHz，翻译线程主要运行在 CPU4–7，运行 1.529 秒、睡眠 8.343 秒。
原始记录为 `mali-lazy-async-scheduler.pftrace`；频率/温度/帧率在运行中发生漂移，低频
既不能单独证明温控限频，也不能证明 GPU 空闲。dumpsys 的 Cached CPU 温度与 Current
HAL 温度不同，判断温度必须使用后者。保存后暂停同一进程降温，恢复首窗 12.55 FPS
随后又降至约 6.55；这不是代码收益，也尚未证明原因。未修改散热策略或设备频率。

### 提交时段、原生同步参数与图像布局实验（2026-10-17）

正常模式只保留提交首尾时间戳、未开逐 draw GPU query：稳定营地四个 300 帧窗口的
GPU 时段 p50 为 51.15–51.17 ms、帧间隔 p50 为 83.31–84.18 ms，翻译 CPU 约
24–25 ms/帧。原始 `mali-minimal-gpu-timing-native-final.log` 和派生 summary 均保留。
TOP/BOTTOM 时段包含提交内停顿，不是纯 shader 活跃时间；不能把这些时间全部解释
为 CPU 回读等待。另用 LLDB 在原生 HLE 入口读取参数，随后删除断点并正常 detach：
本次抽到的 2 个 QueueFenceSync 均 condition0（ALL_GPU_COMMANDS_COMPLETE）、flags1；
10 个 SignalEvent 均 location2（BOTTOM）。`mali-nvn-sync-argument-audit.json` 保存寄存器
证据。此样本不支持把当前等待改为“顶点完成”或“TOP 事件”提前放行，不代表所有场景
永远只使用这些参数。调试器采样不用于 FPS 统计，后续测量另起新游戏进程。

热点 fragment 的 SSA/无用代码整理候选仅替换派生 GFXR 中一个 shader；其余所有块
逐字节不变。原厂校验回放的 2772×1240 输出与参考 RGB **0 像素差异**。实际原厂无限
单帧循环 ABBA 为参考 89.55735 ms、候选 90.01755 ms（约 -0.51% 吞吐），没有收益，
未部署至游戏 shader cache。有限循环工具在最后一轮句柄回收出错，该轮被拒绝；无限
模式先通过 20 次循环校验，再用停止自身回放进程结束采样。回放数字不是游戏 FPS。
循环展开/分支控制提示的 MOC 指标不变，也没有据此部署。MOC r56 编译器指标不能
代替手机 r32 驱动实测。

依据 [Arm 图像布局说明](https://documentation-service.arm.com/static/67a62b17091bfc3e0a947695)
和 [Khronos layout transition 示例](https://docs.vulkan.org/samples/latest/samples/performance/layout_transitions/README.html)，
保留单 mip/layer、非 storage 的颜色图像在 COLOR_ATTACHMENT_OPTIMAL / fragment
SHADER_READ_ONLY_OPTIMAL 之间转换。depth、数组、多 mip、storage、非 fragment、
附件反馈和原始句柄调用保留 GENERAL。保留真实旧布局、原屏障/数据/绘制和整数精度，
未改游戏 binary。`--optimal-image-layouts` 显式开启，默认继续参考 GENERAL。

先用原厂新计数器 FRAG_TRANS_ELIM 验证：原基线 14861406 tiles / 448 eliminated；
同 APKba43a2… / PID11472 / 185 条已热管线的成对采集中，GENERAL 为 15052546 / 0，
候选为 16154088 / 4913893（约 30.42%）。计数为系统 GPU，不能用不同窗口的绝对
总量当作逐帧流量或 FPS；各自比值证明候选启用了这一硬件路径。原始 APC、模式/温度
前后快照和 CSV 均保存为 `mali-layout-{general,optimal}-pair*`。

但**不验收为默认性能优化**：最初额外全局屏障版本同进程 ABBA 帧间隔增加 6.97%；
收窄成图像自身、颜色→fragment 的依赖后仍增加 3.28%。硬件路径生效不等于净加速。
后者宿主 502513、Mali APK 502515 checks，原游戏校验与提交间启停零 VUID、英文原生
画面通过；性能报告为 `mali-image-layout-fragment-abba.json`。保持实验入口，默认关闭。

### NVN 提交内部的 Vulkan 分批实验（2026-10-17）

原生同步入口实测的 ALL/BOTTOM 仍按完整语义完成，不提前发布。实验在真实渲染目标变化
处、累计至少 256 draw 时提交前缀，单次 NVN 提交最多切 4 次，让 GPU 开始执行已准备
的命令，同时 CPU 翻译后续命令。切口强制保存附件；后续通道继续 LOAD。内部队列序号
可以推进，guest 围栏只在最终批次推进一次；查询前缀、跨批计数/重置、事件、回读与
资源退休保持顺序。`--stream-submits` 必须同时使用 `--async-submit`，默认均未启用。

新的真 GPU 回归用 6 个通道、576 次 draw、GPU uniform 更新、前缀与最终查询、事件
和完整像素核对覆盖切批及热切回参考模式。宿主 504076、Mali APK 504078 checks，
零 VUID，guest 围栏恰好推进一次。原游戏校验及原生英文画面检查通过。

APK387601… / PID18084 / V2 chain7d4d057a8aa5 / 固定原生 2772×1240、3D scale1，
未开自定义计时、校验或注入。第一次 8 窗 ABBA 中绝对速度发生明显阶跃（参考从
11.79 FPS 降到约 5 FPS），**不能把全轮汇总 +48.13% 当作干净收益**。同进程独立
确认的两个参考窗为 5.060 / 5.177 FPS，两个候选窗为 7.251 / 6.955 FPS；平均帧间隔
195.397→140.845 ms，吞吐 +38.73%。候选每 guest submit 切 4 次，每帧读回总量约
2.338 MiB 未增加，draw 数约 2344 不变，HWUI 为 0。候选翻译 CPU 时间反而稍增，
方向是并行重叠，尚不能据此宣称达到 15/30 FPS 或默认启用。报告保留在
`mali-stream-submit-{abba,confirm-abba}.json`。

速度阶跃尚未归因：较慢状态的 Perfetto GPU frequency 为 349–383 MHz，而较早正常
状态也出现 214–383 MHz，不能简单解释为该 GPU 频率降低。Current HAL 温度与速度
有关联但未证明因果。使用 SDK 自带、未修改的 Arm HWCPipe device 库读取 80 个样本
成功，驱动明确返回 has_gpu_cycle / has_cg_cycle / has_sc_cycle 全 false；缺失字段
不作为零值或实际频率。`mali-clock-probe-hot.jsonl` 与编译源码均保留。

新增可由 Perfetto 开启的 Android ATrace 标记：NVN command replay、submission
planning、attachment store index、Vulkan queue submit、CPU publish 和 render area
尺寸。异步 submit-to-observed-fence 包含队列、驱动、GPU 与 CPU 唤醒延迟，**不是
纯 GPU 执行时长**。未启用跟踪时标记休眠，不增加逐 draw query；采集脚本使用
`capture-android-scheduler.py --app-markers`。

### 厂商有效频率上限与无跟踪只读查询（2026-10-17）

从 APP 标记跟踪中取得 MTK 原始 `gpu_freq_max`：一组较快状态为 848，一组较慢状态
为 383。核对[一加官方 MT6983 GED 源码](https://github.com/OnePlusOSS/android_kernel_5.10_oneplus_mt6983/blob/oneplus/mt6983_t_13.0_oneplus_ace_2v/drivers/gpu/mediatek/ged/src/ged_dvfs.c)
确认它是当前有效 ceiling 的 MHz，而不是硬件最高档。源码把此计数器固定写到
**合成 PID5566**，不能因手机上碰巧有同号线程就归因于 SystemUI。源码是同平台
公开参考，不冒充当前 2026 固件的精确源码。`mali-vendor-gpu-ceiling-trace-comparison.json`
保存原始对照；此前单看 `gpufreq` 的范围不足以识别上限变化。

新增独立 GPL 诊断程序 `scripts/dev/probes/mali-ged-query.c` 和部署/读取工具
`query-mali-dvfs.py`；**不链接进游戏**。仅使用 `/proc/ged` 的 QUERY_INFO，以及全部
控制输入为零的 QUERY_GPU_DVFS_INFO。不给内核设置目标 PID、GIFT、频率、boost
或温控策略；通过本包普通 UID 权限访问，当前仅允许已核查的 MT6983 平台。
硬件最大档 848000 kHz 与有效上限 383000 kHz 分开保存。一次无跟踪查询读到系统
GPU load96%、当前366000、有效上限383000；load 是系统 GPU，不能说是某个 pass
的独占负载。DVFS 结构来自最近一次 GPU-done 更新，暂停出图时可能陈旧；其未设置
目标 PID的 target_fps/gpu_time=-1 **不是游戏配置或耗时**。

```sh
python scripts/dev/query-mali-dvfs.py --serial 10.10.10.44:40065 --deploy --report .local/logs/new-ged-query.json
python scripts/dev/measure-android-render-toggle.py --serial 10.10.10.44:40065 --scale 1 --control stream_submit_reference --mali-ged-probe --report .local/logs/new-ceiling-stable-abba.json
```

测量器核对本地源码及远端二进制 SHA，只在窗口边界读取。加入 `--mali-ged-probe` 后，
任一边界的有效上限改变就拒绝整轮，保留失败观测；未开额外 GPU layer、逐帧查询或
ATrace。上限不变仍不能保证瞬时频率/CPU 调度/温度完全一致。厂商实际 limiter ID
尚未暴露，不能把限频全部归因于某条温控或宣称找到软件绕过方法。Current HAL 显示
skin49.855°C/status3，与限制同时出现；其余 cooling state0 不能证明没有厂商温控。
`thermal` 类别虽在 atrace 列表中可用，本次 10 秒跟踪未收到 thermal/cdev 事件，
不是“没有温控”的证据。

Android16 的 libandroid.so 确实导出 GraphicsPipeline/Surface/AutoTiming 新 API，但
独立只读 feature 查询返回 sessions=true，graphics_pipeline/surface_binding/auto_cpu/
auto_gpu 全 false。不能只因符号存在就启用不支持的功能；本轮未加入这些模式。
原有可选 CPU hint 仍默认关闭，未把未测 GPU 时长伪填为等待时间。

`capture-android-scheduler.py --app-markers` 已去掉全局 gfx 类别，避免同时开启大量
驱动 API 标记。新的 `analyze-android-scheduler.py` 校验 trace SHA 后导出本进程标记、
队列在途重叠、按时间加权的 GPU ceiling/clock 驻留与原始解析健康统计。GPU 在途
区间仍不是纯 GPU busy；MTK 的 `[fstb]` 等普通文本会被 Perfetto 报成 systrace parse
failure，必须保留并检查，不能把它算作原生标记丢失或静默掩盖。

### 已验收的附件索引描述符缓存（2026-10-17）

系统时间线定位每帧约 4 ms 花在附件依赖索引。现在只在同一次索引构建内缓存纹理
描述符编号→已复制纹理 bucket；跨构建清空，descriptor epoch 变化仍丢弃索引并走原
保守扫描。对所有 handle 都为零的未知 shader stage 跳过空扫描；只要有一个非零
handle，就保留原扫描，未放宽依赖判断。所有事件序号、别名、清除和 LOAD/STORE
判断保持原语义。新检查覆盖同一 descriptor id 在连续构建里重新绑定不同纹理，以及
候选→参考切回。宿主 504100、Mali APK 504102 checks，原游戏同步校验/像素检查通过。

同 APK3daf2f… / PID26601 / V2 chain7d4d057a8aa5 / 原生2772×1240、固定3D1，
保持 async+stream 候选相同，只切描述符索引。在每个测量边界核对有效 GPU ceiling
始终383000 kHz：8窗 ABBA 帧间隔144.325→140.862 ms，吞吐 +2.46%；独立4窗
确认145.593→139.815 ms，吞吐 +4.13%。首轮翻译 CPU32.295→29.284 ms/帧。
无跟踪实际呈现数据保存在 `mali-store-zero-skip-ceiling-{abba,confirm-abba}.json`。
此前未监控 ceiling 的一轮末窗突变，被视为混杂，不据其负汇总做验收。

同 PID 的 APP-only 工具复核（不是 FPS 测量）：附件索引4.096→1.120 ms；两次跟踪
的 ceiling 均383MHz。原始记录为 `mali-store-zero-skip-{reference,cached}.pftrace`。
此项设为默认，保留 `--uncached-store-descriptors-reference`。异步/分批提交、图像布局、
storage bias 和 performance hints 的实验默认策略不随此项改变。

### 同进程降温复测与原厂 SDK 计数器对齐（2026-10-17）

已通过原 HOME 生命周期保存后，对原进程 PID26601 暂停 CPU 执行约5分钟，再恢复；
没有修改频率、温控或游戏状态。HAL skin 从49.9°C/status3降到43.434°C/status0。
暂停时 GED 值可能陈旧，恢复出图后才确认有效 ceiling 回到848MHz。相同代码和场景
四个实际呈现窗口为15.91、7.99、14.71、15.72 FPS，合计343个间隔为13.33 FPS。
这是运行状态变化，**不是新代码提速，也没有达到30 FPS**；即使 ceiling 不变，
瞬时工作频率也会变化，不能把边界 ceiling 检查等同于锁频。原始记录为
`mali-ceiling-controlled-cooldown.json`、`mali-ceiling-cooled-presents.json`。

新增独立工具，直接使用 Arm Performance Studio 2026.5 随附的**未修改** hwcpipe2
设备库及 G710 counter name table，SDK 内容不并入游戏。每100ms取一次 primary
计数器，共40份，保存各 core/内存切片原值、原始单调时钟和时间边界。不是逐 draw
计时、GPU layer、CPU 栈采样或游戏进程专属计数器。

```sh
python scripts/dev/capture-mali-frame-counters.py --serial 10.10.10.44:40065 --deploy --output .local/profiles/new-sdk-window
python scripts/dev/analyze-mali-frame-counters.py --input .local/profiles/new-sdk-window --report .local/logs/new-sdk-window-analysis.json
```

采集器核对 helper/SDK 源输入、APK 和 shader chain 指纹、同进程与场景、管线数量、
前后 GPU ceiling；采样结束后才查询呈现历史，直到历史完整包住窗口。分析器检查
40份连续样本、错误标志、SHA 和时钟域，用额外±1ms边界给出实际呈现数量上下界。
cycle 是计数，**不是 GPU 纳秒**；多核心 active 和多个内存切片不能当成互斥总耗时。

已对齐的两组原厂 SDK 窗口：848MHz ceiling 下63帧/4.147秒（边界范围63–64），
383MHz 下29帧/4.176秒（范围29–29）。慢组每帧 FMA +0.61%、LS short reads +1.34%、
外部读/写 beats +1.30%/+1.10%，未见帧率减半对应成倍工作量；两个 APK 的 GPU
实现、V2 shader 和运行选项相同。记录为 `mali-sdk-{cooled,hot}-canonical-analysis.json`。
单次窗口仍包含其他 GPU 客户端和呈现相位，不据此单独归因具体温控 limiter。

规范工具已在真实进程 PID5921 上重新构建/部署验证：40份样本、10 cores、4 L2 slices，
SDK 实测 AXI128bit、warp16；4.127秒内实际呈现30帧，边界范围30–30。
`mali-sdk-canonical-live-reference-bytes.json` 依据
[Arm G710 counter reference](https://documentation-service.arm.com/static/667183a7e98d6a2ff18647c6)
及记录的总线宽度换算字节数。外部流量可能命中 system cache；core/L2 分类与总线总量
不能重复相加。只有字节数不能证明带宽饱和。

### 已撤回的不可变描述符集复用实验（2026-10-17）

严格抓帧审计发现同一 pool/reset 生命周期中4717次分配含2540组完全相同的描述符。
候选在完成原资源解析和依赖检查后复用不可变集合；夹具覆盖多个提交在途、回收其中
一份资源池时另一份仍未完成，以及重新使用旧 key。宿主/Mali同步校验通过
504136/504138项，但检查通过不等于提速。

真游戏8窗ABBA在各边界848MHz ceiling下，分配/更新减少约54%，吞吐却−0.82%，
翻译CPU时间增加。随后只缓存不超过4个binding的小集合（覆盖抓帧所有命中），
独立8窗、边界383MHz结果仍−0.31%。中间一轮上限变化的对照整轮拒绝。
**已按精确备份恢复全部10个实验文件，并恢复已验收的36ed95c… APK**。这里撤回的是
VkDescriptorSet复用，之前已验收的附件索引描述符缓存不受影响。原始报告为
`mali-descriptor-set-ceiling-abba.json`、`mali-descriptor-small-hot-abba.json`；
被撤回源码和依据归档于 `.local/build/descriptor-set-intern-rejected-final/`。

APK构建入口现会先移除旧成功输出，成功后生成APK/宿主库/源码输入哈希记录。
此前一次失败构建留下的旧APK被明确拒绝，未计入新候选结果；后续真机检查同时核对
安装包哈希和预期检查数量。不能仅凭生成路径存在就部署，必须核对完整构建记录。

### 不可变 c1 常量折叠：工具回放通过，未进入正式缓存（2026-10-17）

针对编译器报告的热点 shader 寄存器溢出，在诊断副本中把96个静态 c1 读取换成逐位
相同的常量。c1 来自当前 shader metadata；运行时本就只在创建时复制一次并零填充。
候选不改变浮点精度、控制流或原游戏/正式shader缓存。严格工具校验仅有1个
VkShaderModule创建块变化，其余114586块逐字节不变。

Arm官方回放器同步校验无报错，原生2772×1240截图RGB全部字节相同。MOC r56估算
该shader的stack spill流量606→462bytes、Load/Store106→95.8cycles，但手机实际
驱动是r32，这些估计不能当作手机实测。4窗工具回放平均118.55→109.27ms（+8.49%），
**单帧波动88–171ms，只有结束边界ceiling一致；不足以验收真实游戏性能**。

进一步SDK对齐窗口的参考/候选实际呈现为46/36帧；每帧LS short读取+0.89%，按
raster quads归一后−0.31%，没有复现离线编译器所预期的明显访存下降。系统GPU范围、
帧边界相位、不同回放进程和瞬时频率仍是局限。故未将该变换加入正式缓存或宣称
游戏提速；真游戏已恢复36ed95c…/V2/native1/async+stream。证据见
`mali-camp523-c1-fold-{replay-provenance,pixel-comparison,loop-abba}.json` 及
`mali-c1-replay-hardware-comparison.json`。普通VS输出仍不归入SSBO写入/CPU回读。

### CPU已知来源的提交后发布：正确，性能尚未验收（2026-10-17）

新增通用NVN候选 `--cpu-known-copies` / `DM_VULKAN_CPU_COPY_REFERENCE=0`。本轮改为默认启用（见文末保留策略）。
所有GPU拷贝、绘制、原事件和真实完成栅栏继续执行。池间拷贝只有在来源不与当前、已封口或在途GPU/CPU写回
相交时才取得CPU已知证明，不为此等待。CPU快照在实际池上传之后、vkQueueSubmit之前，从上传影子数据生成；
inline UpdateUniformBuffer沿录制时有效载荷生成。后续未知/局部/相邻写入使合并范围回退GPU读回。
全部guest/影子发布仍在GPU完成后、事件/栅栏可见之前，源GPU覆盖或CPU后改都不能污染快照。

回归覆盖字节护栏、来源GPU先/后写、目的局部及相邻覆盖、已知完全覆盖未知、显式封口、录制后CPU编辑，
以及timeline挡住上一份真实GPU生产者时非阻塞回退。宿主507392、Mali507394项同步校验通过。
真实营地及触控移动/回原位置附近、原HOME存档通过，校验进程14111无VUID/FATAL；不是完整玩法验收。

同APKc083a247…/PID15270/V2 chain7d4d057a8aa5/原生2772×1240/固定3D1.0/async+stream，
8窗各观测边界ceiling383MHz，吞吐+1.52%；独立4窗仍383MHz，吞吐−2.17%。
每个NVN提交回读由2.338398MiB降至1.338242MiB，约42.77%，CPU已知发布1.000156MiB。
首轮翻译CPU29.406→29.761ms/呈现，完成线程5.665→5.534ms；不得把减少回读字节直接当作帧率提升。
该轮曾因 FPS 未稳定改善而保持默认关闭；这不否定减少回读的工作量收益，后续保留策略已修正。初次两轮因测量器未保留新统计字段而拒绝；已补齐字段并验证真实开关状态后才计入上述对照。
完整指标、输入指纹、校验与运行证据汇总于 `cpu-known-copy-result.json`。


## 2026-10-17：robustness 诊断未通过收益复核，转查 CPU 绑定热点

官方 Arm 重放捕获只修改 `vkCreateDevice.robustBufferAccess` 一项，其他 114586 个块逐字一致，
原生 2772×1240 的一帧 RGB 完全一致，验证无 VUID。初始重放计时出现 +23.32%，但不作为真实游戏性能。
预热至少 80 次重放后，以未修改的 Arm SDK 主计数器做独立 ABBA：每 raster quad 的 LS 短读
−0.012%、短写 +2.272%、外部读 beats −1.495%，未发现显著内存工作量减少；实际呈现也不稳定。
因此拒绝将该诊断升级为生产优化，游戏的 robustness 保持启用。
证据：`.local/logs/mali-robust-sdk-comparison.json`；初始 loop 报告已标注未通过复核。

随后在真实游戏 PID19413、APK c083a247…、V2 chain7d4d057a8aa5 上使用官方 gatord
抓取 15.361 秒用户态调用栈，管线数在采集边界均为 167，perf_harden 已恢复。
1488 个样本中翻译线程 TID19513 占 550；其 `vk_shader_bindings` 下 `memcmp` 有 58 个自身样本。
报告严格匹配实际加载的宿主 ELF、libc、Mali 驱动哈希，见
`.local/profiles/mali-post-dependencies-cpu-symbols.apc/symbol-inputs.json` 与
`.local/logs/mali-post-dependencies-cpu-functions/`。`dm_thread_enter` 是 tail-branch 客体入口，
其“大量自身样本”不能解释成 32 字节宿主入口耗时；客体匿名代码及回溯归因仍需区分。

代码核对发现 c0/c2 查重会读取 GPU 映射内存。新增通用候选（本轮改为默认启用）：CPU 自有不可变键、
上一份相同内容直接复用；按规范化字节比较，不依赖全局状态脏标记，引用只存活于对应提交。
它不删除 GPU 写回、同步或绘制，也不修改游戏逻辑。候选是否提速须以真机后续对照为准。


首版常量键候选的官方 gatord 复核使用同 PID22568/APK29d95fb8…，所有边界均173条管线，
营地、坐标、HP与原生输出不变。图形 bindings 下 memcmp 自身样本32→2，bindings 含子调用
样本108/886→74/872。仅属低频 CPU 采样证据，不是 FPS 测量。
原始记录与精确已加载 ELF 哈希见 `.local/logs/driver-constants-official-cpu-comparison.json`
和两份 `mali-driver-constant-{reference,candidate}-cpu-symbols.apc`。
首轮计时 ABBA 因有效 GPU 上限改变被拒绝。第二版保持候选算法一致，但仅候选模式分配/复制
CPU 键，避免给对照路径增加额外开销。主机508164、Mali508166项正确性检查通过；
首版候选另通过真实营地移动/返回、同步校验和原生命周期新存档。帧率收益仍待独立完成的对照。


V2 计时已完成：APK6b2c7221…/PID24638/V2 chain7d4d057a8aa5，原生2772×1240、
固定3D1、async+stream，所有观测边界有效上限383MHz。8窗翻译CPU30.020→27.908ms/呈现
（−7.04%），吞吐−1.32%；独立4窗30.334→27.971ms（−7.79%），吞吐+0.08%。
CPU耗时下降可复现，未验证帧率收益。该轮曾因此保持默认关闭；后续按工作量收益保留并默认启用，但不作为剩余 GPU 瓶颈已解决的证据。
证据：`driver-constants-v2-{measured,confirm}-abba-analysis.json` 与
`.local/logs/driver-constants-result.json`。


## 2026-10-17：按减少工作量保留优化

用户明确要求：从原理能证明节省工作、没有明显副作用的优化应保留，当前 FPS 未涨不能作为唯一撤回理由。
以后分别记录工作量、附加 CPU/内存/同步成本、可见画质和整帧性能。

- 驱动常量 CPU 自有键/最近值复用：默认启用，保留 `--mapped-driver-constants-reference` 和原 `driver_constant_reference=true` 对照。已有官方 CPU Profile 支持局部 CPU 开销下降；真实营地两次对照的翻译 CPU 耗时约降 7.04% / 7.79%，不宣称解决 GPU 低帧率。每个唯一 c0/c2 内容增加 0xa00 字节 CPU 键，随提交资源回收。
- CPU 已知来源免冗余回读：默认启用，保留 `--gpu-readback-copies-reference` 和原 `cpu_copy_reference=true` 对照。已知范围用不可变快照在原 GPU fence 完成后发布，原 GPU copy/未知来源回读均保留。已有营地记录回读量约降 42.8%；快照与 CPU 拷贝成本另记，测得约 1 MiB/提交的已知数据。未证明 FPS 上升不再阻止保留。
- 隔离 AST 片元：用官方 GFXReconstruct `--replace-shaders` 和既有帧，原厂 LWI/gatord 的 B-A-A-B 计数显示 GPU cycles/frame 约降 3.34%，fragment cycles 约降 5.82%，L2 LS bytes 约降 46.15%，画面 RGB 相同；回放 FPS 无稳定改善。保留候选及有效局部收益，整片元阶段启用的人物黑块是泛化默认启用的实际阻塞，而非 FPS 条件。
- 全阶段片元 AST、盲目 RelaxedPrecision：官方截图可见人物或材质黑块，暂不启用，候选和证据均留存。轻微浮点/画质差异可接受，黑块不属于这种差异。

其他历史撤回项另行复核：无效 occlusion 查询省略有已验证的查询数量收益，已恢复并默认启用，不能因旧对照约 0.67% 的间隔变化就丢弃；描述符集合复用与 optimal layouts 保留实验及附加 CPU/屏障成本证据，不能只看 API 数量下降就忽略成本。

本轮运行时修改只改变两个已验证算法的默认选择，未改游戏 binary、着色器缓存或分辨率。
干净构建记录与源码快照：`.local/build/retained-work-reductions-candidate/`；
设备同步校验：`.local/logs/retained-work-reductions-{mali,adreno}-validation.json`；
策略及所有候选证据索引：`.local/logs/optimization-retention-decision.json`。


遮挡查询省略已恢复：复用当前已存在的 RESET/REPORT 逆向分析，不增加额外整提交扫描。
只省略证明会在任意读取前被清零的计数，绘制、样本报告时间戳、跨提交尾部累计、原 fence 语义均保留。
冻结/外推或未能建立证明时使用原查询路径。查询 span 在失去活性时结束，随后真正需要的查询重新开始。
`--dead-sample-queries-reference` / `sample_liveness_reference=true` / `DM_VULKAN_SAMPLE_LIVENESS_REFERENCE=1` 可对照；
运行状态记录 `sample_liveness.reference` 和 `elided_draws`。测试同时执行参考/优化两个模式的单句柄、多句柄、
嵌套及重复调用，核对两半实际输出和 samples=128，不仅检查查询 API 数量。
最终产物/完整源码指纹：`.local/build/retained-query-candidate/`。

2026-10-17 补充：纹理采样结果精度与 LS 来源

现版 c2b7、ASTC、原生 2772×1240 / 固定 3D scale=1 的真实游戏官方记录为
14.656 FPS、44.415M GPU cycles/frame、547.484 MB LS reads/frame、551.802 MB
LS writes/frame。证据是 `.local/logs/retained-flow-original-official-summary.json`；
不同温度/频率下的前次 16.964 FPS 不能作为这一版涨跌的受控结论。距离 30 FPS 目标仍有差距。

只给浮点纹理采样/抓取结果增加 RelaxedPrecision，保留坐标、地址、常量、算术及控制流，
对既有完整帧的 61 个 fragment 模块用官方替换接口做候选/参考/参考/候选对照。
LWI/gatord/Streamline 的每帧结果：LS 读取 -37.27%、LS 写入 -20.41%、
GPU cycles -3.16%、fragment cycles -3.25%；像素/warp/varying 工作量约变 -0.45%。
回放 FPS -8.42%，没有将它写成游戏涨帧。原厂 MOC 的五个热点 stack spill usage
减少约 16%～68%；FP16 arithmetic 比例仍为零，不能宣称 ALU 已整体转成 FP16。
这支持大量 LS 与编译后的临时数据处理有关，不证明所有 LS 都是 spill。

证据：`.local/logs/sampled-result-precision-official-abba-comparison.json`、
`.local/logs/sampled-result-precision-id-controlled-moc.json`。另做纯汇编往返及保留原 ID
的对照，五个 MOC 成本证明收益不是重新编号的假象。整帧没有黑块/缺几何；平均 RGB
绝对差 2.23/255、约 4.88% 像素有通道差值超过 16，属于真实画质取舍，不能叫逐像素相同。

候选保留并接入原离线加工入口，`DMSH_VK_RELAX_SAMPLED_RESULTS=1` 显式启用；
开关进入缓存指纹和反射记录，不增加 shader 键白名单或游戏 binary 补丁。
它是新精度策略，并非恢复原游戏已有 half 指令：既有 GPU 编译器的可达 CFG 解码显示
五个原热点无 HADD/HMUL/HFMA，也无显式 LDL/STL。LDC 数量不能当成所有常量读取，
普通算术也能读取常量。原 NVDisasm 未能解出这些图形代码，故仅复用既有 GPU 解码器；
工具边界在 `.local/logs/original-hotspot-gpu-inventory-provenance.json`。

全纹理策略暂不改标准入口的默认选择，原因是浮点数据纹理/HDR 范围及其他场景的视觉
边界尚未验证；不是因为 FPS 没涨。下一步验证候选在真实游戏中的画面、资源与整帧性能。
相邻单次常量读取并入紧随其后纯赋值的独立改动已通过完整帧 RGB 对照，并保留于主线；
五个热点 MOC 成本相同，不宣称它已降低 GPU spill。

真实游戏候选 c5254bfb23f8 随后已进入英文营地：原生 2772×1240、固定 3D 比例 1.0，
官方计数为 17.366 FPS、42.241M GPU cycles/frame、493.324 MB LS reads/frame、
447.107 MB LS writes/frame。营地雨、人物、NPC、传送门及 HUD 正常，没有明显黑材质。
证据为 `.local/logs/sample-precision-native-official-summary.json` 和同前缀截图。
这是真实游戏计数，仍未达到 30 FPS；与前次不是受控 FPS 对照，不把 14.656→17.366
全部记成代码收益，也不把固定帧的 -37% LS 读取套用于实际游戏。原厂层配置与
`security.perf_harden` 已恢复，完成原存档后重新进入普通运行。

## 2026-10-17：TBDR 计时归因与完整 Vulkan RDC

已通过官方 RenderDoc application API、明确指定 Vulkan instance，取得当前真实游戏
完整帧：`.local/profiles/renderdoc-native-app-api/native.rdc`，580964547 bytes，
SHA256 `b971f360ec6b3d8b10bd58a258b9f43ebfade1d91edcf8c17f68e7e511921af8`。
捕获含 2404 draws、11 dispatches，原生 2772×1240、固定 3D 1.0。先在手机私有目录
完成 RDC，再复制到电脑；此路线捕获时没有外部控制器或文件回传，ADB 保持可用。
这不能反推此前断连的根因。全局注入设置在发出捕获请求前已恢复。

**不能把 TBDR 的延迟执行/计时归因问题称为 Mali 的特有限制。** Tile-based deferred
rendering 的命令记录、分桶和实际分块着色并非逐 draw 同步执行；某个边界触发的工作
可能包含此前累计命令。时间戳的具体归因还取决于查询阶段、驱动和工具的插入方式，
不能把一个较慢事件的全部成本归给该操作，也不能把重叠区间相加当作整帧 GPU 时间。
本次 generic EventGPUDuration 的 19.57 秒相加结果无效；24.84 ms 不能解释为单个
draw 的执行成本，也不能仅凭首次时间戳就断言该 pass 独占 24.84 ms。
参考 [Arm 对 Vulkan draw timing 的说明](https://community.arm.com/forums/f/mobile-graphics-and-gaming-forum/54832/vulkan-app-drawcall-timing)。

后续优先用官方 Streamline 的实际 LS/外存流量、运算/fragment/quad 数量与调度/阻塞
计数器，结合 RDC 的资源依赖、重复操作、附件 LOAD/STORE 和着色器绑定定位工作。
计数器分类可能重叠；访存多不等于带宽已饱和，离线 MOC 估计不等于手机实测。
只在执行边界明确、无重复计费且条件可比时用工具时间对照，不再按逐 draw 时间排序
推断瓶颈。当前 RenderDoc Arm counter RPC 返回空结果并超时，未取得有效硬件计数器；
不能把枚举成功算作采样成功。既有 Streamline 硬件捕获仍为实测依据。

资源审计证明：深度图 2693 → compute 19257 → compute 19264/19271 的 buffer 144
→ 后续全屏与主颜色绘制。中间存在真实跨阶段数据依赖，不能简单合并深度和颜色 pass。
主要颜色附件为 R11G11B10_FLOAT，不应误称多附件 G-buffer。

### 按实际冗余操作省去全屏出图中间副本

用官方 `CaptureFile.GetStructuredData()` 读取已有 RDC，不重新抓帧、不启动 GPU 回放，
确认尾部操作：2772×1240 RGBA8 源图 3591 完整复制到同尺寸裁剪图 3308，然后清黑
交换链 3266，再做完整覆盖 blit。源/目标偏移均为零，没有实际裁剪或黑边。

通用窗口层现在在整图输入时直接 blit，保留源图 GENERAL 布局和原可见性屏障；
只有部分裁剪仍创建独立图像，以保证线性滤波不读取裁剪范围之外。只有完整覆盖
交换链时省略清黑，存在 letterbox 时仍初始化黑边。未改 shader、游戏逻辑、分辨率、
缩放比例或游戏 binary。已验证源码/APK/ELF 247 个输入指纹，真机营地画面正常。

每个符合条件的出图减少一次完整图像复制和一次清屏，按未压缩 RGBA8 计算，省去
41,247,360 bytes 的逻辑读写；原裁剪图的实际 VkMemoryRequirements 为 14,643,200
bytes。这是操作/资源节省，不把它当作等量外存流量节省或某个独占 GPU 时长。
证据：`.local/logs/present-copy-elision-evidence.json`，原始结构化数据为
`renderdoc-existing-present-operations.json`。

官方 LWI/gatord 真实游戏 2–18 秒窗口为 18.851 FPS、41.268M GPU cycles/frame，
仍为原生 2772×1240、固定 3D 1.0、无外推/冻结。不是与前次 17.366 FPS 的受控 A/B，
不能把差值归给本改动，尚未达到 30 FPS。CSV 完整 1600 bins 已核对；官方 CLI
完成导出后在其 libAnalyticsCore 发送线程崩溃，错误日志和完整导出均保留，不伪称工具
正常退出。随后按官方手册设置 `ARM_DISABLE_ANALYTICS=1`，同一捕获成功重新导出，
CSV 与初次结果逐字节相同，原始采集未重做。记录为
`.local/logs/present-copy-elision-native-official-summary.json`。

### 受保护算术精度候选：实际资源退化，未进入默认版本

MOC r56 对部分热点预测可降低 stack spill/LS 成本，固定帧回放画面差异也很小：
RGB 平均绝对误差 0.00217/255，任一通道差异超过 16 的像素占 0.000378%，未见
明显黑材质或缺失几何。但这不代表手机 r32 编译器会得到相同机器码或资源收益。

已做两轮官方 Streamline LWI/gatord 交错对照，均复用已有固定捕获，不新抓游戏帧。
第一轮还混入 SSA 整理差异；第二轮将五个 PS 的 SSA 指令流和 ID 完全对齐，只让
受保护数据流之外的数值运算多 RelaxedPrecision 标记，其余 93 个模块字节相同。
所有 NoContraction、通往位观察/地址/控制/坐标/导数/深度/存储写入的反向依赖保持。

第二轮按相同 rasterized quad 数归一，LS 读取 **+6.17%**、LS 写入 **+10.48%**，
GPU cycles **+0.13%**；FMA/quad 基本不变，CVT/quad 增加。回放 FPS 约 +2.33%
仍不能作为减少工作量的证据，队列 endpoint stall 也非互斥的全帧等待时间。
所以保留实验和来源，**不默认启用的依据是实际资源成本退化，不是没有即时涨帧**。
未改变手机正式 shader cache 2ee3b4b91974。证据：
`.local/logs/protected-arithmetic-matched-official-comparison.json`，
`.local/build/protected-arithmetic-matched-replay/pixel-comparison.json`。

另用已有 RDC 结构化数据审计图像 usage：主 HDR 2808 创建时含 STORAGE，捕获内
仅见 SAMPLED/COLOR_ATTACHMENT views，没有 storage descriptor；但另有 7 张图像
确实进入 storage descriptor。不能全局删除 IMAGE/STORAGE 标志，也不能把单帧未用
推成整个生命周期未用。若评估延迟增加 STORAGE 用途，必须保持全部子资源内容、
旧在途视图和当次 draw/dispatch 的全部别名绑定一致，并验证设备实测带宽收益。
这仍是待验证方向，未按图像编号或游戏场景修改创建标志。记录：
`.local/logs/renderdoc-storage-usage-audit.json`。

### 2026-10-17：队列上传修正后的降温复测与 Adreno 编译器定位

Mali 同一 PID 13369、APK `f86dd064…`、正确采样精度缓存 `122643330366`，
原生 2772×1240、固定 3D 1.0、未开画面冻结/外推。自然冷却后恢复原进程，
Arm LWI/gatord/Streamline 2–18 秒窗口为 **22.3885 FPS**（21.1–23.5），
Perfetto 窗口 GPU 频率始终 848MHz。此前 12.1087 FPS 的窗口始终被限于
383MHz，不能将二者差异写成代码优化收益。Performance Advisor 指示 GPU 利用率
99.7%、片元受限占比 100%，外存读写约 510.9MB/帧；这些是原厂模型/计数器，
不是可相加的逐 draw 执行时间。Perfetto 30 秒里上传 CPU publication/GPU access
等待均为零，资源上限等待仅 66.664ms，真实客体 GPU fence 等待 8.615 秒。
剩余帧率不能仅通过继续隐藏 CPU 等待来解释或验收。
证据：`.local/logs/precise-queued-upload-cooled-original-game-official-summary.json`、
`.local/logs/precise-queued-upload-cooled-official-waits.csv`，原始捕获见其中路径。
采集后已通过当前进程原生命周期完成两次存档提交并挂起。

新 Adreno735（SM8635）的 LLDB19 + NDK lldb-server 可用；通过 abstract Unix
socket 与 adb forward 连接，不需要应用 INTERNET 权限。加载原厂 libsigchain、
libllvm-qgl、vulkan.adreno 等原始 ELF，完整信号栈确认在 `vk_dispatch` 创建计算
管线时进入高通 LLVM 后空指针崩溃，具体原 shader 为 `098a126f92bf533a`
（`cs_fluid_sim_accumulate_c.bin`）。相同 SPIR-V 可在独立管线创建程序复现，
无需加载整个游戏或额外显存。单独 SSA、标量化、常量简化均不修复；标准
`spirv-opt --inline-entry-points-exhaustive` 单独即可使该管线编译成功。
这是编译器输入规范化，未更改游戏 binary、浮点精度或资源绑定；是否解决完整
游戏必须另验。完整调试记录为 `.local/logs/qualcomm-new-lldb-compute-key-crash.txt`，
转换对照为 `.local/logs/qualcomm-new-compute-function-variants.json`。

原构建入口增加显式 `DMSH_VK_INLINE_FUNCTIONS=1`，纳入工具链指纹和每模块
compiler_options；默认关闭。使用标准 SPIRV-Tools 内联及原有 dead-code/constant
清理，保留 DontInline ballot helper。Adreno 的 subgroup64/ballot helper 与采样
全精度组合缓存 `25c59aa26fd4`，6931/6931 模块通过 Vulkan1.1 SPIR-V 校验。
该计算模块由 3 个函数/24 次调用变为 2 个函数/16 次调用，ballot 与 DontInline
helper 均保留；不使用按 shader key 的生产分支或跳过 dispatch。

6GB Adreno650 全套 ASTC overlay 的六个文件已部署并逐个校验 hash。默认
subgroup32 缓存仍触发原已知 `No UGPR spill support`，恢复已验证 subgroup64 /
ballot helper（采样全精度缓存 `6e86e4981d37`）后该管线通过。但真实游戏后续
启动 PID 26788 仍被 Android 明确记为 LOW_MEMORY，PSS 4.7GB。不能宣称 ASTC
已让 6GB 平板正常运行，也不能把这个低内存退出混作新 Adreno735 的 LLVM
编译器崩溃。详见 `.local/logs/tablet-original-astc-result.json`。

新高通在上述函数内联缓存 `25c59aa26fd4` 与原 BC 资源下，实际进入角色选择和
第一幕营地，输出/Surface 均 2750×1260、固定 3D 1.0、未冻结/外推。
Android 原有 1080×2358 显示覆盖已复位至物理 1260×2750 后再启动，不能把旧
较低 Surface 冒充原生测试。ASTC overlay 在该原生 BC 硬件上出现 ASTC4x4→BC3
动态纹理复制不可用，故使用原项目 touch overlay 与原 BC 内容，不添加游戏
专属格式转换分支。当前进程 PID14122已保存并挂起，完整角色/场景验收与
官方 FPS/GPU 指标另做；启动成功不能等同于 30FPS。证据与截图索引：
`.local/logs/qualcomm-new-inline-compatibility-result.json`。

### 2026-10-17：实际游戏 Snapdragon Profiler 计数器已取得

用户解锁后，Adreno735 原游戏 PID14122 在物理2750×1260前台运行。官方 SDP
2026.9 会话 `.local/profiles/qualcomm-new-original-game-stalls.sdp` 已保存；
从工具自身 sdp.db 用 SQLite 汇总最后20秒，每项392个原厂样本：Shaders Stalled
14.574%、Texture Fetch Stall13.652%、Stalled on System Memory0.384%、
Instruction Cache Miss0.100%。这些指标定义不同且可能重叠，不能相加或归为
独占耗时；系统GPU计数器也可能包含系统合成等工作，不是逐draw采样。
工具的 Metric.description 明确注明 GPU%Utilization 未能查询实际最大频率，
改用默认值归一化。因此本段35.297%的利用率与352.998MHz的平均频率，不能
证明GPU空闲，也不能直接与Mali GPU_ACTIVE百分比比较。此会话无正式游戏FPS
验收。工具原值、定义、时窗与会话SHA见
`.local/logs/qualcomm-new-original-game-sdp-counters.json`。

Mali 分组上传候选70111c92…的原厂20秒捕获、2–18秒分析窗口为21.6546875FPS，
整个Perfetto窗口485个GPU频率事件均848MHz。Performance Advisor：GPU99.7%、
fragment受限100%、外存约537.65MB/帧。CPU上传 publication/access 等待为零，
资源上限等待61.427ms/30秒，真实客体GPU fence等待8.592秒。未证明相比旧22.39FPS
窗口提速；该比较不是受控A/B，不能因单窗差异就判定局部机制有益或有害。
需要继续细化图像/计算依赖并检查实质带宽/flush成本，而非只隐藏CPU等待。
详见 `.local/logs/resource-stage-groups-original-game-official-summary.json`。


2026-10-17 图像依赖候选5ce50020：实际营地通过同步校验；原生2772×1240、固定
3D1.0、精确采样122643330366，官方LWI窗口21.919625FPS，GPU848MHz，仍未30。
PA外存496.155MB/帧，GPU99.7%、片元受限100%；与旧窗的带宽差异不是受控A/B。
Perfetto上传CPU发布等待一次0.139ms/30秒，资源上限44.140ms/30秒。完整报告
`.local/logs/image-dependencies-original-game-official-summary.json`。
新增按图像aspect/mip/layer的历史与纯图像操作依赖；普通绘制和guest-buffer
计算入口仍保守，不能宣称整个同步图已细化。详见资源依赖文档。


2026-10-17 bd4739c0通用同步候选：宿主553000/Mali553781项、真实营地同步验证
通过；原生2772×1240固定scale1。Arm官方窗口22.404FPS，GPU848MHz/99.7%、片元
模型100%，外存536.432MB/帧，物理通道54.019/帧、compute11/帧。未达到30FPS。
修正了clear通道开始/present来源/原亮度compute写入登记；含guest-buffer的
compute仍使用宽来源。上传等待0，实际guestfence墙钟9.602s/30s不是纯GPU时间。
完整记录 `.local/logs/graphics-dependencies-v4-original-game-official-summary.json`。
650在首轮f226测试中WiFiADB断开，结果未验收；不能据连接失败断言游戏或GPU崩溃。

## 2026-10-19：管线预热、热负载控制与野外卡顿

Vulkan 现在保留最多 512 条成功创建过的精确管线配方，并在下次启动、首次
NVN 提交之前由原录制线程预建。配方包含 Shader 内容身份、特化值、顶点输入、
混合/深度状态、动态 Uniform 模式及兼容 RenderPass 状态，不保存进程指针。
原驱动 Pipeline Cache 继续使用；损坏、过期或不支持的配方回退原创建路径。
实际 Mali 启动已完成 `loaded=246 warmed=246 rejected=0`。首次到达尚未记录的
区域仍可能创建新管线，随后由现有缓存后台线程持久化供下次启动使用。
实现与冷/热/失效缓存检查见 `.local/logs/pso-prewarm-v1-implementation-20261019.json`。

后续同一路段实机检查：首次记录 664 个 Present 间隔，最长 132.015ms，
与一次 113.662ms 的新管线创建重叠。该轮新增配方持久化后，下一次启动成功
预建 269 条、拒绝 0 条；复走路线的 12 秒官方 Perfetto 记录中，游玩阶段管线
创建为 0，352 个 Present 间隔最长 66.097ms，仍有两次超过 50ms。
两轮的温控目标、3D 档位和 CPU 频率不同，不能用这些间隔算受控提速比例。
结果验证了新增配方在下次启动预建；剩余长帧继续按实际 CPU 执行与依赖定位。
两轮均执行真实原游戏、完成两次原生命周期存档提交并停止；证据为
`.local/logs/prewarm-thermal-current-play-verification-v1.json`。

浮屏只显示实际 Present 帧率和电池温度；电池温度来自系统广播，不代表 CPU
或 GPU 温度。热负载策略只使用进程内公开 PowerManager API 的事件通知，不用
ADB 温度报告、sysfs 或自建轮询采样。LIGHT/MODERATE 时最多 30 FPS、原 3D
档位不高于 0.65；SEVERE 时最多 20 FPS、3D 不高于 0.60，原生 UI 分辨率保持。
恢复有 30/60 秒迟滞；连续 SEVERE 45 秒或 CRITICAL 时走原 Android 后台存档/
暂停流程。真实游戏已观察到 0.75/60 → 0.65/30 → 0.60/20，并完成新存档后停止。
这些是主动削减发热负载，尚未证明可无限持续游玩或完全避免厂商热保护。
证据见 `.local/logs/android-thermal-drs-v1-verification.json`。

已有野外 6 秒官方 Perfetto 记录中，177 个完整翻译区间平均实际 CPU 20.083ms；
最慢区间墙钟 46.792ms、实际 CPU 45.902ms，重叠的 PSO 创建约 0.437ms。
窗口内 8 次 PSO 创建均小于 0.9ms，说明这个窗口剩余的最大尖峰应继续沿 CPU
执行路径定位。178 次原 NVN SyncWait 中 171 次已完成，7 次阻塞合计 26.945ms；
仅观察到一个活动队列，不能据此实现假设性的跨队列放宽同步。
原始记录 `.local/profiles/nvn-sync-semantics-v1-mali-field.pftrace`，分析
`.local/logs/nvn-sync-semantics-v1-field-cpu-frames.txt` 与
`.local/logs/nvn-sync-semantics-v1-field-readonly.json`。这个短窗口不代表所有场景。

该设备的 vendor atrace 自由文本和错误 TGID counter 会干扰官方 Trace Processor。
必要时使用现有 `capture-android-scheduler.py --clean-atrace-print --buffer-mb 128`，
通过官方 Perfetto `print_filter` 保留完整 B/E 与异步配对，仅保留当前进程的 C
counter。过滤后上述野外记录解析错误为零、调度/频率覆盖完整；被过滤的 vendor
C counter 不能再用于结论。`traced_chunks_discarded` 是服务累计值，不等同本次
丢包，须结合本次 buffer/ftrace overrun、overwrite、packet loss 等统计判断。

## 2026-10-19：跨提交纹理绑定配方与默认泛光

纹理绑定的不可变图像/视图配方可在完成并回收的提交上下文中保留。每次使用仍
检查描述符代号；逻辑图像销毁和真实物理升级都会推进独立代号，先拒绝失效
指针，再比较物理句柄，防止句柄数值重用误命中。实际消费提前更新当前提交的
`last_use`。可能结束通道的回调使用本地纹理快照；返回后代号或提交改变就放弃
旧物理配方，重新解析。上传、布局、hazard 和真正完成后的回收均照常执行。
没有待处理 mip 清屏且没有 NVN 屏障时，读取回调按现有源码必为空操作，跳过
该回调及其保护快照；有待处理工作时保留完整路径。

真实原游戏的两份营地记录均为 3D 0.65、温控目标 30 帧，并各包含 1,353,550
次绘制。慢路径纹理解析由 1,770,404 降为 1,653,500，约减少 6.6%。这属于工作量
收益：CPU 频率与采集窗口不同，官方 Perfetto 的整段 CPU 时间未显示受控下降，
不声称整帧提速。主机异步/校验检查 21,751,986 项、Mali 检查 19,396,499 项通过，
包含跨提交 GPU 内容更新、两次物理升级、描述符重登记及实际退休。
初次 Mali 测试误装了 identity 纹理读取模型，NaN 位模式检查失败；已保留失败
记录，按本机既有 `rgba32f_quiet_nan_v1` 模型重新生成，断言没有放宽。

已有非 lazy 泛光实现恢复上游默认开启，后端、Android Activity 与启动脚本一致；
`--no-bloom-quarter` 保留关闭对照。完整链证明、未知链回退、查询/别名检查和 C
物化均未改。依据是既有真实画面/同步验收和原厂计数器的小幅工作量收益，
不因暂未稳定涨帧继续关闭。只读复核见
`.local/logs/bloom-default-readonly-reassessment.json`。

高通的独立整数夹具在同 PID、空应用 PSO 缓存、普通描述符写入下，旧循环和
合法索引双读取表示均完整通过，原分支索引表示仍失败。资源上传/模板更新
不是该故障的普遍原因；差异缩小到 Shader 表示或其编译上下文。此自有夹具
不能解释为真实游戏变慢的根因，生产 Shader 尚未据此改变。证据为
`.local/logs/q735-driver-controls-v3-independent-result.json`。

## 2026-10-19：五分钟热机与 CPU 常量缓存对照

原生游戏 v5 在营地连续行走 301.74 秒、107 段路线，没有退出；结束后原生命周期存档两次并停止。末段温控目标为20帧，不能作为持续30帧或解决过热的验收。已有官方Perfetto末段记录295个连续出图间隔，均值49.854ms、最长107.225ms；该长帧重叠的命令回放耗时100.758ms，其中实际调度运行92.420ms，没有PSO创建。末段回放均值实际CPU36.187ms、加权1317.93MHz。各阶段与队列边界不是纯GPU执行时间。

本次结束后才提取的logcat热状态已被环形日志覆盖，不能从20帧滞后保持反推持续SEVERE。后续仅流式保存应用已有公有API通知，不运行ADB温度轮询。温控deadline审查另发现低等级区间切换会重置恢复期限，修正及测试见 `ANDROID_THERMAL_DEADLINES_2026-10-19.zh-CN.md`；该bug不能替代对CPU长帧的分析。

稀疏常量键的新原生库先在旧无.dkey缓存上采集完整键reference：固定3D0.65、温控30，实际原游戏PID22409，启动预热288/288/0。官方6秒Perfetto回放实际CPU均值28.517ms、加权1476.67MHz；174个连续出图间隔均值33.476ms、最长49.593ms，没有PSO创建。采集后原存档两次并停止。这是新候选的对照，不是已证明提速。新schema必须通过完整canonical重建、实际模块证明重算及全量哈希部署，再单列候选结果。
