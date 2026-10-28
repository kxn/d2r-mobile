# Store 附件域过滤：v2 已净省 owner 工作，稳定涨帧仍未证明

2026-10-28。**当前 v2 的同 APK 对照，每 Surface 的 owner 用户态指令减少
约 0.43～0.52M（0.60%～0.73%），cycles 减少约 0.19～0.23M。普通运行
仅一对 50.2065→50.3858 FPS（+0.36%），不能称稳定涨帧。** 已据净省工将
源码默认改为开启，保留显式关闭；实际安装仍是原 c09，尚未发布新默认包。
同时记录其他线程和 guest 工作变化，不把局部省工写成稳定整帧收益。

v1 的整线程指令曾增加，属于下面保留的历史结果，不能继续当作当前 v2 状态。

## 改造与约束

Store 查询只消费已经成为物理 pass 逻辑附件的目标。候选维护初始
`S.targets` 与已经遍历的 `SET_RT` 的单调累计域；能证明与这个域无 serial/
内存别名的 Shader 读取，不再追加 Store READ 事件。以后增加附件时，
此前的负证明检查新增目标；已证明相关的资源在本次构建内保持相关。
未知描述符继续产生全局需求，描述符 epoch 变化仍使整个派生索引回退。

没有第二次完整命令预扫，复用原 descriptor/bucket footprint。v1 引入约
24KiB 静态元数据，开启时每次构建清理 16KiB，并新增证明判断/范围比较。
v2 在相同语义下提前处理已知负证明，减少重复 footprint 读取及相同 proof
回写。它仍逐 draw 解析 Shader 读取槽和访问 descriptor 缓存；**只省
事件产物，不能把旧约 2.46M 前向读取预算全部当作可删除工作。**

不跨 submission 缓存、不合并跨 seq 事件、不改游戏流程、真实依赖或提交
顺序。单次只读快照本轮在 `mtl_exec` 命中时发现 replay 已非0而严格拒绝，
未用于证明当前 read-list 稳定覆盖率，没有写变量使快照通过。

## v2 正确性与固定条件

主机真实生产规划器差分覆盖15场景/840个判断，ASan/UBSan及四个错误实现
负控通过；主机测试不能替代真实游戏功能检查。新普通APK实际两种冷启动
模式分别运行原 Store reference cross-check：

| 独立功能运行 | 比较计数前→后 | mismatches |
| --- | ---: | ---: |
| OFF-CHECK1 | 21,586→144,433 | 0 |
| ON-CHECK1 | 18,662→309,245 | 0 |

真实开启模式经过实际跳过分支，未见明显新增显示错误；不代表全游戏场景
已经覆盖。功能检查使用已有原生算法，未开 validation/自定义计时。所有
normal/PMU 性能臂实际检查关闭，comparisons/mismatches全程0。

- Q735/Adreno735，同原营地角色/位置；game与swapchain实际1968×900，
  主3D1673×765/0.85，核准实际创建日志与状态。
- 两臂同APK `f3576a49a9fa8ab14c529549de9bfba2f751603e8d4a2d470b3a50f5ff22c9a0`，
  host `26a104a89007880217939c8b590e6e54b68d85cb9ed738f02972a68fd9b8eb0b`，
  source `628398896ebbb9ab2627f4eeb5fa3c45d4d80f89`；1056构建输入/原生库核准。
- cf83 Shader、12b8 Turnip、原BC3 native pack、table6890/producer919；
  CLEAR ON、inline CPU7、30µs、fixed+extra ON，sparse/stream/hints OFF。
  唯一启动差异为 Store attachment domain 布尔值。
- 独立新事务/新结果目录，没有拿旧c09或v1的OFF充当同APK基线；不复采官库或GPU九指标。

## v2 普通20秒整轮结果

| 指标 / Surface，频率除外 | OFF-N1 | ON-N1 |
| --- | ---: | ---: |
| FPS | 50.2065 | 50.3858 |
| mean ms | 19.91773 | 19.84684 |
| p95 ms | 22.15849 | 22.15656 |
| owner Running ms | 16.55519 | 16.49943 |
| owner运行期 MHz | 2075.51 | 2064.30 |
| 全应用 Running线程和 ms | 32.09735 | 31.66597 |
| 非owner Running线程和 ms | 15.54216 | 15.16655 |
| owner S睡眠 ms | 3.06052 | 3.07821 |
| Present调用内S睡眠 ms | 2.78780 | 2.78852 |

owner少0.05577ms（-0.337%），运行期频率低0.54%；每秒owner占用约
0.83118→0.83134核，几乎相同。全应用每帧减少0.43138ms，主要是非owner
减少0.37562ms；completion worker反而多约0.02145ms。不能将全部减少归给
翻译流程，也不能从线程时间直接推指令迁移。未覆盖频率区间没有补0。

原trace互斥阶段Running：finalization −0.01401ms，planning排除它后
+0.00962ms，故规划总计仅−0.00439ms；replay排除planning后+0.09736ms，
其他未标记owner−0.15703ms。包含关系不相加，所有Running严格闭合。
本次Present等待基本不变；没有v1普通pair那种约0.18ms等待变化。

较宽工作快照分母为OFF1040/ON1085次guest提交，不是1000/1004个Surface。
Store事件约15199.19→5758.14/guest，下降约62.12%；descriptor访问约
12044.50→12025.02、shader阶段4856.70→4852.90，基本保留。ON约78.46%
的considered访问被跳过，约9435次/guest，另有5674次范围比较及541次负证明
复审。78.46%包含旧同seq去重访问，**不是净事件减少78.46%**。
纹理prepare约+0.46%、miss约−14.42%，也属于本轮实际工作差异。

## v2 官方PMU净工作与全应用成本

原32秒Simpleperf记录覆盖官方Perfetto20秒窗口，沿用原时钟转换、完整累计
counter和四个采样边缘；未以CLI前缀EventCount或Running×MHz推指令。

| owner用户态事件 / Surface | OFF-P1 | ON-P1 | ON−OFF |
| --- | ---: | ---: | ---: |
| instructions M | 71.93141～71.97826 | 71.45437～71.50308 | −0.52388～−0.42833 |
| cycles M | 30.64731～30.66540 | 30.43961～30.45585 | −0.22579～−0.19146 |
| owner Running ms | 16.58144 | 16.45789 | −0.12355 |
| owner运行期 MHz | 2063.12 | 2060.69 | −0.118% |
| 全应用Running线程和 ms | 31.57899 | 31.88007 | +0.30108 |
| 非owner Running线程和 ms | 14.99755 | 15.42218 | +0.42464 |

这里的区间是时间边缘计数界，**不是统计置信区间**。`:u`仅owner用户态，
不包含内核或其他线程。两工具退出0/属性恢复，真实collector/PID/owner/ELF、
原数据SHA及边缘覆盖核准；原trace健康门和record loss/truncation均0。
PMU工具FPS50.2105→50.2857只作诊断，不替代上面的正常运行FPS。

全应用并非同时省时：非owner增加0.42464ms，两个主要普通worker分别约
+0.12529/+0.20480ms，Java主线程约+0.08914ms，binder约+0.15201ms；
completion worker却减少0.02949ms，其他线程也有下降。这些不是可独立相加
的根因列表，也没有其他线程的精确指令counter。每秒全应用约1.58560→
1.60311核，owner约0.83256→0.82760核。正常与PMU的非owner方向不同，
因此不声称已经稳定降低总CPU/功耗/温度。

同窗原slice中，OFF1000个Surface含999次完整planning、998次完整replay
并有1次跨尾、998次present、1996次submit；ON1002个Surface含999次
planning/replay/present、1998次submit。ON完整planning/Surface略低约0.2%，
present略低约0.1%；边缘保留未知，不能把全部指令净减独占归因于helper。
没有ON每Surface多执行整周期的证据。

## v2 宏观指令归因和边界

用actual26a1 ELF/DWARF、官方Simpleperf原链、LLVM内联源和原同owner阶段
逐样本归属。实际规划scope约 **5.94167→5.47403M I/Surface（−0.46765M）**，
与整owner sampled净−0.47342M同量级，但其他职责存在相互抵消。

| 规划互斥职责，M I/Surface | OFF | ON | Δ |
| --- | ---: | ---: | ---: |
| Store事件/资源解释 | 2.9806 | 2.3101 | −0.6705 |
| domain helper | 0（未采到） | 0.1490 | +0.1490 |
| walk非Store元数据 | 1.3695 | 1.5811 | +0.2117 |
| reverse/init/未细分 | 1.1310 | 1.0275 | −0.1035 |
| finalization实际范围 | 0.4606 | 0.4063 | −0.0543 |

ON domain只有6个样本，其他细组也仅几十样本，均不是精确调用次数或每次
执行成本。不能把旧v1到v2的helper点差当成静态改写独占收益。

其他互斥预算：资源准备约−0.6203M、replay内driver无细祖先−0.3827M、
HLE线程同步−0.1623M；guest SDK约+0.6925M、main+0.1257M，draw剩余
+0.1939M、program/PSO+0.1520M、提交/出图范围+0.1403M。不能将所有新增
都归domain，也不能只列减少项。独立SELF执行层是host−0.5810M、driver
−0.5697M、系统库−0.1423M、guest main+SDK+0.8182M及少量unknown变化，
严格闭合sampled净差；inclusive祖先预算另留档，不与SELF相加。

OFF sample period和71.928639M，比独立累计counter下界低0.002769M/
Surface（约0.00385%）；ON period和71.455217M位于原界内。旧归因层假定
两者必闭合的断言失败已保留。没有缩放sample period、改counter数学、放宽
原健康门或重采挑有利数据。正式净工作只采用累计界；sampled净差位于该净界
内，但细项依然保留采样分配不确定性。

## SDK mutex/TLS线索

已找到与实际加载SHA完全相同的SDK `aacd2701…69712`，原manifest和7项
SDK产物全哈希核准。官方LLVM解码及同原始NSO的符号元数据表明：351780块
在 `InternalCriticalSectionImplByHorizon::Enter()`，其call经39fb5c和原门表
svc26进入HLE `ArbitrateLock`；f20040/f20100块是原TLS读指令的既有平台
TLSDESC取址stub。它们不是memcpy或新写的游戏锁逻辑。

v2同样看到351780块约2.1664→2.5807M；叶PC几乎全在函数epilogue3517b0，
不是全在重试支路。PMU precise_ip=0，不能把这份预算当busy-spin次数或争用
调用次数。官方Simpleperf dump证明现有record没有保存user registers/stack，
不能恢复实际mutex地址或owner/tag；不能据此认定HLE错误/更改游戏锁。

## v1历史（独立旧APK，不是当前对照）

v1为b53/C2f/sourcef2d，同尺寸/场景/资源的独立OFF/ON。原11场景/762判断
及3负控通过，真实两模式原Store检查均零不匹配。普通50.4511→50.0497
FPS（−0.80%）；Present内睡眠多约0.18217ms，不证明GPU硬件变慢。
PMU owner指令多0.59858～0.68963M（+0.84%～0.97%），cycles也增加。
但其planning实际已从6.0557降到5.7046M（−0.3511M），不是管理把所有
目标省工完全抵消；其他host/guest职责改变。这些历史负结果全部保留，
没有直接拿它们替代新v2的同APK基线。

## 当前状态与证据

按AGENTS第13条，净owner指令/cycles已经下降，24KiB静态元数据和16KiB
构建清理有界且计入净工作；没有新增线程、锁或复制路径。正常/PMU非owner
变化方向不同，现证据不足认定明显资源成本退化。因此不能仅因未证稳定
涨帧长期默认关闭：源码的后端、JNI和Java启动默认已统一ON，显式0/OFF
仍可回退。新源码默认尚未作为普通APK发布，不冒称手机已经切换。

后续优先查当前资源解析列表整体复用覆盖及SDK线程同步的实际身份；保留
每seq事件、未知描述符和epoch回退。没有当前稳定覆盖率时，不先增加新的
每draw检查层。默认策略审查见原目录 `pmu-attribution-agent/default-policy-review.*`。

本轮已重新安装精确c09普通APK，**恢复本次入口的22个真实用户文件**，
UI偏好及accepted桌面锚点一致，恢复用户后未冷启动；游戏和拥有的工具均
无进程，perf_harden=0。一次只读snapshot严格拒绝后已detach、原save2/stop，
owned server/forward清理；Mali/平板未使用。

v2完整证据在 `.local/logs/store-domain-negative-fast-device-v2/`：
`functional-qualification.json`、四个normal/PMU臂、
`normal-whole-analysis-agent/`、`pmu-whole-comparison/`、
`pmu-attribution-agent/`、`guest-sdk-hotblocks-agent/`及`final-restoration.json`。
精确源码/原始sample chains/官方LLDB和LLVM输出、失败断言/负控均保留。
v1原始证据继续在 `.local/logs/store-attachment-domain-device-v1/`。
