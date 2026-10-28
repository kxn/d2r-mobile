# 高通超分接入调研（2026-10-28）

**这台 SM8635／Snapdragon 8s Gen 3 有可落地的 SGSR shader 路线，适合先试 SGSR1。它不能直接使用最新 Adreno Neural Fusion 的专用 AI 超分硬件。** 最大接入难点是把超分放在低分辨率3D与原生UI之间，避免在已经放大、已经合成UI的最终图像上再处理一遍。本轮只调研、下载与主机编译，未操作ADB、部署或执行GPU。

后续进展：已用同一12b驱动、独立普通应用完成SGSR1与QCOM硬件加权过滤的真实PSO编译；QCOM候选生成了专用HOF指令，API依赖也已核实。见[SGSR1真实编译](ANDROID_SGSR1_ACTUAL_COMPILE_RESULT_2026-10-28.zh-CN.md)与[QCOM HOF-v2真实编译](ANDROID_QCOM_HOF_V2_ACTUAL_COMPILE_2026-10-28.zh-CN.md)。这些独立阶段不改写下文最初调研的执行范围，也仍不等于已经完成真机过滤、游戏画质或性能验证。

## 芯片与功能资格

本机既有记录 `.local/logs/qualcomm-new-device-inventory.txt` 明确为 V2528A、SM8635；当前原生运行日志为 Turnip Adreno 735。高通8s Gen3产品简报列出 SGSR、Frame Motion Engine 2.0 和 Game Post Processing Accelerator，不能把这三项混成同一种超分功能。[官方产品简报](https://docs.qualcomm.com/bundle/publicresource/87-73942-1_REV_C_Snapdragon_8s_Gen_3_Mobile_Platform_Product_Brief.pdf)

| 路线 | 实质、输入 | 本机判断 |
|---|---|---|
| SGSR1 | 空间上采样与锐化，单个fragment pass；低分辨率颜色图、尺寸/UV即可 | 最容易试；普通Vulkan shader实现，不依赖私有二进制或固定功能超分开关 |
| SGSR2 | 时间超分；两pass FS为官方推荐变体，也有CS变体；需要颜色、深度、运动、jitter、历史及相机/曝光等参数合同 | 本机可作为shader算法候选，尚无真实SGSR执行验证；困难在于准确输入和接入点，产品表未单独认证“SGSR2”版本 |
| Adreno Neural Fusion | 新的神经时间超分/插帧SDK，使用新硬件；需要Vulkan运行时支持查询及时间输入 | 官方要求8 Elite Gen6及以上，8s Gen3不在支持列表；不把本机NPU存在当成等价支持 |

SGSR1/2公开源代码为BSD-3-Clause；ANF的公开头文件为BSD，而运行库另用QTI二进制许可。不能因ANF仓库公开就称其运行库及模型全开源。[SGSR官方仓库](https://github.com/SnapdragonGameStudios/snapdragon-gsr)、[ANF官方要求与许可](https://github.com/SnapdragonGameStudios/adreno-neural-fusion)

还有一条真正的硬件滤波路径：`VK_QCOM_image_processing` 暴露纹理单元的大核加权/box过滤及block matching。精确12b8源码中，FD735继承 `a7xx_gen2.has_image_processing=True`，Turnip据此广告该扩展；仍须由根在同一实际driver查询extension/features/format，源码资格不冒充真机启用。它可做上采样滤波，但不是完整SGSR自适应算法的开关，预设权重表也不能直接替代SGSR随图像变化的权重。[高通硬件滤波说明](https://www.qualcomm.com/developer/blog/2024/02/high-order-filtering-and-block-matching-new-image-processing-extension-vulkan-optimizes)

新 `VK_QCOM_image_processing3` 增加适用于超分/锐化的特定gather模式；当前12b8的Turnip扩展广告中没有它，不能因本机有image_processing第一版就认定第三版可用。[Khronos扩展说明](https://docs.vulkan.org/features/latest/features/proposals/VK_QCOM_image_processing3.html)

## 现在的出图路径与接入位置

`dm/host/hle_oe.c:145` 已沿用原游戏缩放：3D目标按scale重建，UI与Final Result保持输出尺寸，末端游戏自己的 `ps_upsample_p` 放大3D。正常手机默认UI原生、3D初始0.75；根当前性能对照另固定1968×900、3D1673×765（0.85），两者不能混称手机此刻的实际画质。

原 `ps_upsample_p` 源身份为 `52aac1f2306b8fc5`。官方 `spirv-cross` 反编译显示：它有五个颜色采样及原锐化系数，并另采一张纹理的x通道作最终乘数。**替换其过滤部分时必须保留原UV钳位、乘数和颜色合同，不能把整条pass粗暴删掉。** 本次没有依据纹理名字臆断第二张图的含义；原pass实际附件、色域及UI先后还需用现有完整调用记录准确桥接。

当前通用present在 `dm/gfx/vulkan/nvn_vk_window.c:679` 使用线性blit；已有实验 `nvn_vk_present_shader.inc` 仅允许同尺寸RGBA8，并用texelFetch，尚不支持缩放。这一层收到的是原游戏最终合成图，通常已含UI，也已经做过内部3D放大。直接在此接SGSR1可以作低风险、通用的**最终显示滤波实验**，但会同时处理文字/UI，不能据此宣称已把3D低分辨率源交给SGSR，更不能保证补回之前丢失的细节。

推荐分两步：

1. 先用GPU上的原低分辨率3D颜色图，对比原upsample与SGSR1；源图到输出保持一次上采样，UI在之后原尺寸合成。无CPU回读、无相机状态猜测。先核对真正的3D/UI边界及SDR颜色合同，SGSR1公开shader会clamp到0..1并强制alpha=1，不能原样喂未tone-map的HDR或需保留alpha的附件。
2. 若输入边界尚未证明，可先作显式关闭默认的通用present小实验，验证SGSR1在本机的成本、锐化和文字副作用；它只是显示滤波，作为独立诊断保留。理想的3D/UI间替换必须单独声明为可选画质功能，遵守当前通用NVN与游戏专用分支分离约束，不能暗中按场景、尺寸或某个key替换任意游戏pass。

SGSR1不需要历史，后台恢复与分辨率变化只需更新常量、目标及依赖；可复用现有Vulkan sampler/descriptor/pipeline、资源退休与异步完成机制。SGSR2则需要真实dynamic-object motion、camera reprojection、jitter、reset、reverse-Z/Y轴和history生命周期。已有可选 `rt_*` 相机/历史颜色深度不等于已经具备完整动态物体motion合同；不能用全零motion、只靠相机重投影或final含UI图冒充完整集成。[SGSR1输入与UI建议](https://github.com/SnapdragonGameStudios/snapdragon-gsr/blob/main/sgsr/v1/README.md)、[SGSR2完整输入合同](https://github.com/SnapdragonGameStudios/snapdragon-gsr/blob/main/sgsr/v2/README.md)

## 最短画面与性能实验

固定游戏、主输出和交换链1968×900，先保留当前0.85原过滤作锚点；以同0.85的SGSR1区分“只换过滤”的效果，再对照0.75、0.70。预期3D尺寸分别1673×765、1476×675、约1378×630，最终以真实创建日志为准。从0.85到0.75只表示3D像素面积约少22.1%，不表示整帧必快22.1%。原生1.0作短暂质量参照，不拿它的帧率混入同负载对照。

在营地石砖、草木、怪物轮廓、亮色技能、地牢暗部和完整菜单/文字中，分别观察静止细节与持续移动：锐化光晕、边缘锯齿/闪烁、噪点、颜色偏差、alpha和UI。SGSR2另外检查鬼影、遮挡显露、转场/传送/暂停后的history reset。使用原厂Profile看整主GPU批次、增加pass的读写和停顿，复用整轮成本比较；图像用现成抓帧/录屏工具，不做CPU版重采样来伪造GPU效果。

官方SGSR1/2文档中的耗时来自其他芯片、最大频率及不同输入/输出，不能移植为本机成本。现有SDP频率字段不是独立物理OPP证据，不能据它假定GPU固定约500MHz再缩放官方耗时。SGSR2的history与附加pass会增加驻留和带宽；SGSR1也会增加过滤采样，收益须来自替代原过滤及减少主3D负载，而不是假定超分本身免费。[SGSR2官方性能上下文](https://www.qualcomm.com/developer/blog/2024/10/introducing-snapdragon-game-super-resolution-2)

工程量估计：通用present开关原型约1–2个工作日；证明3D/UI边界并完成保留原pass副作用的SGSR1接入、移动画面和热稳定验证约3–5个工作日。SGSR2在输入齐备后仍需额外数日；当前未证实motion/jitter与history，不能给总工期承诺。以上是代码范围估计，不是已完成进度。

## 本轮实际产物

官方SGSR源码已固定到commit `d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e`，下载在 `.local/research/snapdragon-super-resolution-v1/`。独立主机编译已验证两种SGSR1 fragment与SGSR2两pass-FS的vertex/convert/upscale，共5份SPIR-V通过Vulkan1.1校验。仅做严格GLSL语言适配：SGSR1启用官方UBO开关、固定原不变的mode为const并去除表达式constructor的非法precision写法；SGSR2 vertex补原texCoord的location。原始首次编译失败、全部适配及源码/工具/产物SHA保留在 `.local/logs/snapdragon-super-resolution-research-v1/`。

这些通过只说明shader语言与SPIR-V可用，未创建真机pipeline、未构建完整官方样例APP、未测游戏画质/性能，也未修改生产Shader、驱动、游戏binary或用户设置。
