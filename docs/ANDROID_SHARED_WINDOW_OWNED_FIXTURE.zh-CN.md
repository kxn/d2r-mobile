# 普通 Android UID 共享窗口验收入口（实验）

这个独立 APK 只提供原 `native_vulkan_shared_import_windows.inc` 的 Android 入口。
其生产窗口源码基线为 `eddb4ca`，原用例、SVC 和 NVN 命令不变；不执行游戏模块。
包名 `com.d2rmobile.sharedwindowfixture`，只有一个自有 Activity 和原六个宿主/加载器 SO。
不包含原 MainActivity DEX、游戏资源或游戏模块。默认生产构建不包含新增 JNI。

## 冷启动与验收

- 编译须同时显式 `--shared-pools --shared-import-windows --shared-window-app-fixture`。
- 自有 Activity 只接受自身 `files` 目录中的准备路径；JNI 单进程仅调用一次。
- JNI 在设备初始化前选原 adrenotools + 冻结 `3c8710be…` 驱动、async、64KiB 窗口。
- 强 fixture shim 调用原 `dm_guest_heap_startup_freeze`，随后原 fixture 通过真实
  `svcSetHeapSize(4MiB)` 建 memfd guest RO/RW alias，不用私有假 heap 证书。
- 原 cold overlap gate、真实 GPU 双向跨 handle 字节、append/union/observer、retile
  与 Buffer/Image copy 用例照常执行。原 ASTC4 physical copy 必须实际运行；不支持
  ASTC 的结果明确非通过。既有内部 CPU/GPU timestamp 禁用，不测速度/RSS/GUP。
- 记录实际普通 UID/PID、driver 加载路径/maps/SHA、device/driver/pipeline-cache UUID。
  pipeline-cache UUID 不是图片 optimal-layout UUID；本包不验证 native-image 协议。
- 只针对该编译组合与自有数据，不能代替真实游戏、Mali 或未来 native308 长期引用验收。
  非 owner observer 仍需外层维持 recording 状态稳定，本包没有扩展并发录制合同。

## 复现与事务

先 source `scripts/dev/env.sh`，按私有 Android NDK28.2 CMake 目录配置，再运行：

```sh
python3 scripts/dev/build-android-apk.py --tlsdesc --opt-level 3 --native-jobs 2 \
  --shared-pools --shared-import-windows --shared-window-app-fixture
python3 dm/host/android/tests/make_vulkan_fixture.py OWNED_SHADERS
python3 scripts/dev/build-android-shared-window-fixture.py \
  --base-apk .local/build/android-apk/d2r-native-tlsdesc-debug.apk \
  --output NEW_ARCHIVE --fixture OWNED_SHADERS --driver FROZEN_3C871_SO \
  --runtime-source-ready FROZEN_EDDB_SOURCE_READY
```

`scripts/dev/run-turnip-shared-window-fixture-v1.py` 默认仅离线验证并打印计划。
必须提供现有原游戏 APK/ELF、module report（或现有 baseline-readonly.json）和 shaderchain 精确哈希；只有 `--execute`
才可访问指定设备。独占 lease 仍由 root 分配。控制器复用旧 ordinary-UID fixture
的 installer/first-PID/cleanup 事务：原游戏必须停止，独立包必须不存在，不重试启动，
不改全局 layers/settings，不启动游戏。结束仅停止、卸载本包和删除自己的唯一 tmp，
复核原游戏 APK/模块/链逐哈希未变。终端 JNI 记录到来前不以中途 passed 行判成功。
失去 PID 所有权、原 assert、unsupported、超时均保留失败，不改 oracle。
