# SGSR1真实12b驱动编译与QCOM能力结果（2026-10-28）

**当前原FS与SGSR1-v2候选都已在这台Adreno735的真实12b驱动上创建graphics pipeline。QCOM第一版硬件图像处理功能实际可查询，第三版未广告。** 这是编译/能力资格通过，没有执行绘制，不能称超分画质或帧率通过。

首次安装编排仍按[安装标题合同错误](ANDROID_SGSR1_INSTALL_ORCHESTRATION_FAILURE_2026-10-28.zh-CN.md)保留。root在同一个Android安装会话内核对准确标题并完成确认，没有第二次install请求；恢复后的独立运行记录是 `.local/logs/sgsr1-compile-qualification-v1/ACTUAL-COMPILE1-RECOVERED/`。原 `ACTUAL-COMPILE1/result.json` 的SHA与失败归档逐字节相同，没有重标为success。

## 实际输入与运行资格

恢复后的fixture为普通UID10369、PID28447；准确私有driver及三个shader文件SHA与构建记录一致：

| 输入 | 实际SHA256 |
|---|---|
| accepted12b driver | `12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5` |
| 当前cf83原FS | `e114e05ccbf91820d9089932f65a87fda490b78ad28647e99fb584d6e578fc1a` |
| 当前cf83原VS | `0f96df4903498af6eda45a67fe8d07fa876d7e30624f3f962379e354fa013378` |
| SGSR1-v2 FS | `ff7953bb9e0c8b4f21f17896e107b801fbef6378c7e0e9a1a0bf6dcbe18bdfc1` |

因此原组不是旧module206字节，也不是v3实验driver。现成Mesa输出整理器将两FS准确原始模块BLAKE3与官方NIR、native BLAKE3连接。VS实际文件SHA已核准，但本探针仅打开FS编译输出，没有VS native统计，不补猜其成本。

终端记录两次pipeline创建、零queue submit、零game modules、无计时；独立fixture已经停止并卸载，主游戏c09及profile、cf83、12b、资源和模块锚点前后不变。没有把独立探针安装到主游戏包，也没有填入未知0.85常量执行绘制。

## 实际QCOM能力

实际设备为 `Turnip Adreno (TM) 735`。官方查询结果：

| 项目 | 结果 |
|---|---|
| `VK_QCOM_image_processing` | 实际广告 |
| `textureSampleWeighted`／`textureBoxFilter`／`textureBlockMatch` | 均为true |
| 最大weight filter phases | 1024 |
| 最大weight filter dimension | 64×64 |
| 最大block-match region／box-filter block | 均64×64 |
| `VK_QCOM_image_processing3` | **未广告**；不查询其feature，不补零冒充已知能力 |

格式查询结果，均为optimal tiling：

| 格式 | 普通线性采样 | 可作权重图 | 可加权采样 | Box filter | Block matching |
|---|---|---|---|---|---|
| R8_UNORM | 是 | 是 | 是 | 是 | 是 |
| RGBA8_UNORM | 是 | 否 | 是 | 是 | 是 |
| B10G11R11_UFLOAT | 是 | 否 | 是 | 是 | 否 |
| R16_SFLOAT | 是 | 是 | 是 | 是 | 否 |
| R16G16_SFLOAT | 是 | 否 | 是 | 是 | 否 |
| R32_SFLOAT | 是 | 否 | 否 | 否 | 否 |

官方image-format查询对这些格式的sampled用途均返回成功；R8和R16权重图用途也返回成功，RGBA8和R11G11B10的sampled+color-attachment用途返回成功。这些是能力查询，没有实际创建/采样权重图。

本探针**没有在device创建时启用QCOM扩展**，两PSO也使用普通shader实现SGSR，没有执行固定功能滤波。第一版说明这台设备有可继续验证的硬件滤波路径，不等于SGSR算法自动使用它；SGSR的内容自适应权重不能直接当固定预设weight filter替换。第三版特定gather能力当前不可用。

## 原生静态成本与精度

| Mesa官方编译输出 | 当前原FS | SGSR1-v2候选 |
|---|---:|---:|
| 指令，含编译器统计NOP | 82 | 372 |
| 非NOP | 67 | 312 |
| full register统计 | 6 | 6 |
| max_waves统计 | 16 | 16 |
| constlen | 8 | 12 |
| cat5静态站点 | 6 | 12 |
| gather4g站点（完整输出直方图） | 0 | 4 |
| getsize站点（完整输出直方图） | 0 | 1 |

候选增加了SGSR滤波、原完整fallback、有限性/texel/边界资格以及preamble准备。UBO新出现两条宽度2和一条宽度4的uniform LDC，现成NIR整理器确认位于preamble；不能把它们写成每个fragment重复从外存读取。完整native直方图包含preamble和互斥分支，而cat5统计及某一分支覆盖的范围不同，不能将12个站点解释为每像素执行12次采样，也不能按静态指令倍数推算耗时。

官方NIR确认需要highp的部分保留32bit：纹理尺寸及其倒数、1e-4 texel比较、输入插值UV、缩放/min/max、SGSR像素坐标/floor与gather坐标。native相应使用full register的尺寸/坐标运算。

**滤波颜色、green代理与权重实际降低到了FP16。** 官方NIR中中心和gather颜色结果为16bit，权重归一化也有16bit；native输出有58个 `mad.f16` 静态站点及half寄存器操作。因此即使摘要打印“0 half、6 full”，也不能据此宣称候选全部FP32。highp修复的是坐标合同，没有自动证明颜色/权重数值范围。

候选仍需在进入游戏前证明颜色范围、有限采样值和sum/aWY分母的数值安全，或异常时完整回原路径；还需评估固定SGSR锐化与原k不同、边界锐度接缝、移动闪烁。原始与候选native hash分别是 `d527ec86ab7f21767da039a866fa7b9c478748d1d358b9ec2444ab0101e5180e`、`805d0d1e2276c59ab7d7b1eadd2e014fd115f39e4b2a3d7b51844770a228437d`。

本轮没有GPU忙闲、带宽、cache miss、fragment执行次数或GPU时间。打印的stall/NOP/wave字段是编译器静态信息，不是动态等待计数；相同寄存器/waves也不证明实际延迟或占用率相同。**当前结论是普通SGSR候选可编译、QCOM1硬件滤波能力存在；不能预测把0.85降到0.75后能提升多少帧。**

机器可读结果在 `ACTUAL-COMPILE1-RECOVERED-analysis/comparison.json` 和 `qualification-summary.json`，精度原文摘录在 `precision-excerpts.txt`。它们复用既有Mesa整理器，仅整理官方文本，不新造计时或采样工具。当前0.85仍[缺实际UV/输入descriptor页](ANDROID_SGSR1_CURRENT085_EXISTING_PAGES_2026-10-28.zh-CN.md)，v2尚无绘制；此前1.0图片只属于v1，不据本次编译改写其画质资格。
