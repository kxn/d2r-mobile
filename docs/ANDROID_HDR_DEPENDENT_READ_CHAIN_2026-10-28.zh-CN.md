# 当前 HDR 主体读取链与下一项驱动编译实验

本轮审查当前实际使用的两个已交付HDR FS与HDR10：链条仍是 **每fragment查列表头→循环内读列表索引→读取Buffer记录→部分分支再读UBO**。已交付的末端UBO合并保留；未发现能直接删除的一批重复同址读取，也没有发现这里的多分量Buffer读取被翻译器拆成scalar。下一项值得独立核验的是：只读structured/SSBO数据是否仍占用texture读取路径，能否在同一驱动中以合法的UAV/LDIB路径交付。

这不是宣告该路径更快。先用已有PSO上下文核原生指令确实换路、寄存器/robust/别名合同，再进行独立性能对照；不与HDR10合宽混做，不以Volatile强迫代码生成。

## 输入与可复核范围

复用一次已采完并停机的c185/ee1f/cf83/12b8营地诊断。3个完整49RP CB为10923–10925，147个RP draw计数全匹配；真实payload、native与compute身份资格见[前一报告](ANDROID_SHADER_LANE_AND_COMPUTE_NATIVE_2026-10-28.zh-CN.md)。不新增采集、候选组或GPU编译。

- `.local/logs/hdr-dependent-read-audit-v1/evidence.json`：SHA `c971f5abae7c50d638c5a91ff5c865131fe112530d6f5b270c54f50034c2c1e6`。
- 同目录`original-bundle-inventory.json`：SHA `fc6f1f2aedf2fc53ba54a7ce1f213ec2100d0b5fe5d80c0558867db6d1c9c001`。原始Maxwell字节逐项匹配canonical source_sha256；复用原InstTable/maxwell_refs选择完整32B调度束，由官方nvdisasm解码。170束中152成功、18失败；失败保留，不声称完整shader反汇编成功。
- 当前实际12个源码、16个native变体逐项记录NIR原行、条件上下文、索引的首个内存来源、native完整操作数和draw归属。静态site、参数或编译器stall字段均不换算为运行次数/毫秒。

## 完整主体结构

| 当前源码 | SSBO静态读取site | Buffer txf site | 按循环分组 |
|---|---:|---:|---|
| 两已交付FS、HDR10除两hair | 5 | 16 | 5、3、4、4 |
| 两hair | 4 | 11 | 3、4、4 |

这些是不同列表/循环，不是每个fragment必定执行21次读取。一个SSBO读取在主体入口，提供后面多个loop的计数/分类；每个loop另有一条只读SSBO读取给出记录索引，后续Buffer坐标用这个索引加固定字段偏移。Buffer结果均为vec4 NIR；部分native按活跃分量收窄mask。

以当前`5c1` native `4a9791e4…`为例，官方日志位置：

1. 第40234行读取主体列表头。
2. 第40411行第一loop，第40440行读其列表索引；40442/40449/40451行读索引+0/+1/+2的三个Buffer字段。
3. 另外两个字段位于40471行额外if内，第40473/40486行才读取，偏移包含+6和+3。它们的地址虽然由同一个索引决定，**执行资格仍受先前数据的分支控制**；不能因地址可提前算就无条件读。
4. 后三个loop各自再次读列表索引，分别有3、4、4个Buffer字段。末端UBO16→4发生在其中一个loop的额外条件体，前段列表/Buffer依赖没有随之消失。

这些FS final NIR未见SSBO/image写或barrier；读取标为readonly/reorderable/speculatable的地方与普通texture采样分开记录。没有由此推断CPU/GPU在执行中可以任意改资源；NVN/Vulkan原有同步和别名合同继续适用。

## 已排除的简单解释

**这里没有原宽load明显丢失。** 成功的原官方束里，前段LDG均为scalar `LDG.E.CI`；两已交付FS可见16条多分量TLDS，例如1D RGBA/RGA/RGB。现有Ryujinx texture发射保留component mask和多目标，SPIR-V生成vector `OpImageFetch`；当前NIR/原生仍是对应的多分量Buffer fetch，而非LDC案例那种48 scalar拆分。失败束使原指令全集仍有边界，不能把未解出的指令当不存在。

**没有找到完全相同SSA资源/索引的重复只读site。** 这是窄的负证据，不能关闭所有重复访存调查：代数等价地址、运行时不同字段实际重叠、跨loop动态重复仍可能存在。但共同“记录基索引”并不意味着字段地址相同；索引值在当前IR中已经被后续多个读取共享。多个`bindless_resource`常量表达也不是又从内存解析一次descriptor。

**独立读取并非都串行等前一个。** 上述native在42581/42587/42590行先发出三个独立`isam`，然后42591行才开始消费结果。额外分支内的两个fetch在42610/42613行。这个反证不能量出吞吐，却足以排除“每条Buffer fetch都必须读完才发下一条”的笼统解释。

第一loop另有基于读取结果的提前break；下一索引地址虽然包含线性loop counter，也不能直接预读下一轮。跨迭代流水会增加活跃值/寄存器、浪费提前退出后的请求，并可能越界；当前19full/10waves是应守住的实际门槛，不能预设新增prefetch一定便宜。本轮不实现loop预取。

## 实际GPU预算支持什么

复用`.local/logs/final-c09-upstream-parity-v1/hardware-analysis-agent/formal-comparison.json`及其原SDP输出：同尺寸c09的ShadersBusy71.761%、ShadersStalled16.856%、SystemMemoryStall0.405%、TextureFetchStall12.087%、WaveOccupancy57.844%，read105.263/write44.194MB每Surface。

原工具将TextureFetchStall定义为处理器无法继续向texture路径发请求，可能涉及L1/L2/内存供给。它支持继续调查请求发射、cache/读取路径，**不能定位到这个FS、不能证明所有读取随机，也不能直接乘19.7ms算出可省时间**。SystemMemoryStall低也不证明没有带宽瓶颈；这是全局含合成器的量，与KGSL app SOP/EOP近连续是不同分母。

尚缺每个loop实际进入率/迭代数/active lanes、资源或pass级cache请求/命中、该读取链的stall归属。现有draw计数无法补齐这些量；同一SSA没有重复也不能替代这些动态信息。主线已准备HDR10单组正常对照并检查同轮可用的L1/L2/TexturePipe计数，优先复用那轮工具。

## 为什么旧typed→SSBO实验没有试到真正的另一条读取路径

已有真实12b8原生编译结果很明确：

| 两个FS分别相同的变化 | 原typed Buffer | 旧raw SSBO identity |
|---|---:|---:|
| scalar/其他readonly SSBO的`isam.v` | 5 | 5 |
| structured Buffer读取 | 16个`isam.*.1d` | 16个`isam.v` |
| 实际`LDIB`替代 | 0 | 0 |

总cat5仍45/46，旧变换新增寻址/管理成本。这说明类型虽换，Buffer数据仍在ISAM路径；不能把它当作已经验证过LSU/UAV替代。

exact12b8源码链：

- `ir3_compiler.c:347`：gen>=6默认`has_isam_ssbo=true`。
- `ir3_compiler_nir.c:2030–2062`：可重排的SSBO读取，满足硬件条件时选择`emit_readonly_load_uav`，实际发ISAM；否则调用backend普通SSBO load。
- `ir3_a6xx.c:86–94`及`emit_load_uav`：另一条现有实现发LDIB。
- `ir3_nir.c`明确偏好texture cache路径，并在支持isam.v时允许vector化。这个默认有其理由，切到LDIB可能失去cache/合并优势，不能只看TextureFetchStall下降。

## 冻结的下一项实验准入

先隔离准备一个仍基于同原12b8源码的编译策略对照，仅控制**32bit vec4、readonly且可重排SSBO**是否走现有UAV/LDIB emitter；不改descriptor/resource数据，不加Volatile，不动原受验driver源，不影响普通scalar/RMW路径。优先确认现有官方开关；若不存在，最小编译策略开关须参与cache key和新driver身份。

使用已有实际PSO上下文、原SSBO identity输入先做真实驱动编译。原实际identity final NIR中，16个converted Buffer site已被DCE收窄为10个vec4、3个vec3、2个vec2、1个scalar。因此此次仅vec4策略的预期是10个ISAM变为真正LDIB、其余11个ISAM（含原5个scalar）保留，而非16个全换；同时核寄存器/占用、bounds lowering、别名/屏障与cache合同。没有独立host SPIR-V→Adreno编译器，host仅构建driver不等于这一步已完成；下一步需要普通UID的原编译fixture。它不必先部署整套游戏资源或重建native纹理包。

原typed float4到raw SSBO的语义资格仍是独立门槛：格式转换、整数位观察、NaN/denorm、robust边界和原shader specializations全部保留；改变driver读取路径并不会自动补上这项证明。若原生换路成立，再独立验证画面/完整成本。结果可能是加载请求转移后更快，也可能是cache退化和寄存器增量抵消，必须由整轮正式Profile决定，不预支FPS收益。
