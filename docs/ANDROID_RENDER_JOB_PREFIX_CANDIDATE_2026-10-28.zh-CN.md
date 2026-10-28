# Render Job 准备产物提前复用候选

本候选默认关闭，尚未 Android 游戏对照。它复用现有 `prepared_context`、
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
同新APK OFF/ON掩盖它们。Android正式整线程净指令/cycles和与已接受版本的
差异尚未测，不能声称已净省1.546M或已涨帧。

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

## 同APK真机入口（尚未执行）

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
核准并冻结。默认OFF、check OFF，无诊断wrapper；尚未安装或取得真机净收益。
下一步只做独立功能oracle和同APK OFF/ON官方工具对照，保持Shader/驱动/资源
不变，不与新Shader候选混测。
