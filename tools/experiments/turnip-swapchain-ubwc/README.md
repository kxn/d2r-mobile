# Turnip Android 输出缓冲 UBWC 实验

状态：驱动选项默认关闭。独立夹具已验证真实 UBWC 分配、blit、上屏及前后台重建；真实游戏已进入营地，性能资格仍须以同 APK 交错对照为准。

当前 f830 Turnip 的原生布局日志确认：日常 2358×1080 输出中，HDR、颜色和深度附件为 UBWC，四张交换链图像为线性、压缩元数据大小为零。布局日志是驱动已有的 `TU_DEBUG=layout`，不是新写的计时器。原始记录 `.local/logs/native-layout-visibility-v1/`；此次运行真实游戏营地后原 save2 并停止，调试属性已恢复。

Mesa 的通用 Android AHB 用途查询及 GrallocUsage 回调只提供 GPU render/texture 用途，未请求 Qualcomm 的 vendor UBWC 位。库内现有 Qualcomm gralloc 定义给出位 28 的含义。新 Android loader 优先通过 `GetPhysicalDeviceImageFormatProperties2` 查询 AHB 用途，未调用此机的旧回调；仅改旧回调的首个候选没有改变分配。参见 [AOSP 的交换链分配实现](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/vulkan/libvulkan/swapchain.cpp)。不能把“请求位已设置”当成压缩实际开启，也不能未经测量声称解释了与系统驱动的全部差距。

候选覆盖 Turnip 的 AHB 专用 image-format 查询分支及两条旧 Android GrallocUsage 回调，先调用 Mesa 原实现，保留原错误及所有用途；启动选项 `TU_UBWC_SWAPCHAIN=1` 才为支持 UBWC 的 RGBA8/BGRA8 最优布局、普通 2D 渲染或传输目标增加厂商分配用途。AHB 路径核对有效扩展 usage/flags，未知 create flags、超出支持范围的用途、其他格式及 Gralloc1 shared/front-buffer 均保持原行为。选项使用 Mesa 已有 once 缓存。实际图像布局继续从系统 mapper 元数据导入，不手写 UBWC 布局、pitch 或 offset。

Android 属性对应 `debug.mesa.tu.ubwc.swapchain=1`，仅在实验进程冷启动前设置，结束必须恢复。原游戏、driver、native table、启动 profile 均须先核对/保全；不能用候选驱动直接启动旧 native table，也不能篡改旧表中的身份摘要来绕过拒绝。先用独立图形夹具验证真实分配、绘制、上屏和重建，再通过原生产工具重生成匹配的原生资源声明，才有资格做真实游戏的原厂写入量对照。夹具不算游戏移植里程碑。

`tu_android_usage.cc` 加入当前 Turnip 源码的 Android-only `libtu_files`，其余源文件和原 host-import/native ASTC 补丁保持不变。构建入口 `build.py` 使用已冻结 f830 源码全量清单，校验后复制到独立目录并用原 Meson/Ninja 参数构建；不会改写已部署或已归档的驱动。

冻结输入必须是最终 `source-ready-freeze/entire-source-ledger.json`，而非之前不包含 linear 补丁的 AA 清单。驱动 v4 SHA256 为 `b3fce5ab29c6617bd2cf334b3a56c67951de271bd870878c9a3121ff606b1b9f`。夹具构建入口 `build-fixture.py` 要求明确的 `--driver-build` 与全新 `--output`，复制并校验现有加载桥及所选驱动，不包含游戏文件。

Q735 的夹具 OFF5/ON6 均通过红、绿、蓝、白四种 transfer blit 颜色及真正退至桌面、同 PID 返回重建。开启组的布局日志确认四张交换链图像均为 UBWC，每张含 53,248 字节压缩元数据；关闭组四张均为 LINEAR。颜色夹具没有性能计时，不能拿来报告游戏 FPS。证据为 `.local/logs/native-layout-visibility-v1/` 下相应夹具收条及原始布局日志。

新驱动的 driver UUID 变化，layout/device UUID 与原生生产工具源码指纹不变。已实际运行原生产工具重生成 6,217 条 ASTC4 声明及部署选择所需的 2,838 条 ASTC6 声明；按当前部署的 source/declaration 身份合成 3,379 条 ASTC4 加 2,838 条 ASTC6。全部纹理 payload 与当前混合资源包逐文件一致，仅身份表及包声明变化。第一次误用更早的全 ASTC4 包，游戏在入场前被原认证检查拒绝，失败记录保留；没有绕过校验或改写旧表摘要。正确输入与对比在 `.local/build/wsi-ubwc-game-resources-v2/`。游戏候选仅对该精确驱动指纹保留已有 coherent/lazy 资格，运行时原字节一致性探针照常执行。
