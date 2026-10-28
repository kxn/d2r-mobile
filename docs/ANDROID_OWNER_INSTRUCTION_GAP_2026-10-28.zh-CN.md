# 同尺寸官库对照：帧率提升后仍多出的 CPU 工作

当前普通桌面 c09/cf83 在相同尺寸营地已经快于官库原生引擎，约
50.48 对 43.45 FPS；GPU 每帧写入约少 3.36%。这不证明共享路径的
CPU 开销已经解决。新的官方 PMU 对照显示：我们的 owner 每上屏帧
仍多执行约 17.8M 条用户态指令，差额主要在宿主准备代码。

本报告是 [实际官库对照](ANDROID_DELIVERED_NATIVE_PARITY_2026-10-28.zh-CN.md)
的 CPU 归因补充。不能用这里的工具窗口 FPS 替换普通运行结果，也不能
把高层预算当作全部可删除工作。

## 同窗硬件计数与整应用负载

同一 Q735、game/swapchain 1968×900、3D 1673×765 / 0.85、原 BC
资源及受控营地。我们为 c09 APK、b828 host、cf83 Shader 链、12b8
Turnip；官库为 b8 隔离载体及与原 release 逐字节相同的 e847 原生引擎。
Java/包名有隔离适配，不把载体叫未修改的原 release APK。

使用系统现有 Simpleperf `instructions:u` 事件及附加 `cpu-cycles:u`，
199Hz、DWARF 调用栈；32秒记录中按官方 Perfetto 时钟关系截取实际完整
20秒 Surface 窗口。双方均认证实际 gettid/Pid/Tgid、工具与 ELF 身份，
原数据无丢失/截断，原轨迹健康门通过；未增加自定义采样或计时。

| 每完整 Surface 间隔 | 官库 | 当前 c09 |
| --- | ---: | ---: |
| owner 用户态指令 M | 52.9467–52.9868 | 70.7431–70.7914 |
| owner 用户态 cycles M | 30.5080–30.5377 | 30.1908–30.2124 |
| owner Running ms | 19.0009 | 16.4036 |
| owner 运行期加权 MHz | 1755.66 | 2052.66 |
| 全应用线程 Running 和 ms | 39.3720 | 31.0853 |
| 非 owner 线程 Running 和 ms | 20.3711 | 14.6817 |

指令多 33.51%–33.70%，cycles 仅少约 1%。当前平均 CPI 较低、运行频率
较高；因此此前 Running 下降不能作为 CPU 运算量已经减少的证据。计数
区间只是四个采样边缘带来的界限，不是统计置信区间。指令差额不能直接
线性换成可省时间或 FPS，也不据 CPI 反推 cache miss。

全应用每秒 Running 约 1.700→1.559 核，未看到 owner 的时间节省全部
搬到其他应用线程。非 owner 没有 PMU，仍不能证明没有指令迁移。当前
完成回收线程约 0.895ms/Surface、覆盖到的运行频率约 702MHz；没有依据
把全部额外 owner 工作归罪于回收锁或跨线程交接。

## 先按执行位置，再按完整工作职责拆分

下面来自双方实际窗口的逐样本指令 period，单位 M/Surface。它们是采样
归属预算，不是函数精确计数。缺少驱动上层栈的样本保留未知，不补到宿主。

| SELF 执行位置 | 当前 | 官库 | 差额 |
| --- | ---: | ---: | ---: |
| 宿主 ELF | 45.047 | 29.393 | +15.655 |
| 游戏 main + SDK | 12.497 | 10.340 | +2.157 |
| 驱动 | 5.416 | 6.229 | −0.813 |
| 系统运行库及其他 | 7.808 | 6.986 | +0.822 |

宿主额外 15.655M 严格闭合为：planning/prebegin +6.092M；资源来源及
描述符准备 +3.854M；合并后的 draw/PSO/state/execute/submit +6.072M；
其他 −0.363M。合并项避免两边函数组织与内联不同造成假差。资源整包
含真实下层子调用为 22.925 对 20.584M，净差 2.341M；不能把整个包或
全部宿主差额都当重复工作，也不能把下层节省独占归因于共享内存。

## 当前优先级与可证伪边界

1. **Store 前向读事件的生产。** `mtl_sub_index` 包含子调用共 6.569M；
   其中前向 Shader/纹理读取事件收集约 2.460M，事件 prefix/scatter 另约
   0.267M。调查能否在不新增完整命令预扫的前提下，不为不会影响任何
   附件 Store 判断的采样资源构造这些事件。必须证明 serial/内存别名、
   初始目标、CALL/JUMP、ghost、epoch 与真实查询顺序；native/resident
   本身不是无别名证明。不能只在完成所有解析后再加过滤。
2. **已建立 Render Job 的 RT/DS/context 提前复用。** 精确 PC/DWARF
   中约 1.546M 的毛工作包位于既有附件/PSO命中之前。可研究由 setter
   与 job 生命周期提供失效身份，避免下一 draw 重做兼容比较和构造；
   仍需保留资源回调、query、DS、mip、ghost/直接恢复失效。尚无当前
   实际 eligible 次数，不能承诺全省或折算毫秒。
3. **剩余 PSO lookup 的不可变 recipe。** 约 1.079M 毛包络；已有
   prepare 早复用约 83.76%，不能再给所有 draw 加一层大 key/cache。
   需要先证明剩余组合反复出现，资格和次数仍是缺口。

不优先继续别名图本身：仅 1 个样本约 0.025M，排序约 0.215M；小组
误差很大，新增区间结构或 T×B 遍历可能抵消。也不盲扩 program 缓存：
真实控制段 hash 仍有成本，但现记录没有 program serial 参数，尚不能
分清冷对象、新 serial 与真实碰撞。

规划器产物是 Store 覆盖证明、query 活性、DS 需求、state run 和有序
前瞻，不是旧 Pool 镜像上传计划。当前 query elision、pixel-local、bloom
均实际命中；共享开启不构成删除整段的理由。旧 NOPLAN/sparse 实验的
消费者损失与管理成本反证仍有效。

小补丁 [Store opcode 过滤](ANDROID_STORE_OPCODE_PREFILTER_2026-10-28.zh-CN.md)
已实现并通过生产 oracle/负控，尚未 Android 对照；它只省空 dispatch，
不能把整个 6.569M 记作它的收益。设备目前仍为已验收 c09，未部署这项。

## 原始证据

根目录 `.local/logs/final-c09-upstream-parity-v1/cpu-pmu-v1/`：

- `OWN-PMU2/perfetto/`、`UPSTREAM-PMU1/`：正式 record、轨迹及完整收条；
  `exact-instruction-comparison.json`：未改的累计计数边缘归约。
- `whole-analysis-agent/`：整应用线程 Running、频率、计数完整性和未知。
- `owner-macro-comparison-agent/`：实际裁窗样本、互斥祖先与执行层守恒表。
- `store-alias-budget-agent/`：逐 PC/inline 的 Store 子预算、全部查询入口。
- `draw-residual-budget-agent/`：双方 exact ELF 的 707/624 个宿主残余样本
  及失效合同。`planner-consumer-audit/`：真实生产字段和消费者。

失败 OWN-PMU1 未用于本对照。首次工具身份检查失败及后续以真实6秒官方
自目标探测确认 shell/run-as 读取权限差异的修复均保留；没有放宽 PID、
starttime、cmdline 或事件健康认证。当前用户22份存档和启动配置已恢复，
游戏及采集工具均停止。目标仍进行中，额外 CPU 准备工作尚未解决。
