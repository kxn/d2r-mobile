# 逐 draw 准备：重复证据与 CPU 预算

有重复输入与具体重复重建的证据；目前没有“整段相同准备每 draw 重做，占整线程约 20%”的证据。此前把高频资源消费笼统称为重做，范围过宽。应分别说明职责耗时、重复次数和实际可省成本。

## 当前时间样本

本轮改动前的 b8 APK、host BuildID `d34eeef5781b67efc32d04f8d04b7491090074a8`，三次官方 Simpleperf 30 秒 `cpu-clock:u` / 99Hz / FP，NvN 用户态样本为 2187、2190、2167。分母均为完整翻译线程用户态样本；表中范围为三次观测范围，不是置信区间。

|职责|CPU 样本份额|能否当作重复成本|
|---|---:|---|
|Buffer Pool / dependency / plan|9.22–9.56%|不能；包含已覆盖早退、范围／阶段账本、UBO 计划与保活|
|图像 prepare / View 合计|8.60–9.54%|不能；下面两项已包含其中|
|其中 per-image prepare|1.34–1.65%|含早退／资格判断，不是全在慢重建|
|其中 View consumer|6.04–6.89%|含已有视图查找、元数据、layout／feedback、发布、保活及回调|
|Store collection / logical resolution|3.11–3.43%|含必要读取事件、别名与冷查询；已包含在 planning 父项|

例如原记录每 guest 约 4186 次 texture prepares 是**提前命中**，约 6154 次 Pool ranges 是**覆盖后早退**；不能反过来称为待消除的慢准备。已有 UBO plan、普通图像组、attachment job、raster run、state segments 都已开启，预算必须扣除这些已完成的优化。

这些调用栈有真实时间采样，但不含每次调用的完整资源身份、参数和实际命中结果。独立 API 捕获能给对象变化，不能将它的稳定比例乘另一窗口的 CPU 份额生成可省毫秒。

## 已证明的具体重复

官方 GFXReconstruct 连续六帧 1670–1675 共 14340 draw。其中同一物理 Buffer、RGBA32F、offset 176832、range 1048576 对应的 texel BufferView 每帧仍新建两次，底层 Buffer 未销毁。原因是原缓存随 submission 回收，而非随底层 Buffer 存活。这是完整 key 相同的重复重建，已实施 owner 生命周期复用候选；它不是普通图像全部约 9% 的成本。

四臂真实游戏对照已完成，原 f830 驱动、950 shader、6217 native 纹理、固定 .60、2750×1260 输出及原游戏模块相同。只替换两个 canonical APK；新版运行源为 `a61f757d`，host BuildID `4efbfc963887f7445541d0b3567398aaa33b13d0`。使用原 Simpleperf PMU group-read 与 Perfetto 工具，工作计数来自已有发布字段，不新增计时器或抓帧。

|按执行顺序|原 guest submit 数|texel view 创建增量|每 guest 创建数|NvN Running ms/Surface|Surface 平均 ms|
|---|---:|---:|---:|---:|---:|
|A1 旧版|498|996|2|26.832|33.034|
|B1 新版|500|0|0|26.882|32.986|
|B2 新版|466|0|0|26.540|33.079|
|A2 旧版|499|998|2|26.049|33.127|

两个对照均证实持续创建消除，**没有证明整线程 Running 或帧率提升**。计数端点比工具窗宽，表中原 guest 数不作为内层 Surface 的分母。A1、B2 variants 30→32，B1 30→30，A2 32→32；CPU 频率及驻核也有变化。B1 全窗 PMU 缺 CPU7 边界（99.176% 覆盖），A2 缺 CPU2 极小边界（99.995900%），两者都不能将 covered-core 上界称为完整线程上界；机械可观察裁剪另列，不挑有利窗口。descriptor 分配和 history miss 也未证明稳定净减少，因此不把视图身份稳定夸大为整条 descriptor 消费跳过。

实现新增每 Buffer 8 字节 owner 指针、使用时懒分配缓存、每 owner 最多 64 项，超限仍走原 submission 缓存；在底层 Buffer 原有安全销毁点销毁视图，不删除 Pool／发布／访问／保活责任。8 项 host 生命周期／真实 GPU 资格用例通过，Android 真实游戏两轮正常运行及暖机画面检查通过，已合入主线，保留这一有界省工。当前没有达到 16.67ms。四轮均由原 Android 生命周期完成新存档两次并停止进程；最终装回新版，未让游戏空跑，也未访问已暂停的 Mali／6GB 平板。

## 优先级依据

1. 已证完整语义 key 重复且生命周期可闭合的小修，完成真实游戏资格与自身省工核验；不把小频度工作包装成大预算。
2. 更大的结构研究是固定资源／几何编码与变化参数分离。六帧各 641 draw 的两个组中，仅允许 UBO 变化时，深度组 632、HDR 组 420 个 draw 的其他绑定／状态均稳定。但 UBO 有效绑定确实变化，原 secondary 也不可直接跨帧复用；还需逐消费者参数版本、依赖、查询和寿命合同。
3. 对 View／Buffer 的剩余职责，先证明哪个整段能取消，再据其调用链预算立项。当前 descriptor materialize 加 driver bind 的直接父预算约 4.44–4.89%，仍包含必要工作，不是保证收益。不能因为资源不变就跳过当前 GPU 访问、CPU 发布或保活。
4. 不再重复实现已有 Store 槽缓存，也不因调用达到万次就增加另一层检查。当前没有已证明可直接删除、足以填补十多毫秒差额的单一阶段。

底稿及逐样本输入封存于 `.local/logs/current-preparation-redundancy-budget-v1/`；完整 CPU 原记录在 `.local/logs/current-default-on-callchain-v1/`，六帧结构在 `.local/logs/current-cross-frame-command-reuse-v1/`，两 APK 真机对照在 `.local/logs/persistent-texel-buffer-views-device-v1/`。仅整理现有工具结果，没有增加自定义计时或采样器。
