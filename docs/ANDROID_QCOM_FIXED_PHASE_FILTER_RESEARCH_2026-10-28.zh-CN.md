# QCOM固定相位硬件放大：规范可行性（2026-10-28）

**值得做一个独立硬件过滤实验，首先验证4×4 Catmull-Rom。** 现有Adreno735／accepted12b查询已经证实第一版 `VK_QCOM_image_processing`、加权采样feature、R16_SFLOAT权重图及R11G11B10输入格式的资格。固定相位小核可由纹理单元完成，保留原末尾gain；它是空间过滤，不是完整SGSR、时间超分或AI重建。本轮只读规范与已有数据，没有启用扩展、编译新PSO、绘制或触碰手机，收益尚未测量。

## 已有资格与实际缺口

复用[实机能力查询](ANDROID_SGSR1_ACTUAL_COMPILE_RESULT_2026-10-28.zh-CN.md)：`textureSampleWeighted=true`，最大核64×64、最大总相位1024；R16_SFLOAT有 `SAMPLE_WEIGHT_IMAGE`，B10G11R11_UFLOAT有 `WEIGHT_SAMPLED_IMAGE`，对应image-format查询成功。**1024是32×32总相位，不是每轴1024。** QCOM3未广告，本方案只用QCOM1。

此前探针仅查询，没有在device创建时启用扩展/feature。真实12b源码把 `nir_texop_sample_weighted_qcom` 映射到 `OPC_IMG_BINDLESS_HOF`，且权重view的尺寸、中心与相位进入FDL描述符；这是实现路径证据，仍不能代替实际shader编译、指令检查及输出验证。

当前0.85原pass的C7与两个输入descriptor[仍有明确缺页](ANDROID_SGSR1_CURRENT085_EXISTING_PAGES_2026-10-28.zh-CN.md)。后续必要采集补齐前，不能用历史1.0常量声称当前3D边界已经完全接通；独立硬件算子资格可以先做。

## 核、颜色与归一化

| 问题 | 规范结论与实现边界 |
|---|---|
| 负权重 | 加权模式执行原始乘加和；可选有符号R16_SFLOAT权重，表达Catmull-Rom或Lanczos负瓣。R8_UNORM不能表达负权。MIN/MAX模式只接受0/1权重，不适用。 |
| 自动归一化 | 没有除以权重和的隐含操作。离线/初始化时逐相位归一化，并检查R16量化后的和；不能假设量化后精确为1。 |
| HDR与gain | 原始加权和允许负数及大于1结果。R11G11B10浮点输入不应先夹到1；保留原rawUV的第二张gain采样和末尾乘法，最后alpha仍为1。不能把最终RGBA8的饱和提前到gain之前。 |
| 内部精度 | R16是权重存储格式，不证明累加器为FP16或FP32。GLSL结果写highp也不证明纹理单元内部精度；须验证常量、负瓣、极值及增益后的输出。 |
| 自适应 | 固定相位权重不依赖邻域颜色，不能复现SGSR的内容自适应边缘判定/权重；也不能原样等价原C7.k的五tap锐化。属于可选画质策略。 |

原始和、phase选择及权重布局以[Khronos纹理规范](https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/chapters/textures.adoc)为准；[官方扩展说明](https://docs.vulkan.org/features/latest/features/proposals/VK_QCOM_image_processing.html)明确说明此类高阶过滤在纹理单元完成，且权重不要求限制输出到0…1。上述结论不等于已证明全HDR范围数值安全。

## 最容易接错的是坐标与相位

设原采样坐标转换成texel单位为 `u`，核中心整数为 `C`。规范的首tap是 `floor(u-C)`，相位来自 `floor(frac(u-0.5)*P)`；P为每轴相位数。因此直接套通常围绕 `floor(u-0.5)` 的四tap cubic权重、仍传原u，会在半texel两侧选错核位置。

一个可推导的4tap方案：仅硬件过滤坐标改为 `q=u-0.5`，取C=1，则首tap严格为 `floor(u-0.5)-1`。硬件相位此时由 `frac(u)` 选择，权重表相应按 `t=frac(phaseFraction+0.5)` 生成；二维分别推导再做外积。**这是配套坐标/权重变换，不是把整幅图向左上挪半像素。** 相位代表点、R16量化与texel边界仍须用冲激/斜坡实际验证，不把推导当硬件输出通过。

P只能取允许的2次幂，1024总相位最多P=32；规范按相位索引选表，不替相邻phase插值。低比例移动画面可能有相位量化闪烁。先证明相位与DC正确，再比较清晰度，不能只看静态截图。参数限制见[权重view规范](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewSampleWeightCreateInfoQCOM.html)。

原C7的scale、min/max顺序、逐tap NaN合同与rawUV增益坐标必须继续保留。`CLAMP_TO_EDGE`只约束整张VkImage，不能代替C7有效子矩形；小核越过有效子矩形时保守走原完整fallback，不允许读取无效边缘来假装更锐。

## 资源与shader的最小变化

第一轮选2D_ARRAY 4×4、32×32相位、R16_SFLOAT权重图：逻辑权重数据32KiB，一次上传后跨帧只读。Lanczos2同为4×4，Lanczos3的6×6为72KiB；这些只是texel载荷，不是VkMemory实际分配大小。先用2D表避免把1D separable权重打包/GLSL接口问题混入第一轮；规范支持可分离过滤，后续再单独核准。

权重图需要 `VK_IMAGE_USAGE_SAMPLE_WEIGHT_BIT_QCOM`、独立权重view及 `VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM`，单mip/单sample/identity swizzle。不能因为加权采样给原HDR输入添加STORAGE或SAMPLE_WEIGHT用途；原输入继续SAMPLED，只需格式和view符合资格。约束见[VkImageViewCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html)及[descriptor类型](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorType.html)。

硬件源采样需专用 `VK_SAMPLER_CREATE_IMAGE_PROCESSING_BIT_QCOM` sampler，nearest min/mag/mipmap、LOD0、无anisotropy/compare；边界选CLAMP_TO_EDGE，或CLAMP_TO_BORDER+FLOAT_TRANSPARENT_BLACK。旧提案里的OPAQUE_BLACK不能当现行有效性规则。该sampler不能供普通sample/gather使用，因此原fallback与gain保留普通sampler，增加硬件源descriptor及权重descriptor/布局，而不是原地更换所有sampler。[现行sampler规范](https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/chapters/samplers.adoc)

在device创建时启用QCOM1及 `textureSampleWeighted`；shader使用 `GL_QCOM_image_processing`／`OpImageSampleWeightedQCOM`，正确权重装饰。SPIR-V扩展最低1.4，不能直接沿用SGSR-v2原先Vulkan1.1／SPIR-V1.3目标。**API层另有依赖：QCOM1要求显式启用 `VK_KHR_format_feature_flags2`，或应用声明并使用Vulkan1.3；只生成Vulkan1.2 shader不能满足这条API依赖。** Vulkan1.2/SPIR-V1.5输入可保留，独立应用选API1.3或同时启用依赖扩展。[GLSL声明](https://raw.githubusercontent.com/KhronosGroup/GLSL/main/extensions/qcom/GLSL_QCOM_image_processing.txt)、[SPIR-V依赖与限制](https://github.khronos.org/SPIRV-Registry/extensions/QCOM/SPV_QCOM_image_processing.html)、[API扩展依赖](https://docs.vulkan.org/refpages/latest/refpages/source/VK_QCOM_image_processing.html)

## 与SGSR候选比较，下一轮实际要证明什么

原SGSR-v2内部分支有4个gather及颜色/权重/归一化依赖；固定核可将这段交给一次硬件加权采样，再做原gain，理论上减少shader算术与纹理往返依赖。它仍读取多个源texel和权重；4×4并不保证比原五次bilinear或SGSR少实际DRAM流量，也不保证更快。新成本是权重资源、描述符及专用sampler、核采样/phase访问和可选fallback。应只在既有upsample专用候选入口做，不改变通用NVN语义或最终UI合成。[高通对该硬件路径的说明](https://www.qualcomm.com/developer/blog/2024/02/high-order-filtering-and-block-matching-new-image-processing-extension-vulkan-optimizes)

下一步复用现有独立fixture，先完成算子资格：启用feature，编译并核准真实HOF指令；用同一小组确定输入测试常量、负瓣冲激、斜坡、半texel/phase边界、HDR>1与末尾gain，不把低分辨率人工缩图当游戏画质。与普通GPU shader的同核参考比较，使用现成导出/验证工具，不新增计时器。通过后，补齐当前0.85真实C7/descriptor，再以实际3D输出单独比较原5tap、硬件4×4、SGSR；保持游戏/3D/swapchain尺寸、gain、UI、驱动、场景与温度条件一致，最后用已有GPU Profile核验整帧和带宽成本。当前没有承诺降比例后能涨多少帧。

本轮来源快照、SHA及精确12b源文件映射在 `.local/logs/qcom-weighted-filter-research-v1/receipt.json`；没有新建硬件测试框架或修改生产shader。
