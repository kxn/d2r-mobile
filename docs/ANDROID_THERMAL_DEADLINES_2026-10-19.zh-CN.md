# Android 热保护截止时间修正

`ThermalPolicy` 只接收 Android 公有 thermal status，使用主线程 Handler 的
uptime 时钟，不读取温度、不轮询驱动、不修改游戏逻辑。原有限帧与原存档后台流程保留。

原实现每次等级变化都取消全部 Runnable，造成两处问题：

- 已排队的 CRITICAL、SEVERE 通知依次执行时，后者能取消前者新排的零延迟暂停。
- NONE、LIGHT 之间切换会重置部分恢复倒计时，尽管始终满足“LIGHT 或以下”。

现在按资格区间保存截止时间：

|条件|行为|
|---|---|
|CRITICAL 或更高|在当前 UI 回调直接请求原后台流程，每个前台会话只请求一次|
|持续 SEVERE|首次进入后45秒暂停；重复通知不重置，确实降到 SEVERE 以下才结束该区间|
|曾严重热压，连续 NONE/LIGHT|30秒后20→30；NONE/LIGHT区间内切换保留首次截止时间|
|连续 NONE|满60秒才释放限帧；LIGHT及以上中断该连续区间|
|未知 status|不作为降温证据；保持限帧，取消恢复资格，已开始的严重热压保护仍有效|
|onPause/onResume|撤销旧定时器；保留限帧，恢复前台后等待新公有状态建立新资格区间|

Activity重建时宿主可能仍保留旧限制，因此首次已知状态必须显式同步，包括NONE对应的0；
未知状态不清除旧限制，后续相同动作去重。

恢复30/60仍按原策略执行；没有新增默认限帧或画质档位。JNI、Vulkan上限与HLE动态3D接口未改。
`moveTaskToBack(true)` 仍通过 `onPause → nativeSuspend(true)` 执行原游戏存档和挂起。

测试无需 Android 设备或APK，使用确定性虚拟队列覆盖12组通知顺序、截止时间、未知状态、
旧回调与生命周期用例：

```sh
source scripts/dev/env.sh
mkdir -p .local/build/thermal-policy-tests-v1
javac --release 8 -d .local/build/thermal-policy-tests-v1 \
  dm/android/java/com/d2rmobile/nativeport/ThermalPolicy.java \
  dm/android/tests/com/d2rmobile/nativeport/ThermalPolicyTest.java
java -cp .local/build/thermal-policy-tests-v1 com.d2rmobile.nativeport.ThermalPolicyTest
```

该阶段还完成全部Android Java源码的API36类型编译，未构建APK、未操作ADB。五分钟已有测试的thermal原始日志丢失；限帧20有滞后保持，
不能据此反推持续SEVERE45秒，亦不能将该定时修正当作已经提升FPS或解释107ms长帧。

## 后续 Mali 真机集成验证

已随 APK `3a3d9884eb64ea6427d0442c22a3a161255a9dd4fd6d6749c9f8dd5569b2176a`
部署。原游戏营地跑动测试中，公有 API 在 05:01:47.669 报告 SEVERE，
05:02:32.671 触发原存档后台流程，连续区间 45.002 秒。原生命周期日志确认两次
存档提交、342.1ms；随后停止并确认游戏进程退出。该次没有观察到系统强杀。

该次从启动即启用原项目省电30帧，只是一次功耗对照。它仍然达到持续严重热压，
不能称为解决过热，也不能据此认定发热来自 CPU 或 GPU。测试后完整还原原始设置，
默认仍为上游60帧及原动态3D。启动时公有状态相同不能证明实际结温相同，单次结果
不支持比较两个设置的降温幅度。

证据：`.local/logs/thermal-eco30-v1-mali-integration.json`，公有状态连续日志为
`.local/logs/thermal-eco30-v1-mali/thermal-api-stream.txt`。没有通过 ADB 轮询温度。

## 原生音频与当前持续测试

完整 HW/Software Opus 构造入口接管后，默认原生音频版本的营地跑动持续了
273.998 秒、92 次移动，始终通过原输入在营地跑动（位置数见集成 JSON）。
06:16:03.395 的 SEVERE 连续至 06:16:48.396，45.001 秒后主动原存档并退到后台；
存档确认两次提交、301.4ms，测试控制器随后停止游戏。这次后台属于主动保护，
不能算作系统强杀或游戏崩溃。

原生解码累计 229,409 包，core/API/stack 错误均为零；但持续发热仍存在。
晚期官方 Perfetto 窗口中，翻译回放平均实际 CPU 时间 40.546ms，覆盖频率约
1227MHz；出图平均 50.324ms，最大 115.698ms。该窗口没有主 PSO 创建。
这不支持把这些晚期停顿全部归因于未做管线预热，也不能把音频局部 CPU 节省
等同于整机温度或功耗下降。已有热限制和原存档保护继续保留，需继续减少 CPU/GPU 工作。

证据：`.local/logs/thermal-sustained-v8-native-mali-integration.json`；仍仅使用设备内
公有 API 事件，没有 ADB 温度轮询、私有传感器读取或自定义性能计时。
