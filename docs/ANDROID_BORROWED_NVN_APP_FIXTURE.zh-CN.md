# 独立 Android UID 的 borrowed NVN 夹具

此树复用已经独审的 `687b156` 借用层，以及原 owned app fixture 经过真机验证的 explicit PanVK loader、单次 trunc 后双 append stdio、私有 Mesa cache 和官方 aapt2 独立包流程。新增代码只接测试冷 JNI、补 JSON 功能证据和包装控制器，不改借用状态机/字节断言。

- 安装包 `com.d2rmobile.borrowednvnfixture`；原 Java/JNI 类仍为 `com.d2rmobile.nativeport.MainActivity`，Manifest 必须是完整类名。
- 原宿主库和五个 loader 库共六个 native ELF 必须逐字节等于既有 APK builder 输出。只显式省去不能与 direct ICD 组合的可选 Android VVL layer。
- 显式 `run_vulkan_tests=true`、`borrowed_nvn_buffer_test=true`、`async_submit=true`、`vulkan_driver=panvk`、`run_game=false`、`vulkan_test_validation=false` 才进夹具。冷 JNI 不接收地址，不改 guest/allocator，只设置标准 driver 实验 opt-in 和原测试 selector，单进程重复选择拒绝。
- 私有加载已验证 PanVK driver SHA256 `bdd9df32c38f7c82fb58d815d6a021e8641a545f9fdb1b401416d6908d265d5d`，不写系统 driver。GLSL 使用原有 marker0/39/14/11 的17个文件，UID/flags/atom/UBO/VBO/closing/late-image/strict-negative 与实际 Flush 均记录为功能输出，不新增采样或计时。
- 控制器默认 `--plan`（未指定 --execute），不访问设备。未绑定当前 root 原 APK SHA、28模块部署报告、shaderchain 时只给未绑定计划，禁止执行；不能沿用旧原 APK 假定值。
- 执行前确认原游戏 PID 空、现场 APK/28模块/chain 与租约绑定一致；只安装新独立包和唯一 tmp。已有同名包一律拒绝替换；外部 GPU layer 指向本包也拒绝，不修改设置。
- 一次 launch、45秒功能上限，安装70秒上限。原断言失败即保留，不重试、不解保护再 GUP、不 root/thermal/sysfs。清理只涉及本次精确 APK 的新包/进程/tmp 与本次 host ADB client；再次读原 baseline 确认不变。无法确认清理时报告错误，不冒称恢复完成。
- 验收预期是普通 appUID、PanVK driverID20、实际 flags0xb 非 coherent、atom64；UBO四轮、VBO全/空/全、closing、初始PROT、writer/image退回和两类严格拒绝全部通过，实际Flush含64KiB与4KiB范围。VVL显式关闭，零错误计数不能说零VUID。

这仍只证明**一个完整64KiB借用 Pool**，不证明生产大池内部窗口或真实游戏省镜像/带宽/FPS。源就绪/安装计划不等于真机成功。原主 APK、28模块、资源、存档和系统服务从始至终没有写入口。
