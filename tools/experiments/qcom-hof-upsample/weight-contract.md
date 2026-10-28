# 未来权重资源与相位合同

本文件描述候选需要的资源，**此次探针未创建或使用这些资源**。编译HOF不验证核大小、权重、精度、画质或性能。

1. set1/binding1继续是原普通source sampler；binding2为原gain；binding3保留原C7。新增binding4指向与binding1相同实际image/mip，但使用专用 `VK_SAMPLER_CREATE_IMAGE_PROCESSING_BIT_QCOM` sampler；binding5必须是 `VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM`。
2. 权重view为R16_SFLOAT、2D_ARRAY、单mip/单sample/identity swizzle，image有 `VK_IMAGE_USAGE_SAMPLE_WEIGHT_BIT_QCOM`，view带 `VkImageViewSampleWeightCreateInfoQCOM`：filterSize=(4,4)、filterCenter=(1,1)、numPhases=1024，layerCount≥1024。逻辑载荷4×4×1024×2=32KiB，不是实际分配大小。不要给原HDR source添加STORAGE或SAMPLE_WEIGHT用途。
3. QCOM sampler min/mag/mipmap均NEAREST、LOD0、无anisotropy/compare；选CLAMP_TO_EDGE，或CLAMP_TO_BORDER且FLOAT_TRANSPARENT_BLACK。不能将它用于普通texture/gather。增益与fallback继续用普通sampler。
4. P=32是每轴相位数，总相位P²=1024，layer=vPhase×P+hPhase。原映射坐标texel单位u，硬件坐标q=u−0.5，center=1，于是首tap=floor(u−0.5)−1。硬件相位来自frac(q−0.5)=frac(u)，因此h相位代表点x=(h+0.5)/P对应通常cubic的t=frac(x+0.5)。二维分别推导、按相位外积。不能省略配套坐标偏移或用未经重映射的通常cubic相位表。
5. 对索引[-1,0,1,2]，Catmull-Rom权重可按以下多项式生成，二维权重为wx[i]×wy[j]：

```text
w[-1] = -0.5t + t² - 0.5t³
w[ 0] = 1 - 2.5t² + 1.5t³
w[ 1] = 0.5t + 2t² - 1.5t³
w[ 2] = -0.5t² + 0.5t³
```

每相位浮点归一化再量化R16；须检查量化后的和、负瓣、DC及相位边界，不能假设量化后精确为1。硬件只执行原始加权和，不替应用归一化。相位代表点是候选画质策略，移动闪烁仍需实际图像验证。

候选继续保留C7原scale/min/max、NaN处理与rawUV增益；内部采样在末尾才乘gain，不先截断上界1，alpha1。3texel保守有效子矩形边缘或非finite资格失败时回原完整5tap；HOF结果非finite也回原路径。固定核替换内部C7.k锐化，不能声称与原算法等价。硬件CLAMP_TO_EDGE只约束整张图，不能替代C7有效子矩形。

后续必须先启用合法QCOM1/weighted feature并满足API依赖，再用现成fixture/工具核权重view、负权/HDR>1后乘gain、常量/冲激/斜坡及phase移动边界。当前0.85真实C7和descriptor仍有捕获缺页，不能填入历史1.0常量冒充。禁止用人工CPU缩图代替实际游戏低res出图，不新增timer或GPU框架。

规范来源：[加权采样与相位](https://docs.vulkan.org/spec/latest/chapters/textures.html#textures-weightimage)、[weight view](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewSampleWeightCreateInfoQCOM.html)、[API依赖](https://docs.vulkan.org/refpages/latest/refpages/source/VK_QCOM_image_processing.html)。
