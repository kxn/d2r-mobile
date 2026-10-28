# 原3D放大入口的QCOM HOF实验源码

归档已在Adreno735／accepted12b上、正确声明Vulkan1.3与QCOM1加权采样feature后实际编译的准确GLSL。只保护源码，不接入生产、不增加运行时开关，不含SPIR-V、APK、driver或capture。

- `qcom-hof-catmull-rom.frag`：真实输入源码，包含原完整fallback和末尾gain；HOF部分需要后续创建合法权重资源，当前没有执行过滤。
- `source-receipt.json`：官方接口、原fallback、实际源码/机器码及v2实机证据的来源映射。
- `weight-contract.md`：未来4×4权重view、相位、坐标与sampler合同，不是已创建或验证的权重表。
- `NOTICE.md`／`LICENSE.sgsr`：来源与BSD版权记录；本HOF适配不是高通官方SGSR实现。

从仓库根目录运行现成工具：

```bash
python3 tools/experiments/qcom-hof-upsample/rebuild.py
```

入口只在新的 `.local/build/qcom-hof-upsample-rebuild/` 编译/校验，要求精确得到实机使用过的输入SHA：

```text
a02afd949f55e48af8142dfe3bdf6cf75a063d531bd28cdb7e0c3a09d5a20b60
```

原始编译目标保持Vulkan1.2以重建相同SPIR-V1.5，再分别用现成 `spirv-val` 验证Vulkan1.2和1.3。**shader编译目标不替代应用API依赖声明**：实际应用必须声明API1.3，或API1.2同时启用QCOM1所依赖的flags2扩展。此次成功v2使用API1.3及物理设备≥1.3门。主游戏API未改变。

等价命令，`OUTPUT`需为新建输出目录：

```bash
glslangValidator -V --target-env vulkan1.2 -S frag -o OUTPUT/candidate.spv tools/experiments/qcom-hof-upsample/qcom-hof-catmull-rom.frag
spirv-val --target-env vulkan1.2 OUTPUT/candidate.spv
spirv-val --target-env vulkan1.3 OUTPUT/candidate.spv
```

准确重建核验使用glslang15.2.0、SPIRV-Tools2025.1。工具不同可能输出不同但合法SPIR-V，入口保存失败与版本，不冒称已经实机编译。它不安装、启动或操作设备，也不创建Vulkan对象。

实机native出现一条 `img_bindless.hof.base1 (f32)(xyz)`；原FS仍与此前同一机器码。只说明编译路径成立，没有权重图/descriptor/sampler、没有draw，不能称已经完成4×4 Catmull-Rom过滤、当前0.85画质或性能验证。见[实际v2结果](../../../docs/ANDROID_QCOM_HOF_V2_ACTUAL_COMPILE_2026-10-28.zh-CN.md)。v1曾漏API依赖的失败记录保留，不重标。
