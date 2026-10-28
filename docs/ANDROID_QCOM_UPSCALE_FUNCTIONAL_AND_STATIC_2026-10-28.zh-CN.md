# QCOM 硬件滤波与 SGSR：真实离屏功能、历史 .85 画面对照

2026-10-28。结论是：**这台 Adreno 735 的已用 12b 驱动确实能执行 QCOM 硬件加权滤波；修正配对坐标/权重合同后，12 个独立功能输入全部通过。真实历史 .85 场景的三路 GPU 输出也已取得。HOF 与原画面接近，当前 SGSR1 候选颗粒与块状增强明显，不能直接称为画质提升。尚未测游戏 FPS，也没有默认启用任何候选。**

这完成了“硬件上采样能否实际用起来”的独立资格与第一轮静态画面验证，不等于游戏接入、低 .75 渲染验证或动态超分验收。HOF 是固定核空间滤波，SGSR1 是 shader 空间算法；二者均没有本轮使用运动矢量、历史帧或证明为专用时域超分单元。

## 实际执行和输入身份

设备为 SM8635 / Adreno 735。独立 Android fixture 以普通应用 UID 执行 Vulkan 1.3，明确启用 `VK_QCOM_image_processing` 与 `textureSampleWeighted`；此设备实查 QCOM1 可用，QCOM3 不可用。实际资源支持包括 R11G11B10_UFLOAT 的 weighted sampling、R16_SFLOAT 权重图、最大 1024 phases 和 64×64 filter。不是只看扩展名，也没有调用未知私有 ABI。

| 项目 | 实际输入 SHA-256 |
| --- | --- |
| 私有 Turnip 12b | `12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5` |
| 当前 cf83 原 `ps_upsample_p` FS | `e114e05ccbf91820d9089932f65a87fda490b78ad28647e99fb584d6e578fc1a` |
| 当前 cf83 VS | `0f96df4903498af6eda45a67fe8d07fa876d7e30624f3f962379e354fa013378` |
| 本次实际 HOF v2 SPIR-V | `d5a54a112042303f669f15edec359fa9985c7712cbe8ed255a7f5f94fff80aa5` |
| 本次实际 SGSR1 v2 SPIR-V | `ff7953bb9e0c8b4f21f17896e107b801fbef6378c7e0e9a1a0bf6dcbe18bdfc1` |

执行了当前原项目使用的真实 shader 字节，原五 tap fallback 保留；**没有执行真实游戏 ARM64 模块**。fixture 自有三角形、资源生命周期、离屏 RT 与功能输入不能代替游戏原生运行里程碑。没有改游戏 binary、通用 NVN 行为、生产 shader 链或启动参数，也没有新增 profiler、计时器或采样器。

## 从失败到有限功能通过

第一次实际离屏测试（`.local/logs/qcom-hof-offscreen-v1/ACTUAL-DRAW1`）完成 46 组、92 个输出，但功能结果为 FAIL，完整保留。不能将 PSO 编译成功当成采样坐标正确。

该轮 one-hot 权重使用 source 的 RG 保存整数 x/y，1872 个内部像素显示系统性额外半 texel 偏移；四类 x/y 偏移各 468 像素。phase barcode 也不符合最初的 floor 选相位。问题不是容差太紧，不能用放宽阈值处理。签名负权确实执行出 −2/+6，HDR>1 也能保留，但它们不能掩盖坐标失败。

修正版另建 `.local/logs/qcom-hof-offscreen-v2`，没有修改旧冻结源码/产物。配对修改为 shader 使用 `q=uv`，32×32 phase 权重表代表点为 `t=h/32`。该驱动实测支持的模型为 first tap `floor(U-.5)-1`、选定非 tie 点按 nearest phase；它与最初规范推导存在差异，**这是精确 12b 驱动的应用实验合同，不是所有 QCOM 或 Vulkan 实现的通用结论**。规范完整坐标链没有提供可随意补一个隐藏半 texel 的理由；驱动差异未在本轮修改驱动中解决。

修正版实际完成 12 组、24 次绘制，RGBA32F 输出：

- one-hot 地址与四个非对称 phase 对的 1/4、3/4 bin 共八个 barcode 检查全部通过，相关输出误差为 0。未把 floor/ceil 任意一个都算通过。
- −.5/+1.5 两 tap 的冲激输出精确为 −2/+6；HDR4、原 gain 最后乘和 alpha=1 通过，gain 64/255 最大误差约 `5.94e-8`。
- 仍使用原 `2e-4 + 2e-5*|expected|` 容差，没有扩大容差。没有非有限输出或 alpha 错误。

该集合不覆盖全部 1024 phases、所有 tie/phase31→0 wrap 和全部 HDR 数值域。旧 v1 某个 wrap 点与 carry 模型误差约 `7.6e-5`，无 carry 模型差约 .0313，只是单点补充，不能充当全域证明。RGBA32F 功能测试不等于游戏实际 RT 性能。

实际证据：`.local/logs/qcom-hof-offscreen-v2/ACTUAL-DRAW1-analysis/functional-comparison.json`。普通 UID10378/PID16605，执行后停止并卸载独立包；主游戏 APK 和六个关键锚点一致，清理错误为空。

## 真实历史 .85 同源静态对照

复用现成离屏路径，只做原算法、HOF v2、精确 SGSR1 v2 三次 GPU 绘制。输入不是 CPU 缩小截图，而是现有官方历史 trim 导出的真实 R11G11B10 场景和原配 gain/C7；三组共享资源、只上传一次：

| 输入 | 尺寸/内容 | SHA-256 |
| --- | --- | --- |
| source | 1673×765，R11G11B10_UFLOAT，5119380B | `a6f9fdd6d9e64d36aa820c90de4c718af2a27295f534e43b2553f3e27c93469b` |
| gain | 1673×765，R8_UNORM，1279845B，范围67..255 | `3ae61c645d9db16084b6aaf8f4be5da2f7803e743cc3dcb33c48ef2ab28ac17d` |
| C7 | 原配3个vec4，48B | `9e593f825b1b22e42fe99c02baf633a50d745683fa19d5e1f454c79d6c3516d0` |

C7 的 scale=(1,1)，texel step=(1/1673,1/765)，原锐化 k≈.06，clamp 是半 texel 内边界。它来自同一 trim 的 descriptor2063 / buffer26266+1325824 / 最后记录 draw70437，不是未来 draw79146 的另一块 buffer。source/gain/C7 同属这个历史快照，但没有把它们冒充为未来 draw79146 写完后的实际输入或今天游戏的同瞬间内容。来源收条为 `.local/logs/qcom-hof-weight-contract-review-v1/old-085-resource-audit/selected-trim-inputs/input-receipt.json`。

source 真实 sampler1806 是 linear+clamp-edge；gain 真实 sampler2062 是 nearest+repeat。fixture 有效过滤/寻址相同；它只创建一 mip 且采 LOD0，所以原 source 的 linear mip/maxLOD1000 与 fixture 的 nearest mip/maxLOD0 不造成实际采样差异，未参与的 border color 也不影响结果。不是宣称两份 sampler CreateInfo 逐字段相同。原始 sampler 证据在本轮 `trim-sampler-objects.json`。

输出均为 1968×900 RGBA8_UNORM、7084800B，resize/filter/clamp 全由 GPU 执行，CPU 只编码 PNG 和按原像素裁 ROI，没有 CPU resize/sharpen/gamma。三路 alpha 全255。source 的有效 RGB 最大约 (2.15625,1.3125,1.84375)，没有在 gain 前统一夹到1；这组数据不是所有 UFLOAT 内容的最大值证明。

| 输出 | 原始像素 SHA-256 | 与原算法的 MAE（0..255码值） | 分量绝对差p99 / max |
| --- | --- | ---: | ---: |
| 原算法 | `3d5942615da6040ecccceb6d142491214f0c6438f6b517f98a41482dddfe666d` | — | — |
| HOF v2 | `f3a176c4757f07f435bd5a8ce1541bd68c0052b0671ad842f49314df8095f745` | .2545 | 2 / 14 |
| SGSR1 v2 | `51089ddab71bdd5b07c05f5bb277e1bfd21dd95730f7a76fb0ea739c451cb229` | 1.9900 | 13 / 27 |

这些数值只表示与原算法差多少，**不是质量优劣评分**。没有同瞬间原生1.0真值，不能用更锐或差值小推断细节恢复，也没有移动序列可证明不闪烁、不振铃或不鬼影。

已用 `view_image` 实看同一角色、草地、石砖、亮部的 512×384 原像素 ROI；每张三列依次为原算法/HOF/SGSR：

- HOF 与原算法总体接近，局部轮廓/纹理有细小变化，尚无足够证据说画质有明显提升。
- 当前 SGSR1 v2 的人物皮肤、木栏、石墙、草地出现更强的块状/颗粒与边缘对比；亮部木纹也更硬。不能将锐化感称为恢复细节，不建议据此默认开启。
- 未看到候选独有的整幅 UV 位移、大块缺几何。三组共有的黑色剪影是同一 source 内的内容，不能算候选特有故障。

本地结果：[完整原算法图](../.local/logs/qcom-hof-static085-v1/ACTUAL-STATIC1-analysis/base.png)、[HOF图](../.local/logs/qcom-hof-static085-v1/ACTUAL-STATIC1-analysis/hof.png)、[SGSR图](../.local/logs/qcom-hof-static085-v1/ACTUAL-STATIC1-analysis/sgsr.png)。[角色三列](../.local/logs/qcom-hof-static085-v1/ACTUAL-STATIC1-analysis/character-base-hof-sgsr.png)、[草地三列](../.local/logs/qcom-hof-static085-v1/ACTUAL-STATIC1-analysis/grass-base-hof-sgsr.png)、[石砖三列](../.local/logs/qcom-hof-static085-v1/ACTUAL-STATIC1-analysis/stone-base-hof-sgsr.png)、[亮部三列](../.local/logs/qcom-hof-static085-v1/ACTUAL-STATIC1-analysis/highlights-base-hof-sgsr.png)。图片没有进 tracked 源码归档。

实际目录 `.local/logs/qcom-hof-static085-v1/ACTUAL-STATIC1`，普通 UID10379/PID19129。raw terminal 和 index0/1/2 证明3个PSO/3draw/3输出；旧控制器遗留成功字段仍叫 `actual2_pipeline_creations_completed`，名称未重写，不据此误报只有2个。独立包已停止卸载，主游戏 APK/六锚点相同，cleanup_errors=[]。人眼判断作为独立 `visual-review.json` 补充，原分析 JSON 保持原样。

## 可以接入哪里，尚缺什么

最小接入位置是原游戏的 `ps_upsample_p` 过滤阶段：后续须单独实现可选画质功能，保持原 UV/scale/clamp、原 gain 后乘、alpha=1 和后续 UI compose，替换其内部空间过滤。当前没有声称通用后端已经存在这个替换入口；不在最终 present 把全部 UI 一起锐化，不向通用 NVN 添相机/场景判断。本轮没有执行 UI compose，之前 API 顺序证据仍用于确定接入边界，不能把本轮独立静态输出当整帧游戏截图。

HOF 要额外常驻32KiB权重像素以及专用 weight view/sampler/bindings；它是真实硬件加权采样接口，但不能称完整 SGSR。固定 Catmull 核替代 C7.k 控制的原锐化是明确画质取舍，不能称语义等价。

当前 SGSR v2 保留原接口与 gain 合同，却把部分颜色/滤波运算实际编成 FP16。历史这组较小 HDR 值完成了输出，不证明 UFLOAT 全域安全；仍需按已有 v3 计划将滤波/权重/分母 highp 化并检查非有限/分母异常回退。其固定锐化强度、绿色亮度代理与边界接缝也是待视觉验证策略，不能因为编译成功或本次没黑块就直接用于全部场景。

下一步若继续，先实现可切换、保守门控的 HOF 专用游戏候选，再用现成原厂 Profile 对比同尺寸、同场景、同驱动整帧成本，另查移动过程和其他场景画质。当前没有任何 FPS/带宽收益实证；本轮不以 native 指令量或寄存器数量推算升帧。主游戏正常版本保持原样。

修正版可重建源码已归档于 `tools/experiments/qcom-hof-upsample/qualified-12b-v2/`，官方 glslang/spirv-val 重建精确命中上述 `d5a54a...`。上一级旧 compile-only 源码与失败 v1 保持原样。实际 fixture/输入/产物指纹由新收条引用，未将 SPV/APK/驱动、大 capture 或图片加入源码树。

## 一手规范与官方算法来源

- [Khronos QCOM image processing](https://docs.vulkan.org/refpages/latest/refpages/source/VK_QCOM_image_processing.html)：能力和 feature 合同。
- [Khronos weight image sampling](https://docs.vulkan.org/spec/latest/chapters/textures.html#textures-weightimage)：phase、权重与坐标规范；本轮实际 12b 差异单独保留。
- [Khronos GLSL QCOM image processing](https://raw.githubusercontent.com/KhronosGroup/GLSL/main/extensions/qcom/GLSL_QCOM_image_processing.txt)：`textureWeightedQCOM` 接口。
- [Qualcomm 官方 SGSR](https://github.com/SnapdragonGameStudios/snapdragon-gsr/tree/d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e)：本次 SGSR1 固定版本来源与 BSD-3-Clause 条款，HOF 不宣称为其算法。
