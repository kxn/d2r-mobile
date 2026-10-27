# 上游剩余差距：GPU 查询换算与驱动隔离

状态：**调查继续，任务未完成。** 前一轮把“主要差距已缩小”当作收尾过早；用户要求继续解释并解决共享版本仍落后上游的原因。本文不把接近上游等同于完成，也不将工具捕获时的帧率作为普通运行成绩。

## 当前差多少

最新已有的原厂 Snapdragon Profiler 同设置计数为：本实现每完整上屏间隔写入 44.788 / 44.730 MB，上游 45.693 MB；平均少约 0.934 MB（2.04%）。两端游戏输出均 1968×900、3D 0.85（主要 3D 为 1673×765）。这是全局 GPU 计数，含系统合成，**不包含 CPU memcpy，也不是纯游戏 DRAM 带宽**。原始窗口和实际压缩证明见 [同设置写入报告](ANDROID_MATCHED_WRITE_AND_SHARED_CPU_2026-10-27.zh-CN.md)。本轮未重新采集物理写入，不能将下述查询优化称为新的写入减少。

本轮使用同一台 Q735、相同存档位置/游戏尺寸/比例，官方 Perfetto 实际 Surface 间隔得到：

| 普通运行对照 | FPS | 最忙线程 Running ms/间隔 | 全应用 Running ms/间隔 |
|---|---:|---:|---:|
| 旧版 A1 | 42.465 | 19.087 | 36.836 |
| 旧版 A2 | 42.409 | 19.050 | 36.831 |
| 旧版后继 A3 | 42.535 | 18.946 | 36.293 |
| 查询换算候选 B1 | 42.652 | 18.958 | 36.628 |
| 查询换算候选 B2 | 42.725 | 18.652 | 35.548 |
| 原 release native 库的上游对照 | 43.134 | 18.982 | 39.687 |

候选约有零点几帧的改善，仍低于这次上游约 0.4～0.5 FPS。表格含时序波动，不以两次最高值宣称确定的整帧百分比。上游使用独立可调试 Java 壳，native ELF 与官方 release 字节一致；这不是重新编译过的上游后端。

两端 driver、纹理编码、WSI 和 HUD 尚非单因素相同。当前整应用 CPU 工作较少，不能全部归功于共享；最忙线程接近，也不支持将所有工作量节省线性映射到 FPS。已有共享 343 MiB Pool 的旧扫描/比较/上传分支不再执行；共享后的图像语义、依赖与 Vulkan 驱动成本仍存在。

## 已完成的排除实验

1. **输出尺寸对齐**：保持游戏尺寸和比例、压缩开启，仅把本应用 Surface 从 2358×1080 改成 1968×900。两次 42.445 / 42.405 FPS，与关闭组基本相当。这不是剩余差距的解释，不改系统尺寸。
2. **Store 规划成本**：保留全部 Store、跳过相应消除规划得到 42.479 FPS。官方 PMU 指令约 68.2 M/间隔，对照约 71.7～73.0 M；确有 CPU 工作减少，但未显著改变 FPS。不把这段预算全部当成免费收益或默认撤销已有优化。
3. **材质常量特化**：使用已有安全开关退回基础 shader，关闭运行时特化；资源、游戏输出与原始 shader 链不变。结果约 40.887 FPS、最忙线程 19.748 ms，未解释剩余差距，正常特化继续开启。没有通过冻结某个场景的材质值进行比较。
4. **驱动 UBWC coherency workaround**：源码明确 FD735 属于 `a7xx_gen2`；`ubwc_coherency_quirk=true` 位于 `a7xx_gen3`。因此不能把 A750 的该额外 CACHE_CLEAN 路径归给本机，也没有删除硬件勘误。
5. **写回 barrier**：本实现只在 commit 时集中 flush 原生共享写回；驱动中的相邻 barrier 先累计 pending flush。未发现每条查询都立即要求 CPU 回读的依据，未为减少 API 数而另加无用缓存。

## 真正实施：精确查询换算

现成 Mesa u_trace 显示，49-Pass 主命令缓冲每周期有 4 次本实现的查询结果换算 compute。原 shader 对 96 位乘积逐位除以 625，再移位和饱和，保留 NVN 两个 64 位字的精确 ABI。这是共享路径为避免 CPU 等待而执行的 GPU 工作，不能简单删掉或伪造查询值。

`ef3e1f6` 改用基数 65536 的长除法。常见输入由 96 次逐位迭代降为 6 个分段；保留全部乘积位、浮点周期的整数精确表示、时间戳有效位数、移位、超过 uint64 时的饱和。输出地址、16 字节写入、调用数、依赖和提交顺序不变；不要求 shader Int64/Float64。

| 现成 Mesa GPU trace | 每次查询换算 ms | 四次合计 ms/主 CB |
|---|---:|---:|
| 原逐位算法 | 0.037056 | 0.148224 |
| 新分段算法 | 0.006089 | 0.024357 |

在同类工具窗口中，该局部区间减少约 0.1239 ms（83.6%）。每组取 300 个完整 49-Pass 主 CB，四次查询各 1200 个样本。主要其他 compute 的 hash/尺寸及耗时接近；不能把主 CB 全部变化都归给本修改。

### 测量边界

使用的是当前驱动已经编译的 Mesa `u_trace` CSV 和 IR3 汇编输出，Android 标准 debug `wrap.sh` 只负责设置官方工具选项；没有新增运行时计时器、采样器或查询结果替代器。诊断 APK 的六个 native 库与对应正常 APK 字节相同，正常 APK 不含 wrapper。

每 draw 时间戳默认关闭。先排除启动探针、选择有完整 49-Pass 元数据的主 CB，再取末尾 316 个中的前 300 个；CSV 的 frame 字段恒为 0，实际使用 batch ID。GPU 时间轴没有伪造到 CPU/Surface 边界。嵌套的 Render Pass、draw IB、clear 等时间**不能相加**；主 CB 之间的区间还含 present 工作，不称为纯 GPU 空闲。

工具开销真实存在：开启 pass trace 时约 38.46 / 38.71 FPS，正常运行约 42.4～42.7。原主 CB 间隔外部区间平均约 0.369 / 0.339 ms，但最大仍有 4.30 / 4.67 ms；不能说没有任何长间隙，也不能用工具窗口直接推正常 GPU 帧预算。

IR3 原生汇编已取得。大型 FS 的统计中可见约 2900 条指令、较多纹理/访存指令、全精度寄存器和 8～10 个最大 resident waves。但这些静态统计尚未与具体热 draw 的实际 FS 关联，不能仅以 shader 大小认定热点，或将静态 stall 注释当作硬件实测 stall。历史 protected-color-half 曾有红/黑地面错误，本次未重启这一错误候选。

### 正确性与部署身份

原有实际 GPU 查询数学/发布夹具在同步、异步模式四组均通过：8398、8398、6304、7868 项，核心/同步验证未报错；数学输入覆盖实际高通周期、Mali 周期、负移位、极小/极大有限周期、溢出、无效能力拒绝及目标周围哨兵。原 report 夹具还验证真实 GPU 时间戳和 query prefix/reset/streaming；没有改成只核对实现本身的空断言。另有固定种子的十万输入整数模型核对，不代替 GPU 夹具。

正常候选 APK：`c0c3a64d5fee689d402b75c976958ff1590d2cfa6ce896144d50c1abc85c5733`；host ELF：`b28036958aca089db2942f9d61fbc0a9eb7dfbc2a3114d8002df3b7e57b5cfbf`；源码 `ef3e1f6`。原游戏 CPU 模块、加工规则及产物、shader 链 `29e56b51bce5`、native 资源表和 B3 driver 均未改。真机候选完成营地、截图检查、原 Android 生命周期 fresh save2 和停止，不冒充全场景验收。普通发布身份尚未切换到该候选，后继旧版 A3 已装回原发布 APK。

## 当前继续追查

单独隔离系统 Qualcomm 与 Turnip：同一份上游源代码构建一个仅改变 Vulkan 装载入口的诊断版，两臂都使用相同 dispatch，保留同一游戏模块/资源/shader/存档，不把原 release 与重编版本交叉当作单因素。先资格确认系统驱动臂，再切换同哈希 B3 Turnip、prefer_sysmem 与应用内 UBWC。诊断版已完成四轮交错普通运行（S/T/S/T）：

| 同 APK 的 driver | 第一次 FPS | 第二次 FPS | 最忙线程 Running ms/帧 |
|---|---:|---:|---:|
| 系统 Qualcomm | 43.285 | 43.358 | 18.915 / 18.903 |
| B3 Turnip、prefer_sysmem、UBWC | 39.984 | 40.112 | 19.373 / 19.452 |

系统驱动臂与原 release 的 43.134 接近；Turnip 臂平均少约 3.27 FPS（7.55%）。两个驱动臂均不使用共享，且使用同一重编 host ELF `13565f6df8c9e3054268cda5b03b2c19d47a3bdc4798d936111b20fa8104d364`、同 APK `f8ddb563d279c8590db157ecf0ecfe35c992d3a865b3d0aab434db63eb153c2a`。实际 driver 日志、映射、文件 SHA 和两组截图已核实。不能将该重编版称为原 release 字节相同。

这一结果证明跨 driver 比较存在比当前残差更大的成本差异。我们的共享版本在同 B3 上约 42.7，高于这份上游 Turnip 对照；但不能把全部差值单独归给共享，因为两套兼容层/纹理编码仍不同。两臂 CPU 频率也不同（系统约 1.68～1.72 GHz，Turnip 约 1.30 GHz）；官方 PMU 下系统约 51.0 M、Turnip约 48.0 M 用户态指令/上屏间隔，不能把 Turnip 降帧说成执行更多 CPU 指令。当前 Perfetto 没有产生 GPU frequency counter track，不伪造等 GPU 频率证明；接着使用原厂 GPU 计数器核对 GPU 频率、停顿和工作量。

诊断改动仅为统一使用 Volk 分派、在创建 instance/device 后加载分派表、通过冷启动标记选择系统或 adrenotools/B3，以及为 hooks 采用标准 extracted native-library 打包。生成表沿上游 shell-stubs 工具链；亮度表从原始 RomFS 生成。没有改写游戏 CPU binary、shader 或资源，也没有给原 release ELF 叠补丁。诊断源码 patch、完整源码/生成输入指纹、APK/native 库指纹和原始测量均单独归档。首个启动探测在安装尚未完成时被 APK 身份断言拒绝，未启动游戏，失败记录保留。

当前最重要的未知是：共享省去的 CPU 工作，是否被另一条 driver 的 GPU 编译/执行成本盖住。两端早先已经确认的假字节依赖、渲染模式和输出压缩根因不重复包装为本轮新发现。剩余约 1% 差距、其他场景、长期温度与 60 FPS 仍不算解决。

原始记录：`.local/logs/remaining-upstream-gap-v1/`，包括各 arm 的 argv/receipt、原 Perfetto/SQL/CSV、Simpleperf 原记录及累积硬件计数边界、两组 Mesa 原 CSV/摘要、IR3 原汇编、GPU 夹具和构建清单。采集后均存档并停止本次游戏；Mali 与 6GB 平板本轮未使用。

再次读取上游 main 至 `d93985b`：新增 2.0.1、740–799 改写默认策略、编译失败处理、BC4/5 过滤格式回退、30 FPS 省电设置和 UI 修复。本机 735 不在新 740–799 默认策略范围；实测 40～43 FPS 亦不是 30 FPS 上限。当前固定 release/诊断对照仍明确标为 `b811b90`，不把旧产物冒充刚更新 main。

## 原厂 GPU 计数将范围缩到 driver 与渲染策略组合

同一诊断 APK 再以官方 Snapdragon Profiler 2026.9 采集九项指标，工具原生全局样本通过 Perfetto clock snapshot 对齐到每端首末完整游戏 Surface 间隔：

| 指标 | 系统 Qualcomm | 同 B3 Turnip |
|---|---:|---:|
| FPS | 43.244 | 39.874 |
| GPU clock-count rate，M clocks/s | 498.869 | 499.434 |
| 读取 MB/间隔 | 106.416 | 106.615 |
| 写入 MB/间隔 | 45.834 | 45.327 |
| 片元指令 G/间隔 | 2.396 | 2.115 |
| Shaders Busy % | 80.144 | 82.667 |
| Shaders Stalled % | 16.937 | 23.010 |
| Stalled on System Memory % | 0.362 | 0.414 |
| Texture Fetch Stall % | 12.192 | 11.882 |
| Wave Context Occupancy % | 57.946 | 54.821 |

两边 GPU clock-count rate 接近（不是瞬时频率锁定证明），读取接近，Turnip 写入与归一化片元指令反而更少，却慢约 3.37 FPS。这是不同 driver 配置组合的执行差异实证，不能继续以更多总写入解释，也不能直接定性为 DRAM 带宽饱和。Shader 停顿更高、波占用较低，优先查编译结果、依赖延迟隐藏及其他 GPU 命令成本；这些指标不可相加，尚未证明具体哪条 shader 或调度机制应对全部差距负责。

原始 .sdp、只读解包数据库、指标定义与样本、两份原 Perfetto 和同窗口积分在 `.local/logs/remaining-upstream-gap-v1/gpu-tools/`。工具通过现成 GUI 选择指标，未手改数据库或重写采样/计时。复用已有整理程序，将验证所需指标列表切换为本次实际九项，保留定义核对、global 范围、非有限/负值/百分比哨兵、覆盖与间隙检查。没有图形抓帧层插入。

## 实际管线与一次波大小反证

上游现成 `gfx-pipe-stats` / `gfx-no-warm` 经实际生效确认，使用标准 `VK_KHR_pipeline_executable_properties` 取得两端编译统计，以及帧 3000–3059 的实际 PSO 绘制使用。系统 365 条 executable 统计、Turnip 541 条，各 80 条绘制使用记录。两个主要 HDR FS（原 shader key `5c1ca4e7c3e4657c`、`38f344adf801b153`）在两端实际都为 subgroup64；Turnip 分别为 19 full registers / max10 waves。对照系统 driver 的 full-register footprint 为 19 / 22，各厂统计口径不同，不能推导 Turnip 必然占用更多寄存器。

约 706 次/帧的 UI PSO `ce3ad354c986bf85` 是另一个明确差异：系统 subgroup64，Turnip subgroup128。用现成 `IR3_SHADER_DEBUG=thread64` 做冷启动单变量测试，源码确认该位同时进入 Vulkan pipeline 与 Mesa disk shader cache key；强制 single/double 的必要硬件约束仍优先。后继原 pipeline 统计实证 UI 变为 subgroup64、主 HDR 保持64；没有只看设置值便宣布生效。

该候选官方窗口 38.936 FPS，对照 Turnip 39.874；频率指标约499.87 / 499.43 M clocks/s，写入均约45.3 MB/帧，片元指令约2.115G/帧相近。Wave occupancy 54.82→58.24%、texture fetch stall 11.88→7.05%，但 shader stalled 23.01→22.94%基本不变，净帧率反而下降。较小波需要更多波执行/preamble，是明确新增成本；不能用单一更好看的 occupancy 代表总收益。实验保留，未启用产品默认；原 comparison env 字节及 SHA 已恢复。它没有解决驱动差异。

补齐了先前静态汇编与实际 Shader 的关联：官方 GFXReconstruct 既有营地记录的主要 HDR module318 原始 SPIR-V SHA `e10275407159e69ae08a811dd28bb00b8a69b80c8706304e724ad27c59b4c175`，按 Mesa 原 `spirv_to_nir` 的 source BLAKE3 规则核对，与本实现真实 IR3 日志中六个编译块完全匹配。对应约1967～2133条静态指令、19 full registers、10 max waves；之前日志里最大的约2900条 FS 不能因此被指认为这个热模块。这里只建立源模块关联，尚未把各特化 native variant 的逐 draw 执行成本全部关联起来；没有用 static stall 估计替代硬件测量。

管线统计首次尝试的零长度 marker 权限不足，日志明确显示未开启，失败保留、不算成功。后继只给本次两个自建 marker 设置可读权限，启动12秒先验证真实状态，再进入采集；结束删除两文件。没有修改原游戏、shader 或数据资源。

## 剩余 HLE 交接成本：已发现原有短等待策略遗漏

重新按调度状态分析最忙线程，必须将睡眠段与其后相邻 Runnable 段的 `waker_utid` 关联；waker 并不写在睡眠段本身。现成旧轨迹已经能回答这一点，前一次仅查询睡眠行得到 NULL 是查询方法错误，不能归咎工具缺失。新增一次 wake/syscall 轨迹保留；实际记录了 waking/wakeup，四个 syscall 事件不可用，记录 notice=4，不将其称为完整 futex 调用栈。

在原窗口逐段关联后，本实现 WSI-A3 每上屏间隔由两个 guest worker 唤醒的睡眠共 **0.547+0.528=1.075 ms**，约 **26.76 次/帧**；原 release 上游共 **0.205+0.184=0.389 ms**，约 **10.15 次/帧**。本实现另有约2.139 ms由kgsl-events唤醒，上游约2.577 ms；`dm-gpu-complete`只对应约0.00025 ms，不能再说收尾线程大锁就是当前全部等待的解释。候选 RADIX-B2 的两个 guest worker 睡眠仍约1.068 ms，新增 wake 窗口约1.064 ms，方向重复。

对应源码差异已找到：原上游 `b811b90` 的 HLE 在主线程登记等待后释放 G，最多短暂轮询30微秒，再回到原条件等待；醒来时只有仍在polling阶段才省去条件信号。Android启动默认设置30微秒。本实现旧路径没有这部分，每次直接条件等待。正在补齐相同 HLE 语义的有界等待处理，保留原优先级/FIFO、超时、取消、锁交接和真正睡眠回退；预算冷启动选择、不反复读环境变量、不新增性能计时/计数。截止目前仅功能测试通过，尚未报告真机净收益，默认仍0。

查询算法候选 `ef3e1f6` 已单独完成两次正式图标冷启动、fresh save2/停止及匹配的 schema3 profile/receipt 发布。原 driver、native表、CPU游戏模块、shader链和UI均未改。后继短等待候选是另一次实验，不把其未完成状态算到已发布包里。


## 主线程短等待：同包 ABBA 完成

原生实现 `7ea3d51` 的同一个普通 APK，按 **0 → 30 → 30 → 0 微秒**冷启动交错；每臂仅改变主线程等待预算。原游戏 CPU 模块、Shader、driver、native 图像表、保存位置、1968×900 游戏输出、0.85 比例、内联翻译/CPU7和提交策略完全相同。每轮仍由现成 Perfetto 取20秒，使用完整 Surface 间隔归一化。前后 PSO242、variants32、pending0及图像缓存签名相同；原游戏仍运行推进，没有冻结世界。

| 指标 | 0 微秒，两臂均值 | 30 微秒，两臂均值 |
|---|---:|---:|
| 两次 FPS | 42.723 / 42.747 | 43.349 / 43.320 |
| 平均 FPS | 42.735 | 43.334 |
| guest worker 唤醒前睡眠 ms/帧 | 1.0275 | 0.3185 |
| 对应睡眠次数/帧 | 26.43 | 8.46 |
| 主线程 Running 分段数/帧 | 36.42 | 14.85 |
| 主线程 Running ms/帧 | 18.8988 | 18.7755 |
| 全应用 Running ms/帧 | 35.3815 | 35.3070 |
| kgsl-events 唤醒前睡眠 ms/帧 | 2.1173 | 2.9516 |
| 主线程 Running 时间加权 MHz | 1609.94 | 1670.23 |

本窗口帧率约 **+1.40%**。最强实证是短等待阶段少了真实条件睡眠和调度交接：worker-woken sleep减少约0.709 ms/帧，次数减少约68%，主线程 Running 分段减少约59%。不能把0.709ms直接当成整帧收益：主线程更早推进后，等待 GPU 的时间反而增加约0.834ms；这正说明消除一种等待后会显露另一种限制。CPU频率并非完全相同，也不能凭Running小降宣布指令量显著减少或温度改善。全应用CPU时间基本持平，未见轮询造成显著总Running上涨，但长期热态仍未验收。

这个机制恢复的是上游已有的 HLE 主线程交接策略，不是 GPU 完成轮询或伪造查询。预算只冷启动设置、运行时默认0；符合本机资格的桌面配置才选择30。释放/重新取得原 G 后保留原优先级和FIFO、取消、超时、条件变量回退；生产路径不增加测量计时器。功能夹具覆盖poll开始与结束交接、真实pthread唤醒、直接条件等待、超时与取消；原条件变量正/负夹具、28项启动脚本测试、GCC ThreadSanitizer、Android ARM64原生夹具均通过。Clang TSAN因本机缺运行库未执行成功，不能并算一次通过。测试程序只是功能证据，真正游戏营地结果以上述ABBA为准。

实测APK `bd8676e62c3c84e8e8f4210f2bc63ba7015abc94f50652423ea0e632505288a7`，host ELF `f9369d0304e97a3da3c0a2a15fafd5cd0f39bb1b9464c69117b4860fe5dbab24`。后继 `c19d39f` 仅增加冷启动schema4的Java解析、文档与测试；正式打包APK `52965c563846c58c4090d96e0e7a84f1399f633ef0107f04ffe31c5bafceef5c` 的全部6个native库和assets与实测APK逐字节一致，因此复用这一轮性能证据，不重复跑一轮FPS。schema4必填 `guest_wait_budget_us`，精确字段集与原身份校验保留；规范十进制0..100及旧schema隔离测试通过。

原始证据：`.local/logs/remaining-upstream-gap-v1/short-wait-final-summary.json`、四份 `fast-controls/WAIT*-receipt.json`、对应原Perfetto/SQL/唤醒整理；保留旧采集，未覆盖为新成绩。HLE修改不改变GPU图像工作，本轮没有为此重新采集物理GPU写量；最新原厂匹配窗口仍是本实现44.759 MB/帧、上游45.693 MB/帧，不能把它们标成这个新包的新测量。

## 加快实验：删除任意暖机等待，保留数据资格

并行审计了13轮旧实验和实际状态输出。旧流程在PSO等已稳定后仍固定等tick2500，启动后准备约92.7～94.9秒；每0.4秒读取一次约1秒更新的完整状态，也常重复读取同一条记录。

新编排按真实状态准入：至少8条独立观察、至少150tick前进，PSO数、uniform variants、pending0、图像allocation/rebuildable/released签名不变；variants至少32。每1秒读取，采前后还检查同一PID、同一场景/位置/分辨率/比例及上述签名。实际第一臂启动时variants29，不能立即采；到最终签名稳定后准入，准备约28.6秒，**单臂较旧流程少约65秒**。正式20秒窗口、真实存档和退出、APK和部署锚点校验保留。早准入减少了预热发热，故收益只用新准入的同包ABBA，不能与旧长预热帧率直接相减。

同一APK的冷参数对照不再重复构建、安装或资源加工；不变的资源复用已有完整验证与本次部署锚点核对，变更部分重新核验。无需对每臂附带另一套PMU/调度采集；只有判断确有缺口时追加。截图、日志拉取放在测量窗外。8秒初筛可以淘汰坏候选，不能冒充正式性能结果。脚本只编排现成Perfetto与原生命周期，没有重写工具采样器或计时器。20项离线准入/失败保护检查通过。

并行GPU审查也避免了一次无益的全链重建：热HDR最终SSBO加载已经向量化；偏移溢出保护已落为单条IR3饱和加法，删保护仍需单条加法，不能声称每次加载少两条硬件指令。140次/帧PushConstants约16.9KiB只是API参数，不能乘每像素伪造成DRAM流量。未以这些表面差异占用设备窗口。

下一步仍查同Shader在系统/Turnip的native编译和执行差异。两者同APK约43.3/40.0 FPS的驱动差异已足够重复证据，无须再从头证明；当前自己的43.33不再表明共享版必然比上游43.3慢，但这不代表驱动根因、其他场景或60FPS已完成。


### 本次桌面发布完成

schema4候选已在Q735上完成两次**不带Intent额外参数**的真实图标冷启动。两次原HLE日志均证明 `primary_budget_us=30`，共享池/lazy窗口、native资源、CPU7内联翻译、应用内UBWC和prefer_sysmem保持原配置；现成原Android生命周期均fresh save2后退出。截图目视未见新黑块或几何异常，不代表全游戏验收。

桌面保持用户原设置2358×1080游戏输出、0.75比例，不把1968×900/0.85对比窗口冒充日常模式；截图约40.7的瞬时HUD也不替代正式ABBA的43.33。已安装APK为 `52965c563846c58c4090d96e0e7a84f1399f633ef0107f04ffe31c5bafceef5c`。profile SHA为 `73727b0ba8d576f96308ddd62309e7e882c6be39fcff1c468e98318403b7f850`，receipt SHA为 `6e55c3908dfb709d89e5743c3772542bf83c01033251e8ac24c03b03ea4bf94f`。当前接受状态保存于 `.local/logs/remaining-upstream-gap-v1/accepted-short-wait-deployment.json`，上个query部署记录保留为历史。原模块/Shader/native表/driver等锚点再次确认，系统显示、调试配置和Mesa临时属性已恢复；两个游戏进程均停止。


## 并行复核后的资格纠正与模式实验

还发现一个重要的对照边界：原上游诊断loader给B3硬编码 `TU_AUTOTUNE_ALGO=prefer_sysmem`，系统driver用自身自动选择。因此43.3/40.0证明的是“系统自动策略”与“B3+prefer_sysmem+UBWC”整个组合的差距，**尚未单独隔离shader编译器本体**。相近流量和时钟指标不能跳过模式这个条件。旧项目在不同资源/.60下的模式记录不能替代同一上游APK实验。

无需重编，用上游现成env入口设置Mesa标准 `TU_DEBUG=gmem,startup`。源码证明其分支在autotune之前，保留不可容纳附件、empty/tess、XFB/query等硬性SYSMEM回退；不是删barrier或改guest/shader。正常不插工具的20秒Perfetto窗口：GMEM臂 **25.486 FPS**；恢复原env后的新鲜prefer_sysmem参考 **39.989 FPS**。这排除了“直接把关键Pass推入GMEM即可补齐差距”的方案，不能据此断言所有自动策略都差。

实际模式另外用Mesa现成CSV确认，**不再重复正式FPS采集**：两次冷启公用cmd_buffer/render_pass/IB tracepoints，不开逐draw时间戳；每臂取最后60个完整主要HDR batch，排除尾4个。1673×765的两个大Pass，参考臂各640draw、`tiledRender=false` / Autotune selected sysmem；GMEM臂各641draw、`tiledRender=true` / TU_DEBUG(GMEM)。游戏仍正常推进，场景微小draw变化明确保留，不称逐帧相同。这里证明关键Pass真正换模式；没有拿每draw边界时间当纯shader成本。工具窗口FPS不用于上述普通结果。

自己已发布c19d39f包则直接用原cold接口选 `bandwidth`，无需装新包，其他条件与WAIT30相同。8秒初筛约 **35.348 FPS**，低于已正式测量的prefer_sysmem约43.33；只能作为不改默认的筛选结果，不报告正式百分比或将其冒充同上游APK对照。不能说已经测试了上游B3的默认bandwidth——该上游loader硬编码仍需源码参数化才能做。

本次没有新写采样器、计时器或改驱动。官方Snapdragon SnapshotPlayer的 `-updatabledriver` 指GDD包，没有已验证入口加载应用内B3；现成capture里SCOPE动态表为空，静态ShaderStat存在，故不能把系统driver重放当Turnip数据。相应手册/二进制能力核实与工具缺口保留在 `gpu-next-after-wait/TOOLS.zh-CN.md`，未因工具缺口退回自制GPU计时。优先沿现成Mesa实际mode和原厂全局计数推进。

模式原CSV、源字段核对、APK/driver/env哈希与CAS恢复在 `gpu-next-after-wait/UPSTREAM-MODE1/`；正式与初筛摘要为 `mode-control-final-summary.json`。采后原env和marker状态已恢复，上游原release carrier APK b8d1fcd1已重新装回，移除本次启用selector；主线schema4已发布组合保留，两个游戏均停止，临时Mesa属性为空。未接触Mali或6GB平板。

准备开销的进一步离线审计修正了范围：四臂暖机19.553～45.877秒，共123.934秒，28.6只是第一臂；真实启动约21.18～23.25秒/臂。没有每轮重新扫描52240个shader和36模块，显式Intent也不会重复六项桌面Java检查，完整验证实际已复用。剩余每臂采前第二次APK哈希确有重复，四次合计读88.46MB，但没有命令耗时证据，不能声称又可省几十秒。原save2的应用内工作约0.282～0.301秒，不含ADB/helper等待；不为省这点开销破坏保存。审计没有找到第二个与旧固定94秒暖机相当的浪费点，不再为小哈希开销造管理缓存。


### 模式记录引出的 LRZ 方向已核实，不能直接删 stencil

并行审查了现成双方真实GFXReconstruct整帧命令、PSO状态和Mesa条件。HDR双方均 depthTest=true、depthWrite=false、depthCompare=EQUAL；stencilTest=true，正反面 EQUAL / compareMask2 / reference0 / writeMask0，三个操作全KEEP。前置Pass确实向同一D24S8写入stencil bit1（ALWAYS/REPLACE、reference2/writeMask2），中间depth prepass只清DEPTH并LOAD/STORE保留stencil，HDR随后读取该bit筛选。没有中途stencil clear或动态状态覆盖。全KEEP只说明只读，不说明恒通过，关闭测试会改变内容；这里只证明真实生产消费依赖，不声称已经测量杀掉多少像素。

Mesa CSV的 `lrz=true` 是 `state.lrz.valid`，不等于该draw硬件测试开启。`Stencil may kill fragments` 在这里单独禁止LRZ写入，不能直接等同整体LRZ关闭；HDR的depthCompare=EQUAL另外触发独立的temporary disable（源码注释系统blob也如此）。因此不能凭lrzStatus=DISABLED就新加跳过stencil或修改游戏深度语义的优化。核对脚本、原输入SHA、实际producer/LOAD/STORE/clear和源代码片段保存在 `gpu-next-after-wait/lrz-audit/`；没有为此重新抓整帧或占用设备。
