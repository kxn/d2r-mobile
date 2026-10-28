# CPU/GPU等待与调频：现有正式窗口的关键路径

2026-10-28。**owner确有约3.2ms/Surface不运行，主要等GPU相关出图栅栏；
但现有数据不支持“普遍因GPU睡眠降频，从而形成主要帧率瓶颈”的结论。**
当前可认证GPU主批次约19.72ms，CPU通常已在前一批GPU完成前准备下一批。
不能把删除CPU睡眠直接折算为可兑现的FPS，也不能忽略仍多出的CPU指令。

本报告只整理既有正式工具数据，没有ADB、新采集、运行时修改或自写计时器。
两窗口均是原c09/b828/cf83、12b、原BC3资源、game/swapchain1968×900、
主3D1673×765/0.85、营地同角色位置；GPU与PMU是分开的实际工具窗口。

## CPU究竟在运行还是等待

| 每Surface，频率除外 | CURRENT-G1正式GPU窗 | OWN-PMU2正式PMU窗 |
| --- | ---: | ---: |
| 周期 ms | 19.92401 | 19.94308 |
| owner Running ms | 16.36926 | 16.40357 |
| owner S睡眠 ms | 3.19957 | 3.23823 |
| owner D阻塞 ms | 0.00936 | 0.00271 |
| owner Runnable（R 与 R+ 合计）ms | 0.22424 | 0.24439 |
| owner运行期MHz | 2064.03 | 2052.66 |
| owner实际运行CPU | 7 | 7 |

Running约为周期的82.2%；这不是CPU全程忙，也不是CPU有充足关键路径余量的
单独证明。该线程包含guest主逻辑、HLE和inline翻译，不能把16.4ms全叫纯翻译。
线程状态未覆盖的小段保留unknown，不把差值补成睡眠。

实际睡眠的后续连续Runnable waker表明：

- GPU窗由`kgsl-events`唤醒的睡眠为2.95329ms/Surface，380段，实际最内层
  scope为`waitForever`，位于`queueBuffer`/`QueuePresentKHR`/`Vulkan present call`。
- PMU窗同源睡眠2.93225ms，964段，实际scope为`Vulkan present call`。
- 两个guest worker：GPU窗0x1004/14996与0x1005/15008分别对应约
  0.08188/0.09857ms；PMU窗0x1004/25585与0x1005/25586约
  0.12003/0.11589ms。handle到TID由各自原native日志核准，未跨PID套用。
- `dm-gpu-complete`对应的owner睡眠在这两窗近乎没有；这只说明未观察到
  大量由它唤醒的owner阻塞，不证明其锁从未造成Running内的自旋成本。

waker是调度唤醒来源，不自动等于某个mutex的持有者；未标记guest等待缺少
用户态调用栈/实际锁地址，不能从worker名称推任务/资源职责。嵌套scope不能
相加：GPU窗两层`queueBuffer`产生约5.91ms的包络，是同一睡眠被包含两次，
不是另有5.91ms可省。

## 频率变化的先后已经查询原trace

使用官方`linux.cpu.frequency`，对每段完整GPU-waker睡眠比较入睡前与唤醒时
CPU7频率，并检查睡眠中的转换及唤醒后第一段Running；没有新增时钟。

| GPU-waker睡眠 | GPU窗 | PMU窗 |
| --- | ---: | ---: |
| 总段数 | 380 | 964 |
| 唤醒频率与入睡前相同 | 346 | 883 |
| 唤醒频率更高 | 28 | 66 |
| 唤醒频率更低 | 5 | 15 |
| 边缘频率未知 | 1 | 0 |

少数确实有“先进入GPU等待、睡眠中频率下降、醒来第一段Running仍低频”的
时序。例如GPU窗一次2150.4→1900.8MHz，另一段1900.8→1766.4MHz。
这些实例保留在原SQL输出，不能说此现象不存在；但约91%的GPU-waker睡眠
没有降低唤醒频率，升频实例还多于降频。

CPU7全部降频转换中，GPU窗153次发生在owner Running、8次在S、5次在R；
PMU窗380次在Running、21次在S、18次在R。当前证据不支持把所有低频或
整个帧率差归为GPU睡眠触发。频率与owner状态有先后关系，不足以孤立WALT
利用率、IRQ/其他线程、功耗/温控策略的原因；本两窗口没有频率limits事件，
也不能由“没有事件”证明没有外部cap。

## GPU有没有被CPU饿住

同一CURRENT-G1正式硬件归约：认证main394批平均19.71807ms，present copy
394批平均0.19057ms；不是按最长job推角色。应用SOP/EOP内部观测区间约
7880.98ms，其中7869.52ms被job span覆盖，比例99.8546%；间隙共11.455ms，
约0.02885ms/Surface。前缘28.851ms未观测，保持unknown。

**SOP/EOP包含GPU内部停顿/同步，不是ALU busy或有效运算率。** 因此不能
仅凭这个比例宣布GPU完全高效，也不能排除内部barrier、访存或队列依赖。
但它不支持当前主要损失是大段“CPU没提交，GPU完全空闲”。

复用同批已有KGSL clock pairs和原19.2MHz XO映射（最大残差2.554µs），
对396个能匹配前一main硬件EOP的replay，396个在该EOP前开始、388个也在
EOP前结束，中位结束余量约3.50ms；8个较晚结束和首个未知保留，没有挑掉。
这证明大多数CPU准备确在与前一GPU批次重叠，不能把串行相加的CPU/GPU预算
当帧时长。XO是标准时间戳时钟，不是GPU shader频率。

## 等待最多能省多少，和指令预算怎样区分

- 毛预算：GPU窗S+D约3.20894ms，Runnable约0.22424ms；PMU对应3.24094/
  0.24439ms。假设全部消失也只是owner路径的理论上界，包含真实GPU依赖，
  不能当已经可删除的等待，更不能与GPU主批次19.72ms再相加计算收益。
- 直接由两guest worker唤醒的睡眠毛预算仅约0.180/0.236ms；即使完全消除，
  本两窗也没有由它独占回收数毫秒的证据。Running内可能的active polling不在
  此预算中，仍需原PC/语义证明，不能反向补猜。
- 保持原GPU工作和队列行为时，已观测应用job间隙仅约0.029ms/Surface；
  已认证主批次本身比60FPS的16.667ms多约3.05ms。只删CPU出图睡眠并未
  证明能同时消掉这部分GPU服务预算或内部停顿。
- 原正式PMU的owner用户态指令仍为70.743～70.791M/Surface，上游官方原生
  库为52.947～52.987M，多约33.5%；时间未全Running不意味着这些额外指令
  免费。`:u`不含内核/其他线程，不能用Running×MHz推指令，也不将本次不同
  实现比较独占归因共享内存。

下一步应继续根据实际GPU组/着色器预算减少主批次工作，同时保留CPU整段准备
省工与依赖正确性。少数低频唤醒实例可作为后续具体假设，当前不能将其包装为
普遍的恶性反馈根因；也不需要为这个审查再采一批相同基线。没有修改提交节奏、
帧回调、调频或系统保护策略。

## 证据入口

新增只读归约在`.local/logs/final-c09-upstream-parity-v1/critical-path-waits-agent/`：
`REPORT.json`、两窗`query.sql/csv`及`result.json`、
`replay-vs-existing-hardware-EOP.json`。复用的原scheduler、GPU时钟/序列、
完整PMU边界与来源hash均保留；初次SQL拼接空格错误的输出亦留档，未改原trace。
原硬件报告为同级`hardware-analysis-agent/REPORT.zh-CN.md`，原PMU为
`cpu-pmu-v1/whole-analysis-agent/REPORT.zh-CN.md`。
