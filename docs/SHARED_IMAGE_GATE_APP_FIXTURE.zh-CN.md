# 共享图像 NC prewrite gate 独立 APP 编排

本层基于 b730ff4 + host lease 162178b，仅添加 Android 冷测试入口和独立包事务；不修改 dirty/Vulkan gate 或 GPU oracle。默认无入口，不启动游戏或 guest 模块。新 gate 停源后由独立组合构建 APK，本层源码通过不表示 Mali NC 或实际游戏通过。

Intent `shared_image_test` 仅接受 `nc-gate` / `retile`，同时必须显式 `run_game=false`、`run_vulkan_tests=false`、`validate=false`、`vulkan_test_validation=false`、`vulkan_provider=panvk` 和 `async_submit=true`。直接建立 runtime 路径/日志、选择原 app-local ICD，启动一次原 `dm_vulkan_test_main` 的专用分支，不创建 Surface；Activity recreation / onNewIntent 不可重新选模式。独立日志 `shared-image-test.log` 使用已验证的同文件 append 输出。普通 nativeTests 和游戏启动路径保留。

JNI 在初始化 Vulkan 前固定 whole/window0、mirror0、defer0 和共享模式；只测试自有有限 MAP_SHARED 资源，不切真实 guest heap。`nc-gate` 设置 `DM_SHARED_IMAGE_NC_GATE_FIXTURE=1`；`retile` 设置原 `DM_SHARED_IMAGE_DIRTY_FIXTURE=1` + `DM_SHARED_IMAGE_COPY_TEST=1`。硬件 NC oracle 对 CPU device / coherent / 不支持条件返回 unsupported，不能算通过，也不靠线程名豁免软件 GPU 的 mprotect 行为。正常 LVP/coherent GPU回归与真实 NC 同 atom oracle 是不同证据。

组合后先使用原入口在本树私有 `.local/build` 生成共享编译 APK，再包装：

```
python3 scripts/dev/build-android-shared-image-gate-fixture.py \
  --base-apk .local/build/android-apk/d2r-native-tlsdesc-debug.apk \
  --fixture /ABS/owned-vulkan-fixture \
  --gate-source-ready /ABS/final-gate-source-ready.json \
  --gate-source-revision FINAL_40_HEX_REV \
  --output /ABS/new-independent-artifact
```

包装器核原 APK source_inputs 全哈希、六库相同、gate revision 在当前祖先，再仅改 manifest 包名为 `com.d2rmobile.fixedsharedfixture` 并重签。保留 class package/JNI 名与六库完整字节，只明确移除不可组合的可选 VVL。owned fixture 文件全清单及 gate 报告哈希一并绑定。

```
python3 scripts/dev/run-panvk-shared-image-gate-fixture.py \
  --serial DEVICE --artifact /ABS/new-independent-artifact \
  --driver /ABS/frozen-bdd9/libvulkan_panfrost.so --mode nc-gate \
  --expected-original-apk APK_SHA --expected-original-elf ELF_SHA \
  --module-report /ABS/current-28-modules.json \
  --resource-report /ABS/current-6-resources.json \
  --expected-shaderchain CHAIN --output /ABS/new-result
```

默认仅本地计划，无 ADB。获授权后相同命令加 `--execute`；每次仅一模式/一次冷进程，不重试。执行前原包停止、APK/ELF/28 modules/6 resources/chain 严格匹配；不写原包。只安装不存在的独立包，核包哈希，唯一 tmp；不改系统 GPU 设置。记录原始 PID 文本、先保存 FATAL，再解释 crash fork/替换；歧义的 final pass 不接受，不认领替换。未知进程只有限等自然退出，不能强停。成功需 oracle + wrapper terminal、普通 UID、PanVK driverID20、显式 validation off；NC 还需 flags11/atom64/原 GPU 与 CPU word oracle。finally 停本次 owned 进程和工具、卸载精确包、清自己 tmp，再只读核原基线完整相等。没有真实游戏就无需制造存档。

本层离线测试只验证 exact controller helpers 和编排状态机；合成 ZIP 仅验证默认 plan 不执行 ADB，不是可安装 APK。JNI API29/O3 object 和 javac 通过不代表 gate 链接/设备通过；最终 APK/输入闭合必须在 gate 停源后另记录。不得与原支持的 LVP 矩阵混称真实 NC GPU 验证。

后续格式候选的 `retile` 验收额外要求恰好一条 `ASTC4-copy-physical-encoding` 记录：result=passed，actual_ASTC_GPU、fresh_BC3_array_became_ASTC4、logical_BC3_unchanged、mip_layer_ROI_preserved、default_and_BC3_srgb_views、CPU_version_observer、canonical_pool_bytes_unchanged 必须都是 JSON true。仅旧 retile 总 PASS、LVP unsupported、缺字段、数字1/字符串true或重复记录都拒绝。本条只收紧控制器的硬件验收，不修改 NC 原检查或 Java/JNI；核心未冻结/未构建前不能称新格式路径通过。
