# 顶点着色器地址规范化：真实游戏收益与瓶颈变化

2026-10-28。**同尺寸、同资源、同 APK 的普通游戏对照从 43.25 提升到 49.86 FPS（+15.28%）；独立工具对照的 GPU 主批次从 22.914733 缩短到 19.818330ms（−13.51%）。每帧 GPU 读写量基本不变。** 这次命中了着色器寻址与编译结果中的实际成本，不是再次降低渲染比例，也不是靠减少画面或放弃同步提速。

本报告只验收两份 VS 的地址等价规范化。后续 FS、VS+FS 组合或其它表示实验没有包含在本轮数据中，不能沿用这里的收益宣称已通过游戏验证。

## 改了什么，为什么可以通用处理

候选用已有离线工具处理原模块 `3deaf68fd65b5298`、`54c93fa22abff9ee` 中的整数地址表达式。规则依据可证明的低位零/余数，规范化逻辑移位与常量偏移，保留 32 位模运算和溢出语义，不假设运行时骨骼索引范围，不改变 UBO 范围、精度、robust 策略、内存接口或读取结果。

- 两模块分别有 48 个不同 SSA 结果被改写，原 `prove.py` 使用 Z3 位向量对全部 32 位输入证明等价；不是抽样若干骨骼索引。
- 自定义规则前后 `OpLoad`、`OpAccessChain` 数量相同，没有在这里手动扩大读取或删掉数据。规范化后再由声明的 SPIRV-Tools 处理公共表达式，给驱动合并寻址/向量读取的机会。
- 原真实阴影 VS 的独立普通 UID、同驱动编译实验精确复现了 **349 native instructions / 48 cat6 → 142 / 12**。原 48 项是 12 组 vec4 的标量读取；它们不是 48 次寄存器溢出。该编译实验不代替实际游戏验收，也不把某个阴影链接上下文的统计挪作所有 HDR 变体结果。

工具和契约见 [地址规范化说明](../tools/experiments/spirv-ubo-address-normalize/README.md)。候选由完整、验证过的原 `29e56b51bce5` 缓存派生，输入 receipt SHA-256 为 `59320caac04154d1dd118581ed4443b563cd862b17ded88c73adc1c6da2083c2`，新链为 `1065929e0840`。两份 VS 及依赖其内容的反射/证明闭包通过原 emitter 重建和独立验证；没有对已加工 CPU 游戏 binary 叠补丁。

本轮之后又用同一规则检查了当前已精确映射的全部 9 个几何 VS：只有上述两份匹配，其余 7 份为零。最大 HDR 两主 FS 配对的 `54f730a6c0efc118`、`af64b417689666bf` 没有该移位/加法地址模式，实际 native cat6 也为零。不能将这次收益机械外推到其余 VS。原 9-key 映射是几何热点集合，不包括完整 49-pass 中所有 UI/全屏 VS。

## 固定条件与身份

- 设备：同一台 vivo V2528A，SM8635 / Adreno 735，USB 序列号 `10CG570J59001U1`。不是上游报告的 Adreno 730，也没有使用 Mali 或 6GB 平板结果。
- APK SHA-256：`498c0ff85fbabb4b5f8ab655fdd11ae019c2c852eed3f35b0588feff1f42d3ef`；host ELF：`4e97b33298e2792120a8544bf3b7e7dae14dab134279fcd8c36ff95213619592`；源码：`23808fbec74c4875357aec9a8670fcef0a0fed2b`。
- 定制 Turnip SHA-256：`12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5`。原 BC3 native 资源、共享 guest heap/lazy windows、兼容消费表均保持不变；两边都实际通过 native table 身份验收。
- 实际 game output 与 swapchain 都为 **1968×900**，3D 固定比例 **0.85**，实际主目标 **1673×765**。同时核对原运行时输出和 native window 应用结果，没有只根据启动参数推断尺寸。
- 原生 ARM64 游戏 main/SDK 执行，游戏线程内翻译、CPU7 掩码 `0x80`，固定状态缓存/extra 开启，sparse 关闭，启发式 stream submit 关闭，sysmem/UBWC 策略、原有 pixel-local/bloom 设置不变。ADPF hint、专用 Present worker、covered-clear 候选关闭。
- 相同受控角色与营地：area/client_area=1，坐标 5225/6289，等级3、经验2920、金币51；窗口内没有操作角色。28 个原游戏模块、profile、资源和驱动均有哈希准入；真正用户目录另行保留，不以改玩家状态构造结果。

完整启动参数必须使用各组 `argv.json`；上述摘要不能代替共享/native/provider/table 等完整准入配置。

## 普通运行：约 15% 的整帧收益

普通窗口只使用现有官方 Perfetto 的调度、频率与 Surface 记录，不开启 SDP、抓帧层或自定义 CPU/GPU 计时。每组设定 20 秒，结果按实际完整上屏间隔计算。

| 指标 | 原链 ORIGINAL-N1 | 规范化 NORMALIZED-N1 | 变化 |
|---|---:|---:|---:|
| 完整 Surface 间隔数 | 861 | 993 | 不强行凑相同帧数 |
| FPS | 43.2485 | 49.8556 | **+15.28%** |
| 平均间隔 ms | 23.1222 | 20.0579 | −13.25% |
| P95 间隔 ms | 33.2360 | 22.1582 | 长帧明显减少 |
| owner Running ms/上屏帧 | 18.5827 | 16.4847 | −11.29% |
| owner 运行期加权 MHz | 1625.68 | 2049.48 | **+26.07%** |

两边原 full-word specialization 变体均为32、pending=0。原链 PSO182、候选180，原组保留了此前清屏预热的两条旧管线；没有把它们称为新增编译。图像 allocation 相差48KiB，此级别变化也见于同代码原链复测，不能将它认作本次内存布局收益。

原链相对前一组同 APK/同参数的 20 秒 `covered-clear-v2/OFF-N2`，FPS 43.1791 → 43.2485，仅 +0.16%；owner Running 变化约 −0.15ms/帧。随后回切原链、开启原厂工具的正式窗口仍约43.18 FPS，与候选约49.93形成独立佐证。不过本轮不是多场景、热稳态的长期交错重复测试，不能给出全游戏保证百分比。

## 原厂指标与实际 GPU 批次

正式工具配对为 **PARITY-ORIGINAL-G2 → PARITY-NORMALIZED-G1**，两边均通过原采集健康 gate，相同 PSO180/spec32/private image313450496 签名。该组同时使用官方 Snapdragon Profiler 九指标和 Perfetto/KGSL 事件；其 FPS 是工具活动窗口观测，和普通运行结果分别报告。

| 同 Surface 窗口指标 | 原链 G2 | 规范化 G1 | 变化 |
|---|---:|---:|---:|
| 完整 Surface 间隔 | 341 | 395 | — |
| 工具窗口 FPS | 43.1787 | 49.9333 | +15.64% |
| main 批 SOP/EOP 平均包络 ms | **22.914733**（338批） | **19.818330**（391批） | **−3.096403ms / −13.51%** |
| main 批 P95 ms | 23.016630 | 19.953984 | −3.062646ms |
| present copy 平均包络 ms | 0.191300 | 0.186269 | −0.005030ms |
| 可观测硬件内部 SOP/EOP 区间并集 | 99.7044% | 99.8157% | 均持续有任务 |
| SDP GPU 工作频率均值 MHz | 499.132 | 499.593 | +0.09% |
| SDP GPU clocks/上屏帧 | 11.5597M | 10.0052M | −13.45% |
| SDP 读 MiB/上屏帧 | 100.8955 | 100.4298 | **−0.46%** |
| SDP 写 MiB/上屏帧 | 42.2034 | 42.0583 | **−0.34%** |
| SDP fragment instructions/上屏帧 | 1.8781G | 1.8804G | +0.12% |

这里的读写是原厂全局指标，含合成器等 GPU 活动，按同一完整 Surface 窗口积分再除以帧数，不是应用独占计数或峰值 DRAM 带宽。**每帧量近似不变不代表每秒量下降**：吞吐提高后每秒读写反而增加。没有据此宣称省电、降温或带宽不再重要。

GPU 工作频率近似相同、每帧读写/片元指令基本不变，同时 main 包络和 clocks/帧减少约13.5%，支持着色器寻址及其编译结果有实际收益；不能将全部改善都解释成 CPU 升频，也不能改写为“减少13.5%的 DRAM 流量”。SDP texture fetch stall 为10.34% → 11.92%，system-memory stall 为0.329% → 0.396%；本组没有支持“系统内存饱和已被解决”或“全部只是带宽 bound”的证据。

硬件关联使用同 `(context,stamp)` 的 **queued → API root**，再关联 submitted/retired；submitted 可能被延后，不能按其当时所处 API 给批次分类。每臂独立使用原驱动事件的标准时钟关系校准，最大映射残差2.45/3.28µs，约19.2MHz的 XO 校准时钟不是 GPU 工作频率。前缘38.247/47.845ms未完整观测，未补零。主批包络包含 GPU 内部停顿，不是纯 ALU、单个 shader 或某个 pass 时间。

## CPU、等待与选频反馈

同一正式工具窗口的官方调度归约：

| 指标 ms/上屏帧，频率除外 | 原链 G2 | 规范化 G1 |
|---|---:|---:|
| owner Running | 18.2578 | 16.2909 |
| owner 运行期加权 MHz | 1683.44 | 2047.13（+21.60%） |
| owner 频率覆盖率 | 99.938% | 100% |
| owner S | 4.0211 | 3.3214 |
| owner I | 0.4321 | 0 |
| owner Runnable | 0.2594 | 0.2390 |
| owner D | 0.00845 | 0.00486 |
| present wall | 4.1604 | 3.5022 |
| present 内 S | 3.6710 | 3.0260 |
| kgsl-events 对应唤醒的 S/D | 3.67065 | 3.01950 |
| 同应用其它线程唤醒的 S/D | 0.2798 | 0.2304 |
| NVN replay Running | 10.8869 | 9.1793 |
| submission planning Running | 0.9783 | 0.7273 |

最后两项等 named slice 是包含关系范围，不能彼此或与 owner 总时间相加。replay 运行期频率1646 → 2022MHz，未改的 planning 路径也从1601 → 2020MHz、同时变短。**选频反馈是本轮需要保留的真实关联，不作为噪声扔掉；但没有 PMU 指令计数，不能将 Running 缩短直接认作少执行了同样比例的 CPU 指令。** 现有配对不能确定升频分别由 GPU 睡眠、负载历史、温控/功耗策略或其它系统状态贡献多少。

owner Running 按每秒真实时间反而从788.35增至813.46ms，全应用1454.66 → 1537.51ms。它在更短周期内生产更多帧，不能据每帧 CPU 时间降低宣称总 CPU 使用率或热量下降。

present/submit 外部 queue 锁范围约0.001ms量级，没有出现长期争抢该锁能解释大差距的证据；这不等于所有 guest 锁没有成本。present 仍有约3ms、由 kgsl-events 唤醒的睡眠，不能为了去掉墙钟等待破坏同步。两边几乎整个可观测 GPU 时间都有批在执行，具完整前序关系的337/337、390/390个下一次 replay 均早于前一 main EOP。**本窗口无证据支持 CPU 提交间隙长期饿住 GPU 是主因**；剩余 main 约19.82ms本身仍超过60帧的16.67ms预算，GPU重路径应继续优先研究，同时保留约9.18ms CPU replay 的减负方向。

原链的737段 I 全在上一 NVN SyncWait 与下一 replay 之间的无 owner slice 间隙，由 IRQ 上下文唤醒，`io_wait=0`、`blocked_function=NULL`。缺少该处用户 off-CPU 栈，不能把它归为某把游戏锁或温控调用，也不把唤醒时恰好运行的 kswapd/ps 名字当作它们主动释放游戏锁。

普通与工具窗口均显示长帧减少，但最大间隔仍约33.24ms。正式组 >30ms 为33/341 → 2/395；不能说卡顿已经消失。已有11.1/22.2ms间隔及 VSYNC-app节奏不足以独立证明物理屏幕处于90Hz模式，没有借此修改暂缓的 pacing/回调设计。

## 失败记录、复现与验收边界

原 `PARITY-ORIGINAL-G1` 因 `systrace_parse_failure=4` 被原 gate 拒绝，**未进入正式对照**。后来通过原始 print/线程关联与同版本官方 Perfetto parser 定位到 lmkd 的四条普通文本：两条 medium pressure、一条 memory level change、一条 thrashing 0%。这解释了解析来源，但不据此放宽 gate、删除事件或将失败窗口改标成功，也不直接断言游戏造成内存压力/被杀。随后原链 G2重新采集并通过全部原检查。

所有记录根目录为本地 `.local/logs/native-bc3-three-arm-game-v1/ubo-address-game-v1/`：

- `ORIGINAL-N1/`、`NORMALIZED-N1/`：完整 argv、启动/模块与设备身份、资格/场景、20秒 Perfetto、前后状态、fresh save2及停机收条。
- `gpu-tools/PARITY-ORIGINAL-G2/`、`gpu-tools/PARITY-NORMALIZED-G1/`：原厂 DB/session/九指标收条、原轨迹、SQL/CSV；原厂同窗积分为 `gpu-tools/comparison.json`。
- `whole-analysis-agent/ROUND-N1.{json,zh-CN.md}`、`ROUND-G2-G1.{json,zh-CN.md}`：完整CPU/等待/频率与各阶段交集；每次官方查询均保存工具、trace、SQL及输出哈希。
- `hardware-analysis-agent/formal-comparison.json`：真实硬件批次、角色关联、时钟/边缘与持续排队证据。最初按 submitted 所处 API 分类的旧 exploratory v1保留但排除，不冒充最终归约。
- `whole-analysis-agent/trace-health-ORIGINAL-G1/`：四条系统 print、SQL、官方 parser 源版本/哈希与明确拒绝结论。
- `whole-analysis-agent/all-current-vs-normalization/`：9个已映射几何VS的真实draw覆盖、原native结果、原规则输出及Z3证明。两已选模块覆盖阴影107/107 draw/CB，在depth与HDR各覆盖51/642 draw；draw次数和索引/count参数均不冒充实际vertex invocation或GPU时间。

资源计数快照使用自身 guest-submit 分母；正式工具组分别403、402个guest submit，不强行换成341、395个上屏间隔。readback/upload bytes和CPU等待计数均为0；guest/internal各1意味着总应用 `vkQueueSubmit` 约2/guest，不能再次误报成总1。纹理准备、资源分段构造等每guest次数基本未变，说明这次没有绕过整段 CPU 资源准备。

每组结束都由原 Android 生命周期取得新 `commits=2` 保存确认后停机；原用户目录备份不因候选测试被覆盖。测试和分析只使用现成官方工具及其输出编排，没有另写计时器/采样器，也没有用探针替代真实游戏执行里程碑。

本轮可以确认：两份地址等价 VS 候选在本机同营地、同尺寸有约15%的普通整帧收益和约3.10ms的 GPU main 包络减少。不能确认：全场景60帧、长期热稳态同收益、能耗下降、纯 ALU/带宽单一瓶颈，或尚未游戏对照的 FS/组合候选收益。
