# Host import 标准 Vulkan 功能验证

现有 `native_vulkan_test` / Android `nativeTests` 增加独立开发模式。
只使用夹具自己 `mmap(NULL, ...)` 的有限 anonymous 页，不涉及游戏堆、
固定地址空间、`mprotect`、NVN pool 或共享内存集成。它在 NVN device
初始化之前返回，不加载游戏模块，也不采样、计时或声称性能收益。

本机仅找到 CTS 的 `vktApiExternalMemoryTests.cpp` 源文件，未找到可直接
执行的 `deqp-vk` / CTS 工具；该源文件依赖 CTS 框架，不是独立程序。
因此补充此最小字节 oracle，继续由官方 Vulkan Validation Layers
及同步验证检查 API/同步用法。

验证使用标准 `VK_EXT_external_memory_host`，不调用 KGSL 私有接口：

- 四轮独立导入和释放；每轮三次在导入后改写 CPU 内容，再经 GPU Copy
  读到独立 readback Buffer 比较，避免只证明导入时的一次性 cache flush。
- GPU FillBuffer、HOST_READ barrier 和 Fence 后，直接检查原 CPU 指针。
- 每对提交先等待未满足的标准 Timeline Semaphore，两个 QueueSubmit 都
  返回后才由 CPU signal，确保同时存在两次待完成提交。
- Map/Unmap 后及 FreeMemory 后继续读写原 CPU mapping；最后只释放自身映射。
- 对 Turnip 检查 Host Buffer 不可导出、sparse/foreign 查询不支持及 Image
  查询返回不支持。避免在启用验证时故意发出无效 Vulkan 调用。

每轮导入两倍 `minImportedHostPointerAlignment`，自身 mmap 三倍大小以
满足地址对齐，不使用 MAP_FIXED。对齐超过 64 KiB 或没有所需标准
Timeline Semaphore 时，报告明确的 `unsupported`，不冒充通过。
这不证明所有错误分支或物理页绝无泄漏，仍需后续实际驱动验收。

Android 使用现有 Activity / app UID 入口，必须冷启动。以下仅是执行
说明；源码准备阶段不执行设备命令。原私有驱动部署流程保持不变。

```sh
adb -s SERIAL shell am force-stop com.d2rmobile.nativeport
adb -s SERIAL shell am start -n com.d2rmobile.nativeport/.MainActivity \
  --ez run_game false --ez run_vulkan_tests true \
  --es vulkan_driver adreno \
  --ez host_import_test true --ez host_import_opt_in true
```

`host_import_opt_in` 默认 true，仅在 `host_import_test=true` 时设置冷启动
`TU_DM_HOST_IMPORT`。设为 false 可检查默认关闭路径。它不直接认定驱动
支持；驱动仍须完成自己的真实 KMD capability probe。常规游戏不会
默认进入此模式。日志为 app 文件目录下 `vulkan-test.log`。

`vulkan_test_validation` 默认 true；如果原工具层在该驱动确实不可用，
可以明确设 false，保留全部字节断言，但此结果不算 VUID/SYNC 验证。

CLI 同样可用，调用系统/既有指定 loader，不代表 app UID 已验证：

```sh
DM_VULKAN_HOST_IMPORT_TEST=1 TU_DM_HOST_IMPORT=true \
DM_VULKAN_VALIDATE=1 DM_VULKAN_SYNC_VALIDATE=1 native_vulkan_test
```

结果 JSON 区分 `passed` 与 `unsupported`，记录实际 `getuid()`、对齐、
导入字节量、timeline gate 和验证请求状态。Android 应与系统报告的
包 UID 及所加载 driver/ELF 指纹核对。Linux Lavapipe 或 shell UID 的
通过不能代替普通 app UID 的真实驱动结果，更不代表游戏共享池或帧率
取得了进展。采集完成后由协调者关闭本次测试应用/工具。
