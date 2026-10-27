# 整轮对照：减少了什么、增加了什么，以及为什么没有同比涨帧

## 以后每轮的固定入口

```sh
python3 scripts/dev/compare-profile-runs.py REFERENCE_ARM CANDIDATE_ARM --output REPORT_DIR
```

该程序读取现成 Simpleperf/Perfetto/SDP 整理结果；必要时自动调用官方
trace_processor 查询原轨迹的调度状态，不增加设备采样器或运行时计时。
报告先列整帧、全应用/owner、运行频率、运行/睡眠/Runnable、完整互斥职责，
然后列具体 SELF 增减、线程和资源计数。未闭合状态、缺祖先、未知归属不补零。
没有 GPU 数据时不会自动宣称 GPU bound。

本要求已加入 AGENTS.md 第14条。方法和字段资格见
[整轮 Profile 工具说明](ANDROID_PROFILE_RUN_COMPARISON.zh-CN.md)。

## 1. Stencil/顶点布局去重：净减工成立，但也有新增成本

最终部署所用 native437 的同 APK 对照为 `d015532`，fixed1/sparse0，
只改变 extra0→1，游戏1968×900、3D比例0.85及同 B3 保持。

| 指标 | OFF | ON |
|---|---:|---:|
| FPS | 43.030 | 43.277 |
| owner Running，ms/Surface | 18.749 | 18.586 |
| 全应用 Running，ms/Surface | 35.337 | 34.879 |
| owner 退休指令 | 原硬件计数边界 | 减少3.75～3.90% |
| owner 用户态周期 | 原硬件计数边界 | 减少3.33～3.49% |
| owner 运行期频率变化 | — | −2.55% |

同窗口采样职责减少：planning约0.873M、状态重放0.906M、录制0.256M
instructions/Surface；同时 binding 增加约0.556M、CPU source检查0.213M、
other replay0.200M、HLE0.105M。unknown和宽泛other的减少也列出，
不能全部归给候选，更不能仅报目标路径减少。

实际睡眠 S 增加约0.112ms/Surface、Runnable略降；主要后续唤醒来源为
kgsl-events。唤醒者名称不能证明整个睡眠的具体等待原因。
资源计数按同端点876/896个guest submits归一化，不偷用856/861个Surface间隔。

另一份同native诊断 APK 使用原 CMDSTAT，两轮完整300-present窗口分别证明
新记录约31575.5→22285.5/帧（−29.42%），字节1332.85→1061.60KiB（−20.35%）。
该诊断有原mutex开销，不拿它的FPS作净性能证据。

结论是净CPU工作量减少；单对FPS约+0.57%还不是持续涨幅或温控收益证明。
实际CPU时间的下降远小于指令下降，频率变化是必须一起报告的观测。

原始整轮报告：`.local/logs/sparse-plan-v1/deployment-extra-whole-profile-comparison/`。
初版eff4fa1额外去重对照也保留，但不拿其−4.3%替代最终native437的实际值。

## 2. 紧凑规划：完整报告直接暴露了工作转移

两版均默认OFF；host/GPU回归通过不等于真机性能通过。

| 同包ON相对OFF | 窄span v1 | 完整pure span v2 |
|---|---:|---:|
| FPS | 基本持平 | 43.325→43.304 |
| owner退休指令 | +0.64～0.78% | +0.01～0.16% |
| owner用户态周期 | 略减 | +0.45～0.64% |
| owner Running | 18.749→19.009ms | 18.620→18.747ms |
| planning采样职责 | +0.290M | −0.899M |
| 状态重放采样职责 | +约1.022M | +1.049M |

v1不是因为方向叫“稀疏”就无效：实际reverse SELF减少约0.625M，
却被walk SELF增加约0.740M以及state-segment管理等抵消。
v2分离Store解释与span资格，合并全部pure setter、只在边界写pending计数，
已经让walk与reverse分别减少约0.605M、0.863M。

但v2状态应用仍增加：`vk_state_op` SELF约+0.576M，
`vk_pass_state_segment` SELF约+0.340M；另有binding/source等变化。
因此只看planning减少15.6%就宣布优化成功是错误的。
表内父职责与SELF是两种视角，不能再相加计算净额。

新报告还抓到planning内SHA采样增加约0.488M。
真实祖先是原shader查找/内容哈希，不是新添的“span hash”；
需要结合调用次数或重复窗口核实，不把采样变化强归为新增代码。

候选保留并继续削减已定位的重放成本，暂不写入默认启动配置。
不是仅因FPS未涨而抛弃，而是当前测得新增工作确实抵消了省工。

原始报告分别为`.local/logs/sparse-plan-v1/whole-profile-comparison/`和
`.local/logs/sparse-plan-wide-v2/whole-profile-comparison/`。

### v3：省去状态段管理后，净用户态指令下降，但总CPU时间尚未下降

`0e950cd` 利用已经验证的单span不变量，去掉每个setter的跨节点循环和
重复cursor解码，仍逐条执行原状态应用。真实规划器的ASan/UBSan与八组
同步/异步GPU、共享查询回归通过。以下是真机同APK，只切sparse0→1，
fixed1/extra1、B3、1968×900、0.85均保持的第一对结果：

| 指标 | OFF | ON |
|---|---:|---:|
| FPS | 43.172 | 43.244 |
| owner用户态退休指令/Surface | 68.213～68.264M | 67.144～67.197M |
| owner用户态周期变化 | — | −0.94～1.10% |
| owner Running，ms/Surface | 18.457 | 18.660 |
| 全应用 Running，ms/Surface | 34.645 | 34.975 |
| owner运行期频率变化 | — | −0.66% |
| owner S，ms/Surface | 3.777 | 3.545 |
| owner Runnable，ms/Surface | 0.2895 | 0.2825 |

净用户态指令少1.49～1.64%，但不能把它写成CPU时间少了：Running反而
增加1.10%，全应用增加0.95%。PMU记录的是`:u`，Perfetto Running包含
内核执行；平均频率也不足以反推出内核耗时。这个差异仍须审查，不能把
约0.23ms的睡眠减少当成更快完成，更不能把未知差额编成一个锁或GPU原因。

采样职责中planning少1.252M（约22.6%），但state多0.630M、source检查
多0.665M、binding多0.462M、录制多0.140M；这些变化不是单独函数的精确
计数，也尚未证明全由改动导致。`other_draw`少1.332M与outside unknown
少0.657M仍保留未知身份。状态段SELF仍增加约0.366M，说明不能仅凭简化
源码宣称这部分新增成本已经消失。

资源快照各自跨886/896个guest submits，独立于859/860个Surface间隔；
ON快照没有完全括住Surface窗口，不能用其1～3%的资源次数变化证明同窗
CPU成本因果。原始lost/truncated均为0；这并不能消除游戏动画、采样分配
和调度差异。需要重复性证据后再决定默认策略，现保持sparse OFF。

新增的样本支撑栏进一步限制了SELF结论：本对OFF/ON总计2968/3017条，
每Surface只有3.46/3.51条；`vk_state_op` SELF为26/45条、
`vk_pass_state_segment` SELF为20/37条。它们不是调用次数，不能把几十条
样本的变化当作精确新增了多少指令。净硬件counter与函数采样分配分开报告，
后续反序复核须同时核对两者。

离线调度审查确认两臂owner全部Running都在CPU7、迁核0、频率覆盖完整。
原trace缺syscall/IRQ事件及utime/stime counter，不能据此分离内核时间。
资源after快照早47.44ms来自原1Hz状态文件的旧末行，不是BOOTTIME/
MONOTONIC混用。证据见`v3/causal-review/`（完整根目录同下）。

本轮后已恢复accepted日常APK `39197b1c…`、native `437c58f5…`及原profile6，
完成原保存后进程已停止。没有把实验APK留在设备上冒充已接受版本。
完整报告：`.local/logs/sparse-plan-segment-v3/whole-profile-comparison/`；
恢复凭证：`.local/logs/sparse-plan-segment-v3/pmu/accepted-restoration.json`。

## 3. GPU负载因果对照：有响应，但不能草率定为纯带宽

同d015 APK、同场景、fixed1/extra1/sparse0，输出1968×900与Surface2358×1080固定。
只将3D比例0.85→0.65→0.85。使用官方SDP九指标及同窗Perfetto，
这属于工具窗，不改称普通无工具FPS。

| 指标 | .85 | .65 | 回.85 |
|---|---:|---:|---:|
| 实际主3D | 1673×765 | 1279×585 | 1673×765 |
| FPS | 43.048 | 51.339 | 42.983 |
| GPU clocks/s，百万 | 499.239 | 498.660 | 499.137 |
| GPU读取，MB/完整Surface | 103.719 | 77.803 | 104.188 |
| GPU写入，MB/完整Surface | 44.926 | 31.195 | 45.027 |
| Fragment指令，G/完整Surface | 1.8794 | 1.2551 | 1.8835 |
| owner Running，ms/Surface | 18.751 | 16.301 | 18.288 |
| owner运行期MHz | 1601 | 2103 | 1683 |

减少41.5%的主3D像素，GPU每帧读取、写入、片元工作确实下降，FPS提高约19.3%；
恢复比例后GPU工作和FPS也复现。这排除了固定43FPS上限，证明像素相关GPU负载
确实影响当前运行点。不能因为总带宽下降就宣布DRAM饱和，也不能把重叠stall百分比
相加或乘帧长当独占时间；数据是global/system，包含compositor。

CPU也同时升频，不能把全部涨幅归给纯GPU。回.85那轮比.65观测年龄更短却CPU
频率更低，且.65整个窗口保持约2.08～2.13GHz，反对简单的统一启动boost解释。
原trace没有实际boost/限频原因事件，具体vendor调速机制仍未定位。

目前有证据支持的解释是：CPU省工不等于同幅度的CPU耗时缩短，频率和等待也在变；
同时原GPU像素工作没有因CPU去重而减少。GPU负载干预又改变了CPU调速，二者不能
靠一个单项指标拆开。尚未完全分离其贡献，也尚未解决具体Shader/driver的全部差距；
不能用“可能另有瓶颈”替代上述已验证变化与仍缺的证据。

完整官方数据、逐秒频率/Surface及资格边界在
`.local/logs/scale-causality-d015-v1/causal-review/`和`gpu-tools/`。
本次工具结束时清理碰到已退出collector的/proc竞态，失败保留；后继逐PID确认
全部本次GUI/Xvfb/collector均不存在，记录`gpu-scale-causality-v1/gpu-tools/closed-verified.json`。

## 4. 与上游差距的重新核对：不可继续沿用早期大差距

并行复核已有官方GPU结果后，当前d015两轮0.85工具窗为43.048/42.983 FPS，
上游系统驱动默认策略S4为43.244 FPS，相差约−0.45～−0.60%。每完整Surface
GPU写入分别为44.926/45.027 MB，上游45.834 MB；当前反而少1.76～1.98%。
没有证据继续声称当前仍多写几十MB、因此慢上游十多帧。

三者游戏输出1968×900、主3D 1673×765、同角色营地落点。上游最终交换链
实际1968×900，当前2358×1080，global计数含系统合成，因此仍非同Surface、
更非仅改变共享的单变量对照。S4是保留原后端的b811驱动控制诊断包，不冒充
官方release逐字节相同的native。上游无工具普通窗43.285/43.358也单独保留，
不能与有GPU工具的当前窗直接计算涨幅。

上游同B3控制T3只有39.874 FPS；它用于排除部分驱动混杂，不拿它宣传当前
全面领先上游8%。quad→fine derivatives、常量分量折叠和单写临时变量转发
已在当前29e56链执行；三个改写器与固定上游b811逐字节一致，基础及特化
实际payload也经官方spirv-dis验证。它们不能再当作尚未尝试的高收益候选。

完整资格、默认/同B3并列表、实际产物证明及已排除实验见
`.local/logs/gpu-next-priority-20261027/REVIEW.zh-CN.md`。这一复核不等于达到
60 FPS或完成所有场景；当前优先级回到减少CPU准备整段工作与验证净成本，
找到有真实调用/工作预算的新GPU操作后再开实验，不为凑方向重复旧开关。
