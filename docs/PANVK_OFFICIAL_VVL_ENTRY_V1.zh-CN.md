# 私有 PanVK 的官方 VVL 调试入口（默认不使用）

普通 system/adreno/panvk 入口保持原样。本分支只增加独立冷进程的 `vulkan_layer_probe`，明确 `run_game=false`、`run_vulkan_tests=false` 和 `vulkan_provider=panvk-layered-debug`。不创建 Surface，不装载游戏模块，不导入 guest/host 内存。不能把此功能夹具当游戏通过或性能结果。

Khronos Vulkan-Loader 固定 44b599a5c5fd3c29d86f1d05ffe9e72c65e5b743 的 README 明确不负责 Android，CMakeLists.txt 的 ANDROID 分支直接报错。原 direct ICD 的 GIPA 绕开 Android loader，原 loader.c 明确拒绝 validation；仅装层/设置系统开关不能证明该链被 VVL 包住。

本次优先复用原有 adrenotools `ADRENOTOOLS_DRIVER_CUSTOM`，取得隔离的系统 Android libvulkan，由系统 loader 创建官方层调用链；不写验证器，也不手写 VkLayerLink/dispatch callback。KGSL/GSL 导入属于另一个 feature，本入口不启用。此工具 README 名称和测试范围为 Adreno，不能依据源码便宣称 Mali namespace/HAL 已通过；若加载失败，保留具体失败，再评估标准 layer 接口胶水。

官方层是现有 Khronos Android 1.4.363.0 ARM64 二进制，其 ZIP 与已存官方 release digest 对照。层、原六库及所有 APK 输入都在独立构建清单中。原 fork c5ec 同时导出 HMI，系统 HAL 路线有源码入口；实际是否可用必须由普通 UID 验收。

adrenotools 的失败回退不得被接受：物理设备必须 driverID=20，并在本进程 maps 中找到与预先哈希固定的私有 libvulkan_panfrost.so dev/inode 相同的可执行映射。工具控制器核私有源字节为 c5ec，系统驱动或缺映射直接失败。句柄保留到进程结束。

夹具先复用原 NVN complete_pool_overwrite_test，验证真实 GPU Fill/Copy、旧读/部分写及下一提交 CPU 改写字节，要求正例零 VVL error。随后安装临时官方 debug messenger，调用 size=0 的 vkCreateBuffer：必须准确报 VUID-VkBufferCreateInfo-size-00912 一次并返回 VK_ERROR_VALIDATION_FAILED_EXT，buffer 仍空。该 messenger 返回 VK_TRUE 阻止故意错误下沉到驱动，未实现任何校验逻辑。不同 VUID、零错误、多个错误、正常字节不等或错误 ICD 全部失败。

离线 Linux/Lavapipe 只证明官方 VVL 负控/正常 GPU 用例及工具合同，不证明 Android namespace/private ICD。Android 验收要求真实 driverID20、private_ICD_verified、positive_GPU_bytes、positive_VUID_count=0、negative_count=1、invalid_call_blocked，和同 PID/UID 配置及 terminal 行。显式 unsupported 不能算通过。

控制器改编既有严格独立包事务，默认仅输出计划，单次安装/单次冷启动，无重试。原生产身份由当前 promotion 报告参数化绑定：实际远端 APK SHA、与同 SHA 本地 APK 提取的 ELF、28 模块、6 资源、原私有驱动和 shader chain。原包只读。只停止精确首 PID；多 PID/替换先记录原文、等有限自然退出，不能接管。卸载仅新且精确哈希的独立包，删除仅唯一临时目录。当前生产 022d 可保护，不依赖工具源基线 36a 的旧生产哈希。

现状：原 fork/sharedOFF/原始 BC 运行曾在主菜单到选角转换时出现 VK_ERROR_DEVICE_LOST，尚未 HUD。它说明共享导入不是失败必要条件，不能证明纯驱动 bug；后端非法 Vulkan、shader/驱动编译或驱动故障仍需工具归因。本入口实际通过前不再盲改 CSF/BC/共享开关。
