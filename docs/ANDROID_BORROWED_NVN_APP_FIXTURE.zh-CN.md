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

## 2026-10-20 Mali 独立 UID 实际结果

冻结 `603e3e6` APK `36ca50d9…` 与固定 PanVK `bdd9df32…` 只执行一次，通过 **10,867** 项原断言。设备为 Mali-G710 MC10，普通 UID **10280**，driverID20/API1.4.363，实际导入类型 **0xb（HOST_CACHED、非 coherent）**，atom64；并非用宿主 coherent 结果替代。

- UBO前三轮合法 CPU 改写后保持同一导入，GPU 像素与 live CPU 一致；第四轮原 writer 退回镜像，并验证恢复后的 shadow。前三轮不声称 shadow 已更新。
- 原 BindVertexBuffers + position shader 三轮覆盖 full/clipped/full，保持同一导入和代数。
- 记录九次真实 Flush：六次64KiB、三次4KiB；与原提交边界一致，不增加全局 HOST barrier 或 GPU wait。
- closing、Finalize/ABA、late image、初始受保护页拒绝和严格 dedicated 负例保留原断言并通过。严格 dedicated 是注入的更保守拒绝，不表示合规 HOST_ALLOCATION 驱动会实际要求 DEDICATED_ONLY。

测试包已停止并卸载，唯一 tmp 已清理；原生产 APK `707f4a8d…`、28模块与 `cbd8d2da0b0f` 链前后完全一致，原游戏未运行。详细归档在 `.local/logs/panvk-borrowed-nvn-app-fixture-v2-actual-validation.json`；首次 source-ready 和旧 mprotect/COW 失败记录未改。Mali 租约已归还。

这只闭合完整64KiB borrowed Pool 的有限功能路径；尚未运行真实游戏、生产大 Pool 内部窗口或任意 alias/VMA，也未测量 FPS、功耗或带宽收益。VVL 显式关闭。主线与默认驱动未启用本实验。
