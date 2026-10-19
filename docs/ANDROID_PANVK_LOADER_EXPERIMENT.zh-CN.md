# Android 应用内 PanVK ICD 实验入口

默认仍使用系统 Vulkan。开发启动器新增冷启动选项：

```sh
python scripts/dev/android-game-launch.py --serial SERIAL --vulkan-driver panvk
```

必须先由设备测试负责人验证并放入对应 Android/Bionic 驱动：
`files/runtime/vulkan-panfrost/libvulkan_panfrost.so`。这里只接受固定文件名的普通文件；
解析真实目录后加载，不接受指向别处的库文件符号链接，不根据哪个文件碰巧存在决定后端。
此入口不下载、安装或替换系统驱动。驱动包的输入、补丁、NDK 和 ELF 哈希由独立构建记录核对。

`--vulkan-driver system` 明确选择系统，`--vulkan-driver adreno` 使用原有
`files/runtime/vulkan-driver/libvulkan_freedreno.so` 与 AdrenoTools 路径。
原 `--custom-vulkan-driver` 仍等价于原 Adreno 选择；不传新选项时保留原默认行为。
选择不能与 `--attach` 混用，也不能在 Activity 重建或正在运行的设备上切换。
原生 Android 命令行可在进程启动前使用 `DM_VULKAN_DRIVER_KIND` 与
`DM_VULKAN_DRIVER_ROOT`；环境变量只在加载器首次初始化时读取。

PanVK 分支直接 `dlopen` ICD，再将 `vk_icdGetInstanceProcAddr`（或项目支持的
`vkGetInstanceProcAddr` 别名）交给 Volk，不调用 AdrenoTools。
加载器核对入口确实属于选中的库，持有句柄直到该进程的 Vulkan 生命周期结束。
失败直接报告原因，不回退系统驱动。创建实例后检查所需 Android Surface 调度入口，
枚举设备后核对 `VK_DRIVER_ID_MESA_PANVK` 并记录设备名、驱动名和 `driverInfo`。

直接 ICD 不负责系统 Vulkan loader 的 layer 组合；请求现有 validation layer 模式时
会明确失败，不能把它当作已经支持验证层或 RenderDoc 注入。系统和 Adreno 原路径保留。
后续如需 loader/layer 组合，应另行实现并用原工具验证，不用空入口或伪报成功补齐。

原应用内功能测试有独立的开发配置，默认仍请求 validation 和同步验证。
直接 ICD 功能验收须显式传入 `vulkan_test_validation=false`，例如由测试负责人冷启动：

```sh
adb -s SERIAL shell am start -W -n com.d2rmobile.nativeport/.MainActivity \
  --ez run_game false --ez run_vulkan_tests true --es vulkan_driver panvk \
  --ez vulkan_test_validation false
```

这时 `vulkan-test.log` 先写出 `validation_layer=disabled_explicitly` 的 JSON 配置记录，
保留原 byte/pixel 正确性断言；即使错误计数为零，也不表示做过 VUID/SYNC 验证。
此选项仅用于功能测试入口，不改变普通游戏的验证选项或默认驱动。

能够加载、具备函数入口和报告正确 driverID，只是初始化检查，不能证明 GPU 工作、
Android 出图、游戏画面、稳定性或性能已经通过。测试顺序仍应是应用 UID 下已有的
compute／离屏／AHB／WSI 测试，再运行真实游戏。此修改没有接入用户页导入，
没有打开 `VK_EXT_external_memory_host`，也没有改动 PanVK 的 CSF `ENOTSUP`。

`test_android_game_launch.py` 检查实际启动参数、互斥和接管限制；
`test_vulkan_loader.py` 使用不能执行 GPU 工作的宿主 DSO 检查后端选择、固定路径、
缺少入口、错误 driverID、失败不回退和初始化后不能切换。它们不计作真机结果。
