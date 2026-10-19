# Owned NVN 独立 AppUID 验证入口

此组合树基于 `7aa461c` 独立原型，仅复用已验证的 PanVK loader (`199fb43`)、显式 validation (`1a78a75`)、stdio append (`f0e3051`)、应用私有 Mesa cache (`dbde3f3`)。没有带入当前主线新游戏代码。CMake `DM_VULKAN_OWNED_NVN_FIXTURE` 仍默认关闭。

显式 ON 构建的 `owned_nvn_buffer_test` Intent 必须同时 `run_vulkan_tests=true,run_game=false`，仅触发无地址参数的冷 JNI setter；返回false则拒绝运行。setter 在测试启动前启用 PANVK_DM_HOST_IMPORT，原 native fixture 自行分配/持有64KiB，不接受 Java 传入的 guest/malloc 指针。保留原 Java/JNI 类名，aapt2 只将安装包改为 `com.d2rmobile.ownednvnfixture`，因此不覆盖 `com.d2rmobile.nativeport`。

功能 JSON 记录每轮像素/CPU 原断言已经通过、真实 flags、退回状态；非 coherent Flush 记录实际 atom 范围。它们只补充字节/范围功能证据，不是 profiler、采样器、计时器或性能数据。原2371项断言不放宽。

现成事务控制器只有 `--execute` 才访问设备：现场验证原0104包SHA、28模块、shader chain和PID空，建立before/after；仅安装一次新独立包，在其私有目录放入固定bdd9驱动和原GLSL测试素材，一次45秒功能启动。失败保留原日志，不自动重跑、不解保护重试。只停止/卸载精确SHA本次独立包，清除唯一tmp目录；不写原game/save/resources/settings，不安装系统驱动或停止服务。默认plan不调用ADB。原包已停时不为存档启动。

原型仅自有完整Pool，不能泛化成任意生产Pool、历史raw-image alias或迁移闭包；不得合入主线或默认启用。Android direct ICD显式关闭VVL，zero-error counter不当作VUID覆盖；实际type必须0xb且noncoherent才接收为本次Mali证据。未得到实际结果之前不得称成功，更不代表游戏FPS/带宽收益。
