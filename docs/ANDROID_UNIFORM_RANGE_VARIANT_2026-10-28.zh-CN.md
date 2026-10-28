# DSP3：保留原特化的有范围条件变体

背景：固定112..175B预取的源读取域不等于编译器实际新增读取域。当前12b原生preamble合并覆盖新增byte64..191，故本候选需要192B；不是把source176、descriptor地址对齐、dummy padding或robustBufferAccess当作充分证明。min_bytes是通用协议参数，驱动/优化器扩读形态变化须重新核对，不能硬编码某游戏材质。

本次只扩可选FS `.spec` 协议，原base.spv和游戏CPU binary不变。旧specialization_module完整保留，另有range_module。只有原selector可读、数值规则/variant资格成功、所需UBO逻辑size达到门槛时选择新模块；范围不足或新模块被驱动拒绝退回原特化，原特化也被拒绝才走既有base fallback。候选拒绝不永久关闭旧特化。旧DSP1/2原行为保留。

## 128B封装

全部字段为little-endian uint32。body为完整旧DSP1/2 recipe原字节，随后candidateSPV：

| words | 内容 |
| --- | --- |
|0 /1 | magic `0x33505344` /version1 |
|2 /3 | legacy_recipe_bytes /candidate_spv_words |
|4 /5 /6 | required_binding /required_reg /min_bytes |
|7..14 | 原baseSPV SHA256 |
|15..22 | 完整embedded legacy recipe SHA256 |
|23..30 | candidateSPV SHA256 |
|31 | reserved0 |

loader验证总长度/有界wordcount/三个SHA、legacy原base身份、selector/SpecId/标量类型一致；要求binding/reg在实际最终reflection中唯一kind1/count1，reg3..18、min为正4B倍数且≤65536。旧host因magic拒绝，不会忽略新合同。当前候选binding1/reg13/min192；协议不含shader key或场景判断。

## 热路径和缓存

原Spec0选择成功后只读取 `S.ubo[1][required_reg-3].size` 作O(1)判断，无新CPU游戏内存读、扫描、hash、等待或epoch体系。direct Vulkan descriptor的范围为min(NVNsize,65536)，因此逻辑size下界足够；短范围dummy/staging即使物理更大仍保守保留旧特化。DSP3的hybrid入口沿原Spec0+range选择；原`.pred`仍按embedded legacy recipe哈希鉴权，DSP1/2 hybrid行为不变。

当前draw先pick最终module，再进入prepared/prefix/完整PSO缓存，module身份已包含在原key中。BindUBO、CALL与完整S restore/ghost/递归draw都重新经过pick；没有跨drawguard缓存。预热只编译不执行，可先编译range模块，但实际draw仍按当前size选择。prewarm新kind4与pixel-local新kind6属于**不同编号空间**；PL kind2从envelope内legacy rawSPV提取，kind6取candidate，保持独立来源SHA。texel/PL浅拷贝明确清range字段，不能借原module的许可。DSP3暂保守拒绝quad-transfer/quad-readonly元数据，当前两个目标flags0；未来需另证，不能直接继承。

既有五artifact fingerprint已含完整.spec，所以旧prewarm及dkey/dspan/plocal证明不能按旧身份复用。新闭包必须用原构建规则重新生成，覆盖embedded legacy与candidate；不能把DSP3按旧60B header偏移误解析。

## 内部选择计数的工具边界

原厂Realtime是全局GPU数据，成功PSO记录可能来自预热，均不能回答某时间窗宿主range guard是否实际选中新模块。因此仅补内部语义计数：存在range_module、原selector成功admit后的 `range_selected` / `range_fallback`。它们是**selection attempts，包含draw retry；不是GPU invocation、已绘制draw或时间**。

计数在记录owner本地累加，只沿原uniform_predicate_publish提交边界发布atomic；既有1Hz game-state的uniform_specialization对象追加两字段。每draw无新atomic/日志/采样器/计时器。BASE没有range模块时两项为0；候选正式窗应核selected增量>0，并保留fallback与原guest/Surface分母，不用PSO loaded日志代替。

## Host验证范围

现成Linux NVN/Vulkan production loader、原编码器/回放、Lavapipe Core+SYNC validation：

- generic range192/176/188/0/大size及恢复，原selector值不变；state restore、hybrid/observer、prewarm kind4与原fingerprint拒绝。
- 自有shader的真实GPU像素验证、同submission GPU更新/不可读回base、bounded variants、完整PSO和warm同selector范围切换。
- 实际vkCreateGraphicsPipelines故障注入：range→旧特化，以及range→旧特化→base，旧特化不被前者disable。
- PL原五source兼容+独立kind6、texel/PL range字段不浅继承。
- 强封装损坏拒绝，包括有效SPIRV更改SpecId并重算candidate hash、非法binding/reg/min/长度/保留位/各SHA。

源入口：`dm/gfx/vulkan/tests/build_uniform_range_fixture.py`在现成owned GLSL fixture上生成输入；GPU代码只为module/Spec0/range合同验证，candidate使用owned legacy SPV，不冒充真实游戏预取效果。设备真实prefetch/native指令及整帧收益由后续独立候选对照给出。原prefix热路径另用现成10case夹具回归；UBO小夹具若未命中prefix，日志保留0，不冒称热分支覆盖。

完整host命令/输入/二进制/源码SHA与失败保留：`.local/logs/dsp3-range-contract-v1/`。本工作不操作ADB、不安装、不构建主APK；探针/软件GPU验证不能替代真实游戏里程碑。
