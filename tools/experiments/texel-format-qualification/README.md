# Typed buffer / readonly SSBO 格式资格

这是独立、普通 Android UID 的 Vulkan **功能验证**程序，不加载游戏或主运行时，不修改游戏 Shader/默认配置，也不计时或采样性能。它同时从同一 Buffer 做 `RGBA32F` typed texel fetch 与 readonly `uint4` 读取，以整数精确比较 NaN payload、sNaN、signed zero、denormal 等位型。

GPU Profile 可以指出 fetch/LS 热点，但不能证明上述输入的格式语义一致；这是现成 Profile 工具无法替代的功能证据缺口。程序复用已执行过的自有 SFLOAT 探针、GPU 输入生成与整数归约，不另写分析器。`run.py` 仅编排 ADB 和整理程序现有 JSON；deadline 是退出控制，不是性能计时。

## 来源与装载

- `shaders/` 四个小 GLSL 是自有测试素材，不含游戏代码；`shader-origin.json` 固定源 SHA 和历史官方工具生成的 SPIR-V SHA。
- `source/probe.cpp` 沿原探针逻辑，仅增加进程内驱动装载、Volk 初始化与实际映射身份输出。`source/entry.cpp`、Activity 和 Manifest 负责独立冷入口；默认 `run_probe=false`。
- 直接引用仓库的 `nvn_vk_loader.c/.h`、Volk、adrenotools 头及冷 stderr 辅助，不复制这些已受版本管理的实现。所有实际源码和工具 SHA 进入新 `ready.json`。
- adrenotools 要求真实 APK 的 `nativeLibraryDir`，因此使用单独包名 `com.d2rmobile.texelformatprobe`，复用已验证 APK 的五个原 loader hooks。包内不包含 `libdmhost_android.so`、driver、游戏模块或游戏资源。
- driver 作为独立二进制输入进入忽略的输出目录，再由控制器放入新包私有目录。必须核 SHA、普通 UID、实际 private Turnip 映射和 Vulkan context；无映射/stock fallback 均不能通过资格。

## 离线构建

需要仓库现有 Android SDK/NDK、JDK、`glslangValidator`、`spirv-val` 和 `spirv-dis`。路径从仓库根推导，可显式覆盖 SDK/NDK/工具版本；签名复用本机开发 keystore，不在此目录放密钥。

```sh
python3 tools/experiments/texel-format-qualification/build.py \
  --base-apk .local/build/internal-queue-present-v3/canonical.apk \
  --driver .local/build/turnip-internal-queue-sync-v1/mesa-kgsl-build/src/freedreno/vulkan/libvulkan_freedreno.so \
  --output .local/build/texel-format-qualification-archive-v1
```

`--base-apk` 必须有相邻的 `.apk.build.json`；APK 与五个 hooks 都核原 SHA。默认 driver SHA 是已测试的 `12b8b54a…559f5`；其他 driver 必须显式给 `--driver-sha256`，不会继承已有资格。标准 NDK 编译与 `javac/d8/aapt2/zipalign/apksigner` 包装全部记录；官方 SPIR-V 验证及固定 SPIR-V SHA 不符时构建拒绝，避免默默更换编译上下文。

输出目录必须不存在，且不得放到本源码目录内。新构建会生成自己的 `source_fingerprint`、APK/ELF/工具/依赖/Shader 哈希，状态始终先是 `HOST_BUILT_DEVICE_UNTESTED`。**不将历史 `d7c…`、013e APK 或历史通过结果写成新产物身份。** 此归档不是对旧冻结试验的回写。

## 运行

默认只输出计划，不使用 ADB：

```sh
python3 tools/experiments/texel-format-qualification/run.py PLAN \
  --ready .local/build/texel-format-qualification-archive-v1/ready.json
```

实际执行需要当前任务明确授予该设备队列，填入准确 serial 并加 `--execute`。首臂用 `--install`，只安装独立测试包；安装提示只处理该包，不更改系统保护。控制器发现主游戏 PID 时直接拒绝，不停止用户游戏。

```sh
python3 tools/experiments/texel-format-qualification/run.py NEGATIVE \
  --ready .local/build/texel-format-qualification-archive-v1/ready.json \
  --serial Q735_SERIAL --execute --install --conversion raw --inject-error

python3 tools/experiments/texel-format-qualification/run.py RAW-QUICK \
  --ready .local/build/texel-format-qualification-archive-v1/ready.json \
  --serial Q735_SERIAL --execute --conversion raw

python3 tools/experiments/texel-format-qualification/run.py QUIET-QUICK \
  --ready .local/build/texel-format-qualification-archive-v1/ready.json \
  --serial Q735_SERIAL --execute --conversion quiet
```

默认每通道仅 4,096 个输入，是 quick，不能冒充全域。负控必须真实产生 mismatch/FAIL/rc3，同时 source/coverage 错误为零；加载失败不算负控成功。raw/quiet 任意一边失败都保留首错，不改变 typed 参考或断言。

仅在负控、quick 和实际驱动准入通过后，为通过的模型显式运行一次 full：

```sh
python3 tools/experiments/texel-format-qualification/run.py RAW-FULL \
  --ready .local/build/texel-format-qualification-archive-v1/ready.json \
  --serial Q735_SERIAL --execute --conversion raw --full --chunk-pow 20 --timeout 1800
```

完整结果必须是 `full_domain_completed=true`、4,294,967,296 texel、17,179,869,184 component，mismatch/source/coverage 全零。四个不同双射各覆盖一个通道的全部 32 位输入，**不是穷举所有 RGBA128 组合或所有 Shader 上下文**。约 224 GiB 是算法逻辑工作量，不是测得的 DRAM 流量或耗时。

每次冷进程结束只停止本测试包，核退出后才允许下一臂；GPU 错误/超时保留原日志并停止一次，不反复重启设备。仅本包私有文件发生写入，不操作主 APK/profile/资源/存档，不启动 GUI/Profile 工具，不调频/温控。每臂原日志和收条写到 `.local/logs/texel-format-qualification/actual/LABEL/`；不得复用 label 覆盖证据。`COMPLETED` 表示程序完成，候选是否相等另看 `comparison_passed`，quiet FAIL 不会被写成 PASS。

## 2026-10-28 的历史实测

旧冻结 APK `013e3458…14259`、owned ELF `23632a5e…182a5b`，Q735 普通 UID 10334，**实际 12b8 Turnip**：

| 窗口 | 结果 |
|---|---|
| 注错负控 | 4,096/4,096 texel mismatch，预期 FAIL/rc3 |
| raw quick | 4,096 texel，全部零错 |
| quiet quick | 4,095 texel mismatch，typed 保留 sNaN 原位型 |
| raw full | 4×2³² 分量，mismatch/source/coverage 全零，PASS/rc0 |

四次实际映射与前后 driver SHA、UID/PID/context 均已核；采后测试和主游戏 PID 都为空，ADB 队列已归还。测试包仍安装但已 force-stop。没有主游戏运行或性能收益结论。

这些结果属于原冻结产物，详见本机证据：[结果报告](../../../.local/logs/texel-storage-current-turnip-v1/RESULT.zh-CN.txt)、[原始四臂汇总](../../../.local/logs/texel-storage-current-turnip-v1/actual-summary.json)、[完整结果收条](../../../.local/logs/texel-storage-current-turnip-v1/result-receipt.json)、[原始构建 ready](../../../.local/build/texel-storage-current-turnip-v1/ready.json)。`.local` 不随 Git 发布；`shader-origin.json` 保留这些证据的哈希，未复制 APK/driver/大日志。

## 仍未完成的生产资格

- 生产 `identity.tssbo` 使用 readonly **float4** `OpLoad`；quiet 模型才是 uint4 加 quiet 位再 bitcast。本程序比较 **uint4**，不能自动替代生产 float4 上下文。
- 原 `native_vulkan_texel_storage.inc` 的 marker53（格式回退、GPU producer/alias、bounds、descriptor retarget、base/spec 配对）和 `native_vulkan_texel_bits.inc` 的 marker54 严格位型对照，还需在当前 driver 的相应生产上下文执行。Android 原完整套件包含它们，但现有短入口 `DM_BINDING_CONSUMER_FIXTURE` 仅 host；此工具没有声称执行了这两函数。
- 当前两大 HDR FS 每个 16 个 Buffer fetch 中，各有 6 个到整数位观察的路径；当前实际格式/range/alignment 不能只借旧 GFXR 继承。运行时必须逐资源检查 RGBA32F、stride16、范围、偏移对齐和 descriptor 预算；不合格保留 typed fallback。
- 当前 29e 链无 `.tssbo`。游戏候选必须通过原 `dmsh_vulkan`/`DMSH_VK_TEXEL_STORAGE_ALTERNATIVES` 与明确 load model 重建配对资源，再由 `deploy-android-shaders.py` 核完整指纹/原子切换。本归档未改资源链或任何默认。
- SSBO 也可能被 Turnip 编译为 `isam.v`；接口替换不自然减少 16 次 fetch。新增地址/bounds/格式处理成本与真实 GPU 净收益须另测，不能从这次功能通过宣称提速。

## 离线检查

```sh
python3 -m unittest discover -s tools/experiments/texel-format-qualification -p 'test_*.py'
python3 -m py_compile tools/experiments/texel-format-qualification/build.py tools/experiments/texel-format-qualification/run.py
```

测试检查固定源、主包拒绝、plan 无 ADB、PID/UID/driver/源码身份、重复或缺失事件、输入与覆盖计数、负控和 full/quick 区分。它们只验证工具边界，不代替真机 GPU 结果。
