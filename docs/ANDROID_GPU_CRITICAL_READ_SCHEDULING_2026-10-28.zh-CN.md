# GPU依赖读取调度：实际编译、同条件整帧结果与下一步（2026-10-28）

**关键采样及首个SY确实提前，但本轮没有获得整帧收益。** 同carrier、shader、资源、几何和场景，BASE约50.37 FPS、候选约50.14 FPS；GPU主批次19.654→19.734ms。原厂纹理等待指标略降，整体shader stalled略升。没有发现新增大锁、CPU降频或GPU供给空泡，不能用“CPU掩盖了大幅GPU收益”解释这组结果。单个对照也不足以证明稳定回归；保留机制和源码，当前不替换正常12b驱动。

## 实际修改了什么

原12b驱动在FS首主block中先发材质纹理，再发t6采样。t6结果又参与计算首个动态UBO地址。这是一条采样→地址计算→常量读取的真实数据依赖；并非所有load都串行，原驱动已能重叠许多独立采样。

三个候选阶段均只改IR3编译器，不改NVN语义、游戏binary、图像布局、内存导入或UBWC：

1. v1在寄存器分配前按真实SSA识别读取链。官方完整IR3阶段输出证明，前阶段顺序改善后来又被post-RA调度改变。
2. v2用独立标记保留优先级，post-RA仍只选择原合法DAG heads。实际官方head日志指出：t6已是head，但要等两个硬延迟槽；原策略先发材质纹理，而这些纹理不推进所需ALU延迟槽。不是标记丢失，也不能删硬延迟。
3. v3在此窄条件下用现成、无SS/SY等待的合法ALU填槽。两FS最终native均把t6及第一次SY消费移到材质前；不插NOP、不删依赖、不改变读写数量。首个动态UBO仍排在材质后，完整关键链尚未全部提前。

两真实PSO验证普通UID、准确私有82c驱动、当前cf83原shader、feature及native身份。相比v2，v3未增加非NOP、mov、寄存器或代码尺寸；相比原12b，前级策略仍有+19/+9非NOP与部分mov、代码体积成本，两FS均19full/10waves。静态systall、打印行号都不是硬件耗时。源码与可复现入口见[独立驱动实验](../tools/experiments/turnip-memory-critical-schedule/README.md)。

## 真实整帧合同

证据目录：`.local/logs/fs-critical-schedule-game-device-v1/`。仅`BASE-G3`和`SCHED-G1`为有效窗口；早先BASE-G1被遗留19项校验在工具采集前拒绝，BASE-G2因缺少reset label而未重置受控seed，被启动前门控拒绝。两次失败完整保留，不纳入结果。

| 项目 | 两臂共同值或差量 |
| --- | --- |
| APK / host | 同一`ed1d4915…` / `42e58833…`正常cap60 carrier；相对原cap60只增加两处准确provider准入 |
| shader | cf83dfece8e7原字节；没有混入one-word、readonly-route、SGSR |
| 驱动 | BASE `12b8b54a…`；候选 `82cbd84e…`，完整18218项源ledger差量仅3个IR3文件 |
| 图像/共享资源 | 同BC3包f7b381…、producer919454…、443588608字节payload；未重新编码/烘焙 |
| consumer表 | 12b表689010…；82c表1595e4…；仅严格驱动身份/表hash字段不同，内容区相同；原parser及live probes保留 |
| 实际输出 / 主3D | 游戏和交换链1968×900；主3D1673×765、固定0.85 |
| 节奏 | 显式原进程cap60；同窗原状态记录pace60、period16666666ns；不宣称已测物理面板即时刷新率 |
| 场景 | 同受控22文件seed，营地5225/6289、等级3、经验2920、金币51 |
| 运行选项 | inline CPU7、短等待30us、stream OFF、Store原默认ON、prefix原默认OFF、pixel-local/bloom-quarter/交换链UBWC ON |
| 工具 | 同一官方SDP会话22项global指标＋原Perfetto8s；无新计时器/采样器 |

新driver先以原已源码绑定的NVN53/54及919 producer验证：普通UID10371，1234数据检查、150小BC3 host-copy检查、8组零差异。仅重打包两driver assets，127个非签名entry中125个其余entry相同。这不是全资源一致性/全场景验收，也不是82f特有vec4路线验证。正式游戏两窗另通过实际shared/lazy/native表、原显示/场景及稳态门控。新表没有借旧driver身份，也没有重新发443MB包或整套shader。

## 整轮指标，而非只看一个计数

以下均为原工具有效窗口，MB按十进制；rate以同窗完整Surface归约，百分比仅按时间加权。全局GPU计数包含合成器。

| 指标 | BASE | 82c候选 |
| --- | ---: | ---: |
| Surface FPS | 50.37461 | 50.13900 |
| main SOP/EOP，ms | 19.65405 | 19.73413 |
| present-copy SOP/EOP，ms | 0.18793 | 0.18994 |
| 读，MB/Surface | 105.41593 | 104.97654 |
| 其中texture读，MB/Surface | 86.03826 | 85.71916 |
| 写，MB/Surface | 44.19908 | 44.18911 |
| GPU平均运行MHz | 499.568 | 499.626 |
| Texture Fetch Stall，% | 12.23549 | 11.80529 |
| Shaders Stalled，% | 16.65372 | 16.87741 |
| 系统内存stall，% | 0.40890 | 0.37359 |
| L1 hit / L2 texture miss，% | 63.69234 / 4.37285 | 63.73387 / 4.36056 |
| ALU capacity，% | 33.86864 | 33.81003 |
| Time ALUs / EFUs Working，% | 39.37328 / 13.61043 | 39.24293 / 13.56242 |
| EFU issued，百万/Surface | 84.70302 | 84.72537 |
| FS / VS / CS在shading中的时间占比，% | 81.0963 / 13.2184 / 5.6853 | 81.1338 / 13.2107 / 5.6555 |
| owner Running，ms/Surface | 16.37655 | 16.56225 |
| owner S，ms/Surface | 3.14482 | 3.04403 |
| owner Runnable，ms/Surface | 0.20529 | 0.20776 |
| owner执行时平均MHz | 2063.80 | 2073.82 |
| 全app Running，ms/Surface | 30.73304 | 30.89154 |
| Present wall / S，ms/Surface | 3.37249 / 2.91129 | 3.25102 / 2.80615 |
| submit锁 / present锁，ms/Surface | 0.001645 / 0.000813 | 0.001695 / 0.000737 |

实际提交线程识别了owner和RenderThread两个app context，再由原queued事件与API区间匹配main/copy；没有从最大批次猜角色。已知内部区间的app SOP/EOP跨度并集占比99.8544%→99.8635%，含内部stall，**不是ALU忙碌率**；两窗头部约28ms硬件区间未知仍保留。owner的S/D约99.8%与本app GPU区间重叠，因此不能把约3ms睡眠直接当作可独立删除的帧长。主批次inflight中位3、copy中位4未变，没有看到这轮新增长队列/锁等待或CPU降频。

EFU三项是本次补齐的真实当前12b数据，不再拿旧system-driver的11%套用；Time EFU/ALU以shaders busy为条件，不能相加或乘19.65ms制造独占单元时间。它们没有给出“当前EFU全局吞吐饱和”的证据，也不能排除某条依赖链的EFU延迟。GPU bus约24%、L2 system stall约0.4%也不能直接等同整个SoC DRAM是否饱和。

资格界限：两窗口各自PSO稳态，但累计pipeline181对179、variants均32/pending0；约9.24s原资源快照的texture prepares/guest及views约有4.7%差异，不与约8s完整Surface分母混用。不能把owner增加0.186ms全部因果归为新driver成本，也不将单pair的−0.47%宣称稳定回归。已有GPU数据仍明确不支持“大幅提速只是被CPU隐藏”。

## 判断与下一步

调度候选改变了首SY组边界，却没省掉地址链和实际load；整体读写几乎没变，部分独立材质采样的提前重叠还可能减少。这解释了为什么“指令先发了”不自动成为整帧收益；具体相抵比例尚未独立量化。候选完整保留供后续，不凭单次无涨帧删除源码，也不自动默认启用。

优先进一步核查**小只读UBO表的有界提前读取**：若能证明只访问少量固定vec4、合法资源范围与原支配/越界语义，可以把读取启动从t6结果解耦。当前已知它是实际guest FS UBO10，不是embedded c1，也不能借host c0/c2的dspan证明。必须先做精确FP32/整数域、descriptor/read-only/robust资格与新增寄存器/读取预算，再看实际native是否真的提前并保持合适occupancy。它与仅继续填地址ALU间隔的较小调度候选分开研究，不增加每draw整资源扫描来交换局部收益。

分析及原数据分别位于该目录`gpu-tools/comparison.json`、`whole-profile/REPORT.zh-CN.md`、`hardware-analysis/`。完成后正常c09 APK、12b驱动、cf83链和本次入口22用户文件已恢复，用户存档未冷启动、偏好未变。官方cleanup保留一次进程退出/身份查询竞态失败；独立`final-restoration.json`实查GUI/Xvfb/13采集器全部退出，未把原失败重标成功。
