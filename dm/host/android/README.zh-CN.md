# Android 原生宿主

在原项目 `dm/host` 上补平台适配。真实游戏的 ARM64 指令在手机 CPU 上直接执行；
该目标没有 Unicorn、CPU 解释器、JIT 或模拟器系统服务依赖。
整体目标、缺口和阶段顺序见 [完整计划](../../../docs/ANDROID_NATIVE_PORT_PLAN.zh-CN.md)；约束见根目录 AGENTS.md。

## 构建和真机验证

仓库根目录执行，使用现有 `.local` 工具链与工作区：

```bash
bash scripts/dev/build-android-native.sh --rewrite
bash scripts/dev/test-android-native.sh
# 多设备时：
bash scripts/dev/test-android-native.sh DEVICE_SERIAL
```

需要 NDK r28c、CMake、Ninja、Python lz4/capstone，以及 clang/LLVM/LLD 工具。
目标为 Android ARM64 / API 29 起，使用 ELF TLS；真实游戏输入目录为
`$DM_ROOT/data/switch/3.3.94012/Update/{ExeFS,RomFS}`。
`--rewrite` 按源文件和改写器指纹更新产物，默认 Apple 改写模式保持不变。

产物：`.local/build/android-native/{dmhost_native,native_rule_test,native_fs_test,native_hid_test,native_touch_test}`。
部署仅使用手机的 `/data/local/tmp/d2r-native/`，通过 ADB 运行，不需要 root 或 Switch 固件。
这里的命令测试专用命令行边界目标；完整 Vulkan APK 见 dm/android/README.zh-CN.md。首次部署会复制约 8.97 GB 的资源，后续同步跳过未变文件。

## 当前结果

2026-10-16，PHP110 / MT6983、Android API 36、4 KB 页真机通过：

- 直接复用原 `loader.c`、`hle.c`、`entry.S`、`thread.c`、`hle_svc.c`、`hle_thread.c` 等，
  装载三个真实游戏模块，执行原 SDK 初始化、模块构造并进入 nnMain。
- 原改写器的 ELF 模式通过 x18 虚拟化、原 SP 地址／写回、TLS、计时门和 HLE 强制接管；
  两线程 200,000 次原子递增及信号干扰通过。每个线程验证其 ELF TLS 偏移。
- 原存档／文件测试 68 项全部通过；原触屏测试 13 项全部通过；无图形 HID 测试 15 项全部通过。
- 游戏已执行至 NVN 的 nvnDeviceGetProcAddress 查询。此处显式退出 78，记录图形后端缺失。
  这项验收验证“到达图形入口”，不代表能显示游戏、建局或完整可玩。
- ELF LOAD 段为 16 KB 对齐；只链接 libm/libdl/libc。该手机的实际页为 4 KB，未测 16 KB 页手机。

机器可读报告：`.local/logs/android-native-result.json`。
每一阶段的完整输出在同目录 `android-native-*.log`；存档单测中出现的注入失败日志是测试内容，最终须为 0 失败。

## 代码边界

`platform_elf.c` 把原键页接到编译器管理的 ELF TLS，保留 Android 的宿主 x18；
`platform_fs.c` 提供 Linux 快照和原子交换，原存档事务仍在 `hle_fs.c`；
`headless.c` 提供无图形前端和 POSIX 信号诊断；`main.c` 沿原 `host/main.c` 的启动顺序引导。
`dmhost_native` 专门用 `graphics_boundary.c` 在原 nvnBootstrapLoader 接管点停止；`dmhost_vulkan` 和完整 APK 已用原 NVN 层下的 Vulkan 后端替代该边界。
未知强导入和 SVC 在此构建中直接报错；缺失弱符号按 ELF 语义解析为零。

完整 APK 已显示真实营地，接入原触屏手势和系统键盘，并通过保存重启读档、Surface 销毁重建。
实际击杀、物品交易/拖放、原技能绑定及原后台存档已有真机证据；35 个原功能模块已直接链接。
剩余为可选功能覆盖、性能、长期稳定性和发行；完整 APK 已支持无需 wrap.sh/预加载的原生 TLSDESC 装载。
详见完整计划及 dm/android/README.zh-CN.md；边界测试不能代替 APK 可玩验收。
摘要算法来自固定版本的 mbed TLS，来源与许可保留在 `dm/third_party/runtime-codecs/`。
