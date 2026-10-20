# 原生 Android 应用中的可选 legacy gralloc 探测

完整 MineGraph `703db0ebb9afb2c97ae31f24dba5981c7c765fc1` Android 驱动的首次真实游戏启动，已在原022 APK、原模块、full ASTC4 资源下进入原 ARM64 nnMain；首 PID13500/普通 UID10174，随后 Android linker 拒绝 `libhardware.so`：`namespace clns-9` 不可见。尚未创建 Vulkan 实例或执行 GPU 工作。这是平台装载阻断，不能作为 BC/CSF 崩溃证据；原失败记录、6ac驱动与原全目录恢复证据保持不动。

本适配仅修改 `src/android_stub/meson.build`，把已经存在的 `hardware_stub.cpp` 从共享依赖改为静态链接。函数原字节不变，始终清空非空的输出指针并返回 `-ENOENT`。不复制私有系统库，不伪造 HAL 成功，不改 GPU 算法。

调用处支持该明确的 unsupported 结果：

- `u_gralloc_create(AUTO)` 依次尝试各 factory；CrOS、libdrm、QCOM 的 legacy HAL 查询失败均退出 factory，返回 NULL，继续下一项。
- `u_gralloc_fallback_create()` 查询失败仅发警告，仍注册原有 RGB 元数据路径；普通 RGB 的格式与 stride 来自 `AHardwareBuffer_describe` / `AHardwareBuffer_getNativeHandle`，保留原有可选 vendor mapper。
- YUV 查询在调用 `lock_ycbcr` 前检查缺少 module/callback，并返回 `-EINVAL`，不会空指针调用，也不声称支持该视频路径。

该改法对应 zenith 已有 Android015 平台处理，功能边界明确。新的 ELF 应移除 `libhardware.so` DT_NEEDED，保留三个标准 ICD 入口和 HMI；必须用实际 target 编译依赖及反汇编核验。构建通过不能代替应用装载、WSI、游戏画面或性能验收。

后续仍只做原受控 driver-only 游戏启动：不换 APK/模块/资源，固定 .60/2772×1240、shared OFF、legacy copy OFF，按新 driver 的真实 requirements 分配，driver/temp/cache 原目录 CAS 隔离恢复，原生命周期 fresh save2 后停止；已退出不重启存档。先审新 driver/工具 Ready，再执行新的一次必要验证，不重放旧 none capture。
