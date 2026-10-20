# Native image/window 普通 UID 夹具

本分支基于冻结 `bb4ac548`，只增加默认关闭的 JNI/Activity/CMake 与打包控制器；原 allocation lane、window、family/ASTC 测试本体不改。

冷 JNI 在首次设备/地址空间初始化前选择 AA185、TU host import、64KiB window、真实 memfd startup shim、精确资源 table/pack/producer。它只调用既有 `dm_vulkan_test_main` 的 native resource 分支，不调用游戏 JNI，不加载 NSO。原 native family 本体执行真实 firepit ASTC4、9 mips、5 tail levels、packaged/linear B2T、T2T、双向线性别名和两次 window union，最后 scope/原映射/独立 native allocation 退休检查。

资源固定为先前 actual AA185 成功的135168B native payload，不重新烘焙，不复用 PanVK90112B。独立包 `com.d2rmobile.nativewindowfamilyfixture` 只装新 Activity DEX、canonical 原样六库和许可证，不包含原 app 资产/DEX 或游戏模块。生产包从不安装/启动/写入；控制器只读原 APK/ELF/29 modules/UI4/950/旧 driver 的 before/after hashes。

控制器继承已通过 v3 的双私有路径 dev/inode/size/SHA、同 first PID/普通 UID、maps 前置留证、无重试和精确包清理。它要求真实 ASTC 与共享 memfd、原所有功能通过行、独立135168B import、120B token、two unions、最后回收和资源未变。暂时仅离线准备，不是实际 Android GPU 验收，也不声称性能收益。设备由 root 后续单独分配。
