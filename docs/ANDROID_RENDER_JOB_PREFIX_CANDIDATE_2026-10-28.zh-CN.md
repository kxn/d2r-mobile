# Render Job 准备产物提前复用候选

本候选默认关闭，已完成一次 Android 功能 oracle 和同 APK OFF/ON 官方 PMU 对照。它复用现有 `prepared_context`、
`prepared_pipeline` 和活跃 Render Job，不新建每draw大key或资源表。目标是
让已有产物命中发生在最终DS/RT兼容/272B PSO context构造之前；GPU资源、
描述符、hazard、query及写历史继续正常处理。

来源是当前 exact26a1/source628 的一次完整、只读原生提交快照：2421 draw
中，任何RT/viewport setter均保守失效、再要求原pipeline epoch/plan角色/
拓扑不变，仍有2000次满足逻辑必要条件。它不是物理job命中率。原正式
1.546M I/Surface只是RT/area/DS/context的毛包络，含必要检查和冷draw，
不能按覆盖率相乘或当作已省指令。相关源码与原c09预算源逐SHA相同。

## 做了什么，保留什么

- 新增owner线程的render-input版本。任何SET_RT、viewport setter和
  `vk_state_restore`推进版本，不额外比较这些setter的新旧大结构。
- 在现有已准备context旁保存一枚小收条：job/recording/pass/输入版本、
  原plan DS need/writes、include-depth/stencil角色、area和primitive类别。
  命中后保留不变收条，避免每draw重写同一组字段。
- 命中还要求当前pipeline epoch、**program serial**、实际最终module/
  specialization值、dynamic uniform模式及拓扑一致。不能只看program指针。
- DS命中复制已有最终DS值，恢复其Vulkan sType，保持后续callback期间
  的值生命周期；省掉逐字段解码/构造和stencil写分类。随后在现有单draw
  RT证明中直接使用已证明结果。真正PSO命中时不初始化、填写或memcmp
  272B context；仍消费原pipeline/streams。
- 资源准备在准入之前照跑；图像依赖后、query之前再核job/command/
  recording/pass/representation和版本。丢资格仍走原abandon/cold retry。
  query之后若不再能复用context，正常构造原context，不额外重做query。
- 不跳过CPU source、Buffer/image准备、GPU依赖、query reset/report、
  DS写入累积、shader/writeback/history登记或任何draw。

初版排除ghost、pixel-local实际producer/consumer、mip clear pending、
instance expansion、可写资源、runtime stencil override及不保留原命令的
可选模式。pixel-local入口**前面那一次**target比较仍保留，不能把整个
RT预算都记作已省。readonly DS后续writer、resource promotion和subpass
不能因逻辑RT相同被跳过。

NVN QUADS与TRIANGLES可能映射同一个Vulkan TRIANGLE_LIST，但原draw_area
仅对NVN primitive4..6缩viewport。收条单独保留该类别，避免仅以VkTopology
复用错误area。新增真实三角形/四边形用例覆盖这个反例。

## 新增成本与对照边界

命中增加少量身份/版本和动态输入标量检查、DS值复制、最终callback后
身份复核以及一个owner本地工作计数；没有每draw大hash、资源遍历、时钟、
采样器或热点atomic。冷路写收条，热命中不重复写相同收条。

OFF也有新增成本：SET_RT/viewport/restore的版本维护调用、draw上的冷开关
分支，以及原DS构造提取为helper可能带来的编译布局/调用变化。原快照中
RT+viewport setter共154次，restore另计；不能说这些成本为0，也不能仅以
同新APK OFF/ON掩盖它们。同 APK 的正式整线程净指令/cycles 已测，结果及其归因边界见下节；与未改
parent 的完整新增维护成本仍未隔离，不能声称已净省1.546M或已稳定涨帧。

## 实际 host GPU 与平台检查

独立分支基于f57b3a8，Linux fixture调用原公开NVN encoder、实际生产后端
和Lavapipe，不执行游戏CPU代码，也不冒充Android里程碑。

| 验证 | 结果 |
|---|---|
|十种场景，OFF/ON × sync/async|四组通过；ON各100次实际context复用，102次原DS/target/area/context oracle，2次真正query或资源callback回退|
|GPU像素|四组及SAN两臂的十份像素文件逐字SHA相同|
|原readonly DS暖draw→后来writer→采样结果|OFF/ON × sync/async通过；ON各7次prefix复用/oracle|
|ASan+UBSan|同生产encoder十场景OFF/ON async两臂通过|
|失效破坏负控|漏viewport版本触发area oracle；漏DS restore版本触发DS oracle；漏RT版本且关闭独立pixel-local前置防线时触发target oracle|
|Android启动工具|37测试通过，包含OFF/ON/CHECK、attach拒绝及未启用候选的check拒绝|
|Java/JNI|Android36 SDK javac全源通过；NDK API29 JNI语法检查通过，未构建APK|

十场景包含直接state restore、viewport/RT setter、动态polygon offset、
真实query REPORT、相同VkTopology的QUADS/triangle切换，以及warm receipt
之后的真实资源callback结束pass。原query前缀、CALL/跨handle、promotion
和既有像素/Core+SYNC验证继续执行。

保留所有失败：首次复用已标external输出被原raster准入拒绝；原fixture
保留PSO全检查mode2使候选不适用而零命中；一次构建完成前调用了旧SHA；
负控输入没有改变有效DS或被另一防线挡住，均未当通过。第三RT破坏在
默认pixel-local下被独立target比较拦住，只有明确关闭该合法可选路径后
才验证新oracle确实拒绝。没有放宽原GPU/raster资格或伪造失败结果。

证据目录：`.local/logs/render-job-prefix-reuse-audit-v1/`。
`candidate-host/QUALIFICATION.json`绑定最终binary/源码及各组收条；
`candidate-host/final-v4/`为同一已冻结binary的13组完整通过结果；第三负控
明确pixel-local关闭。更早的`final-v3/`及独立第三负控验证保留，不改原失败。

## 同APK真机入口与已完成对照

```
# 普通OFF/ON对照都保持check关闭
python3 scripts/dev/android-game-launch.py [原共同参数] --no-render-job-prefix
python3 scripts/dev/android-game-launch.py [原共同参数] --render-job-prefix
# 单独功能检查，禁止作为性能窗
python3 scripts/dev/android-game-launch.py [原共同参数] --render-job-prefix --check-render-job-prefix
```

JNI/Java在native启动前冻结两个bool，默认OFF，热Intent移除且日志提示。
launcher核实实际`[vulkan render job prefix] enabled=N check=N`，不以Intent
传参代替后端生效；另记录首次实际复用marker。没有新增UI、改profile schema、
游戏binary或Shader。

主线`5444d97c`已由原Android入口完成一次构建，37项launcher检查通过；
候选归档`.local/build/render-job-prefix-candidate-v1/ready.json`绑定APK
`9ff6c2a7…`、host`a06a8f1b…`，1056个源码输入和六个native库均逐SHA
核准并冻结。默认OFF、check OFF，无诊断wrapper。随后已实际执行一次独立
ON-CHECK功能oracle和OFF-P1/ON-P1正式对照，保持Shader/驱动/资源不变，
未与新Shader候选混测。


## 正式 CPU 对照：净指令略减，replay Running 基本未变

证据：`.local/logs/render-job-prefix-device-v1/independent-whole-analysis-agent/`
的 `REPORT.zh-CN.md`、`comparison.json/md`、原同trace `phase-state.sql/csv`；
完整祖先/源行/样本数在同级 `pmu-attribution-agent/`。只使用官方 Simpleperf
累计计数、ReportLib/LLVM 和 Perfetto；没有自写计时器或新采样。

同9ff6/a06a、12b8、cf83、BC3 shared/native6217、营地和真实尺寸；Store域
两臂ON、功能CHECK两臂OFF，仅prefix OFF→ON。OFF/ON为999/1006个完整
Surface间隔，各自录制零loss/truncation，确切ELF/driver BuildID吻合。

| 每Surface | OFF-P1 | ON-P1 | 边界 |
|---|---:|---:|---|
|退休用户指令 M|71.21269–71.25966|70.58111–70.62970|净少0.5830–0.6786M，约0.82–0.95%|
|用户周期 M|30.70222–30.71781|30.65689–30.67430|仅少0.091–0.198%|
|owner Running ms|16.545945|16.483407|−0.062538|
|owner运行期MHz|2065.397|2079.810|+0.698%，不按MHz伪造指令|
|全应用Running和 ms|31.538817|31.344115|−0.194702|
|owner S+D / Runnable ms|3.083503 / 0.226159|2.991835 / 0.248972|睡眠减少、Runnable略增|
|工具窗FPS|50.18870|50.51617|单对+0.652%，不是普通运行稳定收益|
|p95 / max ms|22.156562 / 33.232917|22.155299 / 33.539063|p95近乎不变，max略增|

关键的互斥Running分拆：replay扣除planning/submit/present等嵌套后仅
**8.622378→8.618952ms（−0.003426）**；planning与finalization基本不变。
命名NVN外为6.601522→6.541686ms，少0.059836，解释了总Running减少的
大部分，仍混有guest/HLE/录制和未标记范围。**不能说本候选令翻译显著变快，
也不能把全部−0.6M指令独占归为target省工。** present中KGSL唤醒睡眠
2.798302→2.701909ms；这也不是本CPU准备候选已消掉GPU工作或队列锁。

新增管理确实可见。完整职责采样中显式prefix helper由0→15样本、约
0.338M I/Surface；draw其余由11.851→12.766M。叶PC/LLVM源切片显示旧
DS/RT/area/context包络约0.821→0.515M（36→23样本），新增helper和
入口guard/发布包络约0.021→0.467M（1→21样本）。这些切片嵌在父预算中，
不能相加；几十样本、源行调度和非直接改动职责的变化不足以精确判定局部
净成本。它们说明重复证明不是免费操作，不能把毛预算当作已省量。

工具before/after资源快照均实际存在且括窗，分别为21.6546s/1087guest、
21.4322s/1083guest；计数不能改用Surface分母。内部submit仍1/guest，旧pool
上传和回读字节仍0，Store indexed59/builds1且fallback增量0。views/prepares
+1.69/+1.49%，texture miss−11%，descriptor miss+4.20%，PSO lookup+0.94%，
所以不能假定其它资源工作完全相同。PSO181/variants32稳定；图像分配在各自
窗口内稳定，但OFF比ON多49152B，shared import驻留gauge也有小差，均保留。
当前game-state未发布prefix总hit计数，首次actual marker不等于覆盖率。

OFF原外层health因11条`systrace_parse_failure`拒绝；原failed receipt未改。
独立官方SQL/同版parser逐条证明11条全部来自非目标lmkd普通print（含两次
critical pressure），与错误数精确闭合；其它error/data_loss和8CPU丢失均0，
目标Surface连续、owner状态无内部缺口、已记录Running频率100%覆盖。根已
显式接受目标Surface/已记录调度频率/PMU范围，前缘69.126667ms仍unknown，
**真实内存压力条件不删除**。ON原health通过。详见同目录
`health-analysis-agent/OFF-P1/root-accepted-scope.json`，不是泛化放宽健康gate。

结论限于：同新APK总指令略减、周期微减且GPU数据缺失；保持机制候选，
不因未明显涨帧直接撤销。按本轮用户最新指示，停止继续优化guard/复用缓存
及其他CPU计算；仅收尾本次结果，工作重心全部转到GPU。CPU侧只保留锁竞争、
同步等待及其与降频的时序调查。长期视觉、散热、跨场景净益及相对未改parent的
完整共同维护成本仍未认证；本候选不作为GPU访存主线的替代方案。
