# Android 最终出图压缩与实际 GPU 写入（2026-10-27）

本轮按实际 GPU 写入优先，完成了 Q735 的输出缓冲 UBWC 分配实验及应用内发布。没有改变 2358×1080 日常输出、0.75 3D 比例、游戏模块、shader 链或纹理 payload。四轮同 APK 交错对照的平均写入 **61.686→51.061 MB/完整出图间隔，少 17.22%**；平均 FPS **38.695→39.974，约高 3.31%**。这是输出/合成路径上的实测节省，不能称作全部剩余性能差距已经解决。

## 定位与实现

使用已存在的 Mesa `TU_DEBUG=layout` 检查真实游戏，HDR、颜色和深度附件已为 UBWC，不能再笼统归因于所有 RT 未压缩。但在 native-buffer-geometry 日志与 WSI 创建日志之间，四张 2358×1080 交换链图像均为 `linear`，压缩元数据为零。

Mesa 通用 Android 用途查询未请求 Qualcomm 的 gralloc 私有 UBWC 位。最初只覆盖两个旧 GrallocUsage 回调，没有改变实际分配；随后核对手机系统 loader 与 [AOSP 交换链源码](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/vulkan/libvulkan/swapchain.cpp)，确认较新 Android 优先通过 AHB image-format-properties 查询获取用途，绕过旧回调。

独立驱动候选在该 AHB 专用分支及两个旧回调上保留原结果，为受支持的 RGBA8/BGRA8、最优布局 2D render/transfer 目标请求 vendor 位 28。有效 create flags、超出支持范围的用途、其他格式及旧接口的 shared/front-buffer 条件继续保守处理。实际图像布局仍由系统 allocator/mapper 返回，不编造 UBWC modifier、offset 或 pitch。

实现位于 `tools/experiments/turnip-swapchain-ubwc/`。构建入口校验最终 f830 源码全量冻结清单后复制到独立目录，复用原 Meson/Ninja/NDK 参数。原 userptr/coherent/native ASTC 源码保持原样。驱动选项 `TU_UBWC_SWAPCHAIN` 使用 Mesa 原 once 缓存，驱动默认关闭。

## 正确性与资源身份

独立 Android WSI 夹具执行实际 clear→线性滤波 blit→present；OFF5/ON6 均通过红绿蓝白截图检查及真正退至桌面、同 PID 返回重建。开启组的四张输出图像确为 UBWC，每张具有 53,248 字节元数据。夹具没有性能计时，不能用它报告游戏 FPS。

新驱动的 driver UUID 变化，layout/device UUID 不变。没有修改旧表摘要绕过认证：实际运行原生产工具重生成 6,217 条 ASTC4 和当前选择所需的 2,838 条 ASTC6 声明，然后按当前资源身份及顺序合成 3,379 ASTC4 + 2,838 ASTC6。当前混合资源包的 12,437 个文件中，全部纹理 payload/CFT 文件逐文件相同，仅 `native-layouts.bin`、`native-pack.json`、`replacements.tsv` 不同。宿主仅对精确的新驱动 SHA 保留原 coherent/lazy 资格，运行时原 GPU 字节一致性等探针仍执行。

第一次选错了更早的全 ASTC4 输入，真实游戏在入场前被原 native 认证拒绝；未产生性能窗口。该失败保留为 DAILY-A1，未改成成功或绕过检查。纠正为实际部署的混合资源后，以下四轮均进入原营地。

## 四轮官方计数器

统一条件：Q735、同一 APK `bebfb36`、同一新驱动/原生表、2358×1080 游戏及 View 输出、0.75 固定 3D 比例（主 3D 附件 1769×810）、原 blit、native-buffer-geometry 关闭、inline/CPU7、stream reference、prefer_sysmem、pixel-local 与炫光开启。每次独立冷启动，PSO 180/特化 32 稳定且 tick≥2500 后采集。独立观察窗口不完全相同，不能把单轮差异当作精确到小数点的普适提升。

使用原 Snapdragon Profiler 2026.9 九项计数器及原官方 Perfetto 8 秒窗口，按完整 Surface 间隔折算。MB 为十进制。计数器为全 GPU，包含合成器及 Android UI；纹理读取是总读取的子集，不相加。不是每个 draw 的时间、独占 NVN 成本，也不能据此证明 DRAM 饱和或长期降温。

| 顺序 | 输出压缩 | FPS | 总读 MB/间隔 | 总写 MB/间隔 |
|---|---|---:|---:|---:|
| DAILY-A2 | 关闭 | 38.734 | 125.437 | 61.760 |
| DAILY-B1 | 开启 | 40.199 | 114.317 | 50.891 |
| DAILY-A3 | 关闭 | 38.656 | 124.853 | 61.612 |
| DAILY-B2 | 开启 | 39.749 | 114.734 | 51.231 |

两组平均 GPU clocks 约 499.33/495.91 MHz。平均总读少 8.49%，平均总写少 17.22%，平均 FPS 高 3.31%。关闭后的回归接近第一次关闭，支持分配策略具有稳定的访存收益。截图未发现明显缺画面或几何错误；未进行全场景或长时间散热验收，不把此结果加到以前不同分辨率/不同候选的百分比上。

## 普通启动发布

宿主 `6f3b131` 加入应用内冷启动布尔选项与严格 desktop profile schema 3。新 schema 在 schema 2 的全部身份和选项上增加必填 `turnip_swapchain_ubwc`；旧 schema 的额外字段仍拒绝。Java 校验仍约束实际 APK、设备、驱动、模块清单、shader marker、原生表及发布收条。启动工具拒绝运行中改选及非 app-local Adreno provider；未指定时保留外部实验条件。

此精确 Q735 发布配置选择 true，在 Vulkan 初始化前写入应用自己的环境，不保留全局 ADB 属性。完成两次无测试 extras 的普通 MAIN/LAUNCHER 冷启动，真实营地、共享/lazy 路径和原 save2/停止均通过。第一次普通启动用驱动已有布局日志确认，四张实际游戏交换链均为 UBWC；第二次普通启动无 layout 输出。用户显示、散热及存档设置保留。候选数据中的状态不是最终 APK 的独立性能实测；最终 APK 验收的是同一驱动选项的真实分配与普通启动行为。

- APK：`fbc5ab997074884f786c2016fd0a38878f815da3718c6c1a2c2cf91a2286d04f`
- 宿主 ELF：`f2c436e25dedf653c463f87a6af360464f59a8b59b30537226ebb57e27810682`
- Driver：`b3fce5ab29c6617bd2cf334b3a56c67951de271bd870878c9a3121ff606b1b9f`
- 原生表：`0c24bb093dd9840ad4b461b73dbc4e120fb105b6c9f7e5bdaea2f2d8df973738`
- Pack：`92861f05e2311c6999e724c2d6652ac90ac583a5cfc2cc635fb695a5c51ff3e7`

27 项 Python 启动检查及 Java profile 身份/类型/隔离检查通过，canonical APK 构建通过。以上不替代真实游戏验收；真实游戏的两次存档、布局和共享路径证据独立保存。

## 后续优先级与边界

本轮证明最终输出与合成分配能够多消耗一块实际带宽，并非所有差距都来自翻译线程。剩余写入继续优先核对大的 HDR/深度附件在 pass 边界的写回、确实未被读取的 Store 与复制。HDR 附件已经压缩，后续候选必须先证明具体多余工作，不能简单再开一个“RT 压缩”开关，也不能按 draw 时间归因 TBDR 成本。

旧已接受 APK、f830 驱动和原混合原生表均保留，未重写游戏二进制。Mali 未使用。采集后游戏均原存档两次后停止；原厂 GUI、Xvfb 和该次采集器均退出，关闭收条保留了一次进程自然退出导致的 /proc 读取竞争，后续核对未留下采集器。

## 证据

- `.local/logs/native-layout-visibility-v1/`：原游戏布局、夹具、原工具实际重生成与保留失败。
- `.local/build/wsi-ubwc-game-resources-v2/`：正确混合资源选择及全部 payload 一致性对比。
- `.local/logs/wsi-ubwc-game-v1/`：四轮 launch/原存档收条、截图、paired-summary，以及原 `.sdp`/数据库/Perfetto 与其完整窗口。
- 同目录 `publication/result.json`：`committed=true`，普通启动两轮、实际布局与完整身份。
- `.local/build/wsi-ubwc-app-private-v1/`：canonical APK、构建记录及宿主 ELF。
