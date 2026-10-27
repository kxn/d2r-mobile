# Turnip Android 输出缓冲 UBWC 实验

状态：默认关闭，尚未证明真实游戏可用或有性能收益。

当前 f830 Turnip 的原生布局日志确认：日常 2358×1080 输出中，HDR、颜色和深度附件为 UBWC，四张交换链图像为线性、压缩元数据大小为零。布局日志是驱动已有的 `TU_DEBUG=layout`，不是新写的计时器。原始记录 `.local/logs/native-layout-visibility-v1/`；此次运行真实游戏营地后原 save2 并停止，调试属性已恢复。

Mesa 的通用 Android GrallocUsage 回调只提供 GPU render/texture 用途，未请求 Qualcomm 的 vendor UBWC 位。库内现有 Qualcomm gralloc 定义给出位 28 的含义；实际分配器是否接受、mapper 是否能正确读取压缩元数据以及系统合成是否正常，仍必须真机验证。不能把“请求位已设置”当成压缩实际开启，也不能未经测量声称解释了与系统驱动的全部差距。

候选只覆盖 Turnip 的两条 Android GrallocUsage 回调，先调用 Mesa 原实现，保留原错误及所有用途；启动选项 `TU_UBWC_SWAPCHAIN=1` 才为支持 UBWC 的 RGBA8/BGRA8 普通交换链增加厂商分配用途。默认、其他格式、非渲染用途和 Gralloc1 shared/front-buffer 均保持原行为。选项使用 Mesa 已有 once 缓存。实际图像布局继续从系统 mapper 元数据导入，不手写 UBWC 布局、pitch 或 offset。

Android 属性对应 `debug.mesa.tu.ubwc.swapchain=1`，仅在实验进程冷启动前设置，结束必须恢复。原游戏、driver、native table、启动 profile 均须先核对/保全；不能用候选驱动直接启动旧 native table，也不能篡改旧表中的身份摘要来绕过拒绝。先用独立图形夹具验证真实分配、绘制、上屏和重建，再通过原生产工具重生成匹配的原生资源声明，才有资格做真实游戏的原厂写入量对照。夹具不算游戏移植里程碑。

`tu_android_usage.cc` 加入当前 Turnip 源码的 Android-only `libtu_files`，其余源文件和原 host-import/native ASTC 补丁保持不变。构建入口 `build.py` 使用已冻结 f830 源码全量清单，校验后复制到独立目录并用原 Meson/Ninja 参数构建；不会改写已部署或已归档的驱动。
