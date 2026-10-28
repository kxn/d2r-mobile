# SGSR1 v2与独立真机编译资格探针（2026-10-28）

**独立探针已完成主机构建、签名和输入检查，尚未上机。** 它只使用真实12b驱动创建当前原FS与SGSR候选两条graphics pipeline，并通过官方Vulkan接口查询QCOM扩展/features/格式；没有draw、dispatch、queue submit、计时器、游戏模块或伪造的0.85常量。

复用已经跑通的 `fs-critical-schedule-v3-compile-v1` loader、Java入口、SDK/NDK打包、普通UID安装与清理控制器；没有另起一套采集工具。使用独立包名 `org.kxn.sgsrcompile`，APK与构建记录在 `.local/build/sgsr1-compile-qualification-v1/`。未操作ADB、安装、部署或修改主游戏。

## 准确输入

| 对象 | 固定输入 |
|---|---|
| Driver | 现有accepted12b，SHA `12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5` |
| 原FS | 当前cf83 `52aac1f2306b8fc5.spv`，SHA `e114e05ccbf91820d9089932f65a87fda490b78ad28647e99fb584d6e578fc1a` |
| 共享VS | 当前cf83 `69cfdc6545a66915.spv`，SHA `0f96df4903498af6eda45a67fe8d07fa876d7e30624f3f962379e354fa013378` |
| 候选FS | 独立SGSR1-v2，SHA `ff7953bb9e0c8b4f21f17896e107b801fbef6378c7e0e9a1a0bf6dcbe18bdfc1` |

历史module206只用于原颜色合同与旧回放，**不充作本次当前原FS基线**。布局/固定raster/顶点格式取自官方历史pipeline23282，当前原VS/FS及候选反射对应；这不等于已经复现当前全部PSO原生身份。没有沿用v3实验driver，也没有旧native hash等值门。

## v2修正与仍有的边界

独立review指出v1默认mediump使改写后的坐标、C7和texel比较可能丢失精度；其fallback也未完整保留原逐tap NaN清理。v2明确将originalUV、C7、ViewportInfo、内部坐标、尺寸、texel比较与gainUV设为highp；fallback直接复用当前原FS经官方spirv-cross导出的完整main，保留原min/max顺序、每次采样的NaN清理、FMA、原增益UV和alpha1。非有限坐标/常量或不符合texel/有效区域合同则走完整原路径。

v2已通过现成 `glslangValidator`、`spirv-val` 与reflection检查；标准 `spirv-dis` 中上述坐标/C7不再带RelaxedPrecision。**没有重跑v2图像，v1旧回放/图片/收条原样保留，不能改写成v2画质通过。** v2在 `.local/logs/snapdragon-super-resolution-host-candidate-v2/`。

仍须明确：

- SGSR固定EdgeSharpness替代原五tap的k；不声称保留原锐化强度，k为0/负值也可能触发SGSR。
- 移除上界1截断是为了保留原增益之前实际存在的>1高光；不是官方HDR认证。
- 坐标finite不证明纹理颜色finite，SGSR的sum/aWY除法及固定阈值/delta策略在任意输入范围下尚未证明。进入游戏前需证明数值范围或在异常时完整回原路径。
- 当前0.85的实际c7和输入descriptor仍[缺捕获页](ANDROID_SGSR1_CURRENT085_EXISTING_PAGES_2026-10-28.zh-CN.md)。它们不影响pipeline创建，不能因此填假值绘制。

## 能力查询与结果边界

原生探针先枚举实际设备扩展，仅在广告时查询 `VkPhysicalDeviceImageProcessingFeaturesQCOM`／`VkPhysicalDeviceImageProcessing3FeaturesQCOM`，并查询第一版properties；对R8、RGBA8、R11G11B10、浮点权重候选格式查询format flags和image-format支持。缺扩展记“不可查询”，不补零。

探针创建的device没有启用QCOM扩展，SGSR候选是普通shader路线。即使扩展、feature和格式均支持，也只能证明潜在硬件滤波能力，不能当作执行了硬件超分。未创建权重图、sampler、图像、framebuffer或任何绘制命令，不自建计时器。

root统一执行入口在 `.local/logs/sgsr1-compile-qualification-v1/README.md`。现有控制器只在主游戏已停且准确c09/cf83/12b相关文件锚点可核对时安装独立包，验证普通UID和真实私有driver/shader SHA，导出官方IR3静态输出，再停止/卸载自己的fixture并核对主游戏未变。准备阶段已完成dry-run，尚无实际设备资格、编译原生结果或画质/帧率结论。

冻结输入、APK SHA、主机检查与全部源码位置保存在同目录 `frozen.json`；构建记录在APK目录 `build.json`。成功创建两PSO只意味着驱动能接受这两条编译输入，不批准默认启用，不推算低res画质或性能。
