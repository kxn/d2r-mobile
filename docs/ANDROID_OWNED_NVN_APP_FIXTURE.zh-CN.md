# Owned NVN 独立 AppUID 验证入口

此组合树基于 `7aa461c` 独立原型，仅复用已验证的 PanVK loader (`199fb43`)、显式 validation (`1a78a75`)、stdio append (`f0e3051`)、应用私有 Mesa cache (`dbde3f3`)。没有带入当前主线新游戏代码。CMake `DM_VULKAN_OWNED_NVN_FIXTURE` 仍默认关闭。

显式 ON 构建的 `owned_nvn_buffer_test` Intent 必须同时 `run_vulkan_tests=true,run_game=false`，仅触发无地址参数的冷 JNI setter；返回false则拒绝运行。setter 在测试启动前启用 PANVK_DM_HOST_IMPORT，原 native fixture 自行分配/持有64KiB，不接受 Java 传入的 guest/malloc 指针。保留原 Java/JNI 类名，aapt2 只将安装包改为 `com.d2rmobile.ownednvnfixture`，因此不覆盖 `com.d2rmobile.nativeport`。

功能 JSON 记录每轮像素/CPU 原断言已经通过、真实 flags、退回状态；非 coherent Flush 记录实际 atom 范围。它们只补充字节/范围功能证据，不是 profiler、采样器、计时器或性能数据。原2371项断言不放宽。

现成事务控制器只有 `--execute` 才访问设备：现场验证原0104包SHA、28模块、shader chain和PID空，建立before/after；仅安装一次新独立包，在其私有目录放入固定bdd9驱动和原GLSL测试素材，一次45秒功能启动。失败保留原日志，不自动重跑、不解保护重试。只停止/卸载精确SHA本次独立包，清除唯一tmp目录；不写原game/save/resources/settings，不安装系统驱动或停止服务。默认plan不调用ADB。原包已停时不为存档启动。

原型仅自有完整Pool，不能泛化成任意生产Pool、历史raw-image alias或迁移闭包；不得合入主线或默认启用。Android direct ICD显式关闭VVL，zero-error counter不当作VUID覆盖；实际type必须0xb且noncoherent才接收为本次Mali证据。未得到实际结果之前不得称成功，更不代表游戏FPS/带宽收益。

## 已完成的独立实机功能验收（2026-10-20）

代码/包冻结 `c982002`，独立普通 AppUID10279、Mali-G710 MC10，固定 bdd9 PanVK app-local ICD（driverID20/api1.4.363）。一次运行原 NVN UBO→DrawArrays→真实像素路径2371 checks通过；实际 flags0xb、非一致缓存、atom64。前三轮复用同一导入/handle/代数并执行三次实际[0,65536) Flush；第四轮原 GPU writer 先退出导入再验证像素/liveCPU/shadow，初始PROT拒绝、保护恢复和后续 packaged image 退回也通过。Activity原启动请求优先，实际canonical window16MiB；所导入的独立完整Pool仍仅64KiB。

显式关闭Android VVL，不能把zero-error当VUID覆盖。原0104游戏包SHA、28模块、cbd8 shaderchain前后相同且PID空；独立测试包已停止/卸载，唯一tmp已删除，cleanup无错误。原game/save/resources/settings从未写入。原失败及之前standalone owned-page报告完整保留，不以新成功覆盖旧失败。

这证明现有手机内核能在普通app权限下配合该定制驱动导入明确自有页，并在排除保护→导入→合法CPU改写→Flush→真实Shader读取→退pin→恢复保护的受限合同下保持正确数据。没有证明任意游戏malloc/VMA、全生产alias/跨节点迁移或GPU共享写入；没有PFN/COW普遍性结论，也没有游戏零拷贝/FPS/带宽/发热收益。optimal VkImage/块线性转换仍是另一件事。此树继续独立，不合主线、不默认开启。

完整不可变证据：`.local/logs/panvk-owned-nvn-app-fixture-v1-actual-validation.json`（SHA6488f13f79488221621041bc8b4f71b5f4d10584f7d7ea68614869692e589c05），对应原始日志和before/after均在该报告中记录哈希。
