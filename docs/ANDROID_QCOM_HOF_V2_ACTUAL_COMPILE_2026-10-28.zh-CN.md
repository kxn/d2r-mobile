# QCOM HOF-v2：API依赖闭合后的真实编译（2026-10-28）

**v2已补齐QCOM1的API依赖并真实创建两PSO，HOF专用机器码与v1逐字相同。** v1的API声明漏项与FAIL记录保留；本次是新的独立运行，不改写旧结果。仍没有权重图、sampler、descriptor写入、绘制、GPU时间或画质结果。

## 准入修正与实际运行

v2仅将独立native fixture的两个API常量从1.2改为1.3：应用 `apiVersion` 与物理设备最低版本检查。75项host检查核准其余源码、Java、manifest、loader、queries、descriptor/pipeline布局及三个shader payload均与v1逐字相同；没有重新烘焙shader或修改主游戏API。准确API依赖为QCOM1需flags2扩展或core1.3；v2选择core1.3。[QCOM1依赖规范](https://docs.vulkan.org/refpages/latest/refpages/source/VK_QCOM_image_processing.html)

实际 `.local/logs/qcom-hof-compile-qualification-v2/ACTUAL-COMPILE1/raw-compile.log`：

```text
HOFCOMPILE_ENABLED api=4206592 QCOM_image_processing=1 textureSampleWeighted=1 textureBoxFilter=0 textureBlockMatch=0 weighted_operations_not_executed=1
HOFCOMPILE_CREATED index=0 label=original-current-cf83
HOFCOMPILE_CREATED index=1 label=QCOM1-HOF-CatmullRom-weight-contract
HOFCOMPILE_TERMINAL pipelines=2 queue_submits=0 game_modules_executed=0 no_timing=1
```

4206592即Vulkan1.3。冻结native源码在device创建前明确拒绝physical API<1.3，实际执行通过该门并创建device。QCOM1与weighted feature显式启用，box/block关闭。实际普通UID10376、PID20089；两PSO完成后停止并卸载独立 `org.kxn.hofcompile`，main_before/main_after的APK及六个锚点相同、cleanup_errors为空。root独占设备执行，本次分析只读已有结果。

因此v1遗漏的**API依赖声明资格已闭合**；未运行验证层，不把这一项闭合扩大成所有Vulkan调用已获验证层检查。

## 字节与机器码连续性

| 输入/产物 | v2实际SHA256 |
|---|---|
| 独立APK | `d22af5eb4a399ccc9e4b49c9584472f5e9d37f4f76138c77d41829e26c9d5c24` |
| accepted12b driver | `12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5` |
| 当前cf83原FS | `e114e05ccbf91820d9089932f65a87fda490b78ad28647e99fb584d6e578fc1a` |
| 当前cf83原VS | `0f96df4903498af6eda45a67fe8d07fa876d7e30624f3f962379e354fa013378` |
| HOF候选SPIR-V | `a02afd949f55e48af8142dfe3bdf6cf75a063d531bd28cdb7e0c3a09d5a20b60` |
| 完整Mesa NIR/IR3文本 | `bbc9c9632eeacb13036cc6332d238d1322644259e777bf7009d55598c6a6a9d4` |

完整raw-ir3与v1的SHA相同，逐字节相等：原FS native仍为 `d527ec86ab7f21767da039a866fa7b9c478748d1d358b9ec2444ab0101e5180e`，候选为 `fda94dee3ddb820661fd3edae7c97f60ccc216addc59880a4aae42e162c9e293`。候选确有一条 `img_bindless.hof.base1 (f32)(xyz)`，没有退化成shader内逐tap或gather模拟。

静态统计保持原82/67非NOP、候选225/189；两者6 full、16 max_waves，候选constlen12。两份互斥原fallback仍产生10个普通saml站点，不能加总为每像素采样；成功分支HOF之后保留原gain。具体静态边界见[v1机器码分析](ANDROID_QCOM_HOF_ACTUAL_COMPILE_2026-10-28.zh-CN.md)。**API修正没改变机器码不等于已证明过滤效果、带宽或帧率。**

## 可重建源码与仍未完成的验证

准确输入已保存为 `tools/experiments/qcom-hof-upsample/`，含GLSL、现成glslang/spirv-val重建入口、来源/版权收条及未来weight view/phase合同。host重建得到相同a02afd94输入，并分别通过Vulkan1.2和1.3的官方spirv-val；原编译目标1.2仅用于保持SPIR-V1.5字节，不替代实际应用API1.3依赖。归档不含SPV/APK/driver或大capture。

本轮没有建立4×4/32×32phase/R16权重资源，所谓Catmull-Rom是下一步实际view与权重的必要合同，尚未采样。硬件内部累加精度、R16量化、phase量化闪烁、负瓣/HDR>1、当前0.85的真实C7/descriptor缺页、画质与净GPU成本仍待验证。没有新增绘制框架、timer、shader运行时开关或生产修改。

现成Mesa文本整理器输出在 `ACTUAL-COMPILE1-analysis/comparison.json`；本次门与前后身份核验在 `qualification-summary.json`，源码与结果SHA收条在 `receipt.json`。v1 frozen、APK、actual result和FAIL summary均单独保留。
