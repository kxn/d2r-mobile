# QCOM硬件过滤：真实编译结果及API依赖漏项（2026-10-28）

**accepted12b在Adreno735上确实为候选生成了 `img_bindless.hof` 专用机器指令。** 原FS与候选两PSO均创建成功，原FS native hash与之前完全相同；没有执行采样、创建权重图或绘制，不能称4×4放大、画质或性能通过。另发现探针遗漏API扩展依赖声明，须在进入任何图像实验前补齐；本次仅确立驱动实际接受并编译HOF，完整规范准入不通过。

## 实际运行与精确输入

记录目录为 `.local/logs/qcom-hof-compile-qualification-v1/ACTUAL-COMPILE1/`。root统一执行；本代理只准备host输入及离线整理。独立包 `org.kxn.hofcompile`，普通UID10374、PID16440；结束后已停止并卸载，主游戏APK、profile、shader链、driver、资源及游戏模块锚点前后相同，cleanup_errors为空。

| 输入 | 实际SHA256 |
|---|---|
| accepted12b driver | `12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5` |
| 当前cf83原FS | `e114e05ccbf91820d9089932f65a87fda490b78ad28647e99fb584d6e578fc1a` |
| 当前cf83原VS | `0f96df4903498af6eda45a67fe8d07fa876d7e30624f3f962379e354fa013378` |
| HOF候选FS | `a02afd949f55e48af8142dfe3bdf6cf75a063d531bd28cdb7e0c3a09d5a20b60` |

原/候选都启用QCOM1加权采样feature，日志 `HOFCOMPILE_ENABLED` 明确打印 `textureSampleWeighted=1`、box/block=0。这比此前SGSR探针仅查询feature更进一步；但不能消除下述依赖漏项。原VS/FS是当前cf83字节，fixed graphics接口继续复用旧实际pipeline23282，不宣称完整重现当前全部PSO状态。

安装包实际label为 `D2R Mobile`，独立package为 `org.kxn.hofcompile`；aapt已核准该标题与既有installer的精确标题门一致。没有再次触发之前SGSR的标题失配。host-check的58项通过是输入/范围检查，不是完整Vulkan合法性证明。

## 官方NIR／native证据

官方编译日志 `raw-ir3.log` 第442行保留 `(float32)sample_weighted_qcom`；第705行实际生成：

```text
img_bindless.hof.base1 (f32)(xyz)r2.z, r5.x, t#4, a1.x
```

因此是硬件高阶过滤指令，未被转成四个普通gather或shader内逐tap乘加。宿主SPIR-V由现成glslang生成，并通过spirv-val和spirv-cross反射，包含 `TextureSampleWeightedQCOM`、权重装饰及一条加权采样指令；没有自行造编译器或汇编器。

| Mesa静态输出 | 当前原FS | HOF候选 | 之前SGSR-v2，仅供结构参考 |
|---|---:|---:|---:|
| 指令，含统计NOP | 82 | 225 | 372 |
| 非NOP | 67 | 189 | 312 |
| full register | 6 | 6 | 6 |
| max_waves | 16 | 16 | 16 |
| constlen | 8 | 12 | 12 |
| HOF站点 | 0 | 1 | 0 |
| gather4g站点 | 0 | 0 | 4 |
| mad.f16站点 | 0 | 0 | 58 |

原FS native BLAKE3仍为 `d527ec86ab7f21767da039a866fa7b9c478748d1d358b9ec2444ab0101e5180e`；候选为 `fda94dee3ddb820661fd3edae7c97f60ccc216addc59880a4aae42e162c9e293`。原FS桥接成立，没有因为启用QCOM或API目标改变而换成另一份原shader。

候选完整输出有10个 `saml`、多个gain采样与getsize站点：编译器内联了“坐标资格失败”和“HOF结果非finite”两处原五tap fallback，它们是分支路径。不能将它们加总成每像素10次原采样。成功路径由一条HOF产生RGB，再执行原rawUV gain及alpha1；HOF内部实际tap、权重访问、cache/DRAM事务与耗时仍未知。额外两条宽度2、一条宽度4 UBO LDC位于preamble，不写成每个fragment重复读取外存。

候选NIR颜色结果与native HOF返回为float32，没有SGSR-v2的 `mad.f16` 滤波链；这仍不能证明纹理单元的内部乘加精度、R16权重误差或全HDR数值安全。相同寄存器/waves静态值也不能推断运行占用率、功耗或帧率相同。

## 必须先修正的API依赖声明

**此次准备遗漏了QCOM1的API扩展依赖。** 探针声明应用API1.2，只显式启用QCOM1；而[QCOM1扩展规范](https://docs.vulkan.org/refpages/latest/refpages/source/VK_QCOM_image_processing.html)要求 `VK_KHR_format_feature_flags2` 或Vulkan1.3。物理设备支持1.4不等于声明1.2的应用可自动使用core1.3；[VkApplicationInfo规范](https://docs.vulkan.org/refpages/latest/refpages/source/VkApplicationInfo.html)明确区分这两者。[扩展依赖规则](https://docs.vulkan.org/spec/latest/chapters/extensions.html#extendingvulkan-extensions-extensiondependencies)也要求启用依赖。

所以“vkCreateDevice/两PSO返回成功”只证明12b实际接受并生成了HOF，不能升级为扩展合法性完整通过。host准备与58项检查均漏掉这条依赖；原frozen、APK及实机结果原样保留，不覆盖或重标为合法。没有验证层输出，不伪称已得到验证层通过。

下一次最小修正只涉及独立fixture：应用API及guard升到1.3，保留本次合法的SPIR-V1.5输入；或保留API1.2并明确启用flags2依赖。先复核准确扩展依赖，再重新取得正式编译准入，不改主游戏API。本轮不追加上机、不修改冻结v1；该问题不需要改变硬件算法或驱动。

## 后续图像实验边界

这条候选的“4×4 Catmull-Rom”目前是权重view的必要合同：R16_SFLOAT、2D_ARRAY、center(1,1)、32×32总相位1024，采样坐标减0.5texel并配套相位表；本轮没有创建这些对象、填写descriptor或执行HOF。因此实际native证明的是HOF指令可生成，不能说它已按4×4采样。原C7.k的锐化被固定核替换，属于可选画质策略。

API依赖闭合后，先在既有fixture验证合法权重图/专用sampler、恒色/DC、相位边界、负权重和HDR>1后乘gain；再补齐[当前0.85实际C7/descriptor页](ANDROID_SGSR1_CURRENT085_EXISTING_PAGES_2026-10-28.zh-CN.md)，保持原UV、gain与后续UI，比较真实图像。没有新增GPU框架、timer、性能开关或生产代码。本轮没有GPU Profile、动态请求数、整帧时间或质量结果，不能按225比372的静态指令差推算收益。

离线整理复用现有 `analyze-compile.py`／Mesa输出整理器，产物为 `ACTUAL-COMPILE1-analysis/comparison.json`、`qualification-summary.json` 和 `receipt.json`。研究背景见[固定相位硬件过滤可行性](ANDROID_QCOM_FIXED_PHASE_FILTER_RESEARCH_2026-10-28.zh-CN.md)。
