# 修复后的原厂 GPU 计数器、呈现与二级命令缓冲差异

## 实测方法与对照

使用此前已跑通的 Snapdragon Profiler 2026.9 官方 GUI、服务、九项实时计数器与官方 Perfetto。按真实游戏 Surface 的首尾完整出图间隔裁剪计数器数据，使用官方时钟快照对齐设备/SDK 时间；没有新采样器、GPU 计时器或以单 draw 时间推断 TBDR 成本。

当前端是已发布 `5d4b24c` canonical APK，Turnip f830、native ASTC、共享/lazy/精确 GPU 字节依赖、内联 CPU7、stream splitting OFF、`prefer_sysmem`。上游是同机先前已验证的独立 Java carrier，原 release native 库不变，APK SHA256 `b8d1fcd10edcc70c2f1f98758efeb396fafdb62ce6c209b3afd4934464313592`，系统高通驱动与原 BC 路径；沿用之前已资格化的五个 inline-CS shader 补齐和独立存档，不是本轮重新改库。双方输出 1968×900、固定 0.85，同一营地位置。驱动/纹理资源不同仍是比较边界。

最初 `GPU-FAST1` 窗口特化变体 30→32，有编译增长，保留但不作为正式无编译基线。补采 `GPU-RETURN1` 前等待 tick≥2500、变体≥32 且八次不同状态行稳定；正式窗口 PSO 257→257、变体 32→32。临时尺寸对照也为 257→257、32→32。新上游控制仍为约 43.36 FPS，与前次同机结果一致。

## 原厂结果

下表均为**整个 GPU 的计数器，包括系统合成和 Android UI**，不是独占 NVN、某一个 HDR pass 或实测系统 DRAM 饱和度。MB 为十进制。纹理读已经包含在总读中，不能相加。

| 指标 | 当前正式基线 | 上游新控制 | 临时缓冲尺寸对照 |
|---|---:|---:|---:|
| 官方 Surface FPS | **40.43** | **43.36** | **40.96** |
| 游戏输出 | 1968×900 | 1968×900 | 1968×900 |
| Swapchain / Android 游戏缓冲 | 2358×1080 | 1968×900 | 1968×900 |
| GPU clocks/s，百万 | 497.75 | 499.38 | 493.88 |
| 总读 MB/完整游戏 Surface 间隔 | 114.65 | 106.65 | 111.18 |
| 其中纹理读 MB/间隔 | 95.06 | 86.49 | 91.60 |
| 总写 MB/间隔 | **65.77** | **45.86** | **51.33** |
| 全 GPU fragment 计数，百万/间隔 | 29.53 | 26.15 | 27.90 |
| Shaders Busy，% | 71.44 | 80.20 | 72.70 |
| Texture Pipes Busy，% | 45.66 | 51.79 | 45.94 |
| Texture L2 Miss，% | 7.40 | 5.09 | 6.53 |

三组 GPU clocks 相近。当前额外总读主要体现为纹理读取，写入量差异更大。仅凭约 4.6 GB/s 的相近读速率或 Shader Busy，不能判断系统 DRAM 已饱和，也不能将全 GPU fragment 数差异称为游戏 HDR Overdraw。

## 缓冲尺寸对照的结论与范围

前次官方六帧 API 记录表明，双方均 FIFO、四张交换链图像；差异不在 MAILBOX。上游缓冲为游戏输出大小，由系统放大；当前实现先用 GPU 把游戏结果放大到物理 View 缓冲，再交给系统合成。上游 `dm/android/vulkan_present.cpp` 的 `present_native` 默认开启，调用 `ANativeWindow_setBuffersGeometry(active,w,h,0)`。官方 NDK 头文件说明该 API 只改缓冲像素数，窗口屏幕尺寸保留，合成阶段负责缩放。

实施前先做无需更改游戏或宿主 APK 的诊断：所有游戏停止时，将 Android 已有的 `wm size` override 从 1080×2358 临时改到 900×1968，继续强制相同游戏输出/3D 比例。记录实际缓冲尺寸后，用同一官方 SDK/Perfetto 方法采集，原存档 commits=2 后停止，核对仍为本次临时设置后恢复 1080×2358。

实际约少 **14.44 MB 写入/游戏间隔，约 22%**，总读写约少 17.9 MB/间隔；帧率只高约 0.5 FPS，不能说它解释或解决了全部剩余差距。

这项诊断同时减少 WSI、HWUI 和系统合成相关的缓冲像素，**不是只改 ANativeWindow 的隔离实验**。真正应用内的原生尺寸出图仍需独立实现/验证；不能承诺同样减少 22% 写入，也不能将降低全系统 override 作为产品修复。物理显示设置已经恢复，当前桌面默认仍由用户既有选项控制。

## 一个比末端缓存更值得验证的结构差异

前次实际 API 记录中，当前实现每帧约 49 次 `vkCmdExecuteCommands`，上游为 0，直接在主 command buffer 录制。当前后端仍采用此结构：先将绘制录到 secondary，结束时确定附件 Store 动作，再在 primary 中创建 RenderPass 并执行 secondary。Store 末端可改是 Metal 的自然接口，Vulkan 这层对应付出了额外结构成本。

已从 f830 的实际编译输入清单核对 `tu_cmd_buffer.cc`：SHA256 `53b6446cb169fbdb31d6ce406745f6641a84745a5d3f0e9f822bf187335cbafd`，对应函数与检查的源代码一致。`tu_CmdExecuteCommands` 会：

- 进入二级执行前处理全部 pending clean/flush。
- 结束后将图形状态标成 dirty。
- 将缓存状态重新初始化成保守的 pending invalidate；源码说明 secondary 中可能有任意 flush，primary 无法直接继承其已知状态。

这是实际源码行为，**不是已经测出 49 次全缓存 flush 的成本**。LRZ 的处理还取决于设备的 GPU direction tracking 和实际深度状态，不能一概说该机关闭了 LRZ。

因此下一项高优先级是隔离“secondary 带来的缓存/状态边界”。应使用可证明的初始附件策略与 primary 录制对照，而不是再给每 draw 加一个 cache。实验须控制 Store、像素本地子通道、清屏/只读深度策略：若不能同时支持，必须两边使用相同开关，避免把关闭其他优化的代价混入 secondary 差值。不能直接删 Turnip 的保守 invalidate；那需要独立证明跨 secondary 的真实缓存状态。

## 证据与收尾

原 SDK `.sdp`、数据库、定义、实际 GUI/服务身份、Perfetto、SQL/CSV、时钟窗口、截图和原存档收条位于 `.local/logs/coherent-fast-gpu-v1/gpu-tools/`。正式结果为 `comparison.json`（`GPU-RETURN1`、`GPU-UPSTREAM2`、`GPU-PRESENTSIZE1`）；早期有特化增长的 `GPU-FAST1` 原数据保留。`display-size-experiment.json` 记录临时显示设置的恢复；`secondary-driver-source-proof.json` 记录真实编译源码核对。

当前进程和临时尺寸实验均经原存档确认后停止；上游使用独立的既有比较存档副本，仅请求原后台生命周期后停止，没有伪称取得上游 save2 回执。全部本次 SDK GUI、Xvfb 和设备 collector 均已核对退出，系统工具设置恢复原值。关闭时有一个 collector 在 `/proc` 身份读取之间自行退出，原错误保留；后继逐项存活核对为零，未杀替代进程或无关服务。此轮未更新游戏模块、贴图包、shader 或默认 APK。以上仍是营地短窗口，未宣称全场景、温度或 60 FPS 已解决。
