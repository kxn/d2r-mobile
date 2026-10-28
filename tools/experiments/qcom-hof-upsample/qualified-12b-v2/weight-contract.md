# 与实际 12b 配对的资源合同

仅资格于 SM8635 / Adreno 735 和 SHA `12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5` 驱动；不是通用 Vulkan/QCOM 坐标结论。

- app/physical API 至少 1.3，明确启用 `VK_QCOM_image_processing` 和 `textureSampleWeighted`；查询实际 source 的 WEIGHT_SAMPLED 和权重格式 WEIGHT_IMAGE 支持。
- binding 4 是原 R11G11B10 source 的专用 IMAGE_PROCESSING sampler/view；原 binding 1 留给完整原五 tap fallback。binding 5 是 R16_SFLOAT 4×4、1024 array layers 的 SAMPLE_WEIGHT view，filterCenter=(1,1)，numPhases=1024。reductionMode=WEIGHTED_AVERAGE；不得把 MIN/MAX 的 0/1 权重限制混进此路径。
- phase grid P=32。layer = v*32+h。实测配对 shader 使用 q=uv（不另减半 texel），表代表点 tx=h/32、ty=v/32。每轴 Catmull-Rom 权重为 `[-.5t+t²-.5t³, 1-2.5t²+1.5t³, .5t+2t²-1.5t³, -.5t²+.5t³]`；二维为 y/x 外积，依 layer/y/x 排列，以 IEEE little-endian binary16 保存，共 32768 字节。这正是原 `qcom-hof-offscreen-v2/prepare.py` 生成的数据，不做额外归一化。
- 独立 one-hot / phase barcode 证明的是该驱动的 tap origin 与选定 non-tie phase；不是只比较两个同源 Catmull 公式。实测支持 firsttap=floor(U-.5)-1、nearest phase，而原规范推导的候选在 v1 失败。全 1024 phase / ties / phase wrap 全域尚未穷尽；不得将当前应用修正称为所有驱动的规范实现。
- 原 C7 scale/clamp 与 raw gainUV 保留，gain 在滤波后乘，alpha=1，不在 gain 前 clamp 到 1。边缘三 texel strip 或非有限坐标走完整原五 tap；候选滤波非有限也回原实现。原 C7.k 内部锐化由固定 Catmull 核替换，是显式画质选择。
- 静态 .85 输入使用真实同一 trim source/gain/C7；source linear+clamp-edge，gain nearest+repeat，与真实历史 sampler 的有效 LOD0 行为一致。只读一 mip，因此 fixture 的 maxLOD0/nearest mip 与原 maxLOD1000/linear mip 对 source 没有实际采样差异；不是逐字段 CreateInfo 相同。

当前源码没有接入通用 NVN 后端，没有修改游戏 binary/玩法，也没有设置默认运行时开关。
