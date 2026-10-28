# GPU Shader 翻译中的宽读取信息：现象、覆盖与回归边界

2026-10-28。**宽度丢失确实不只发生在常量缓冲：LDG、LDL/LDS、ALD
也有逐分量发射路径。但不能由 SPIR-V 标量数量直接推导硬件访存次数，
更不能把所有路径盲目改成128位读取。当前最适合先扩展的是已证明的整数
地址规范化；真正保留 load-group 的 IR 改造需要另立内存语义合同。**

本轮只审查 GPU Shader 离线翻译，未执行或引入任何 CPU 模拟器/JIT。
没有改翻译器、运行时、驱动或生产 Shader 缓存，没有 ADB 或新性能采集。
这里的“宽读取”是数据加载，不是指令缓存取指。

## 转换链与已经证实的来源

实际链为原 Maxwell GPU 机器码 → vendored Ryujinx Shader IR/SSA →
SPIR-V → 原有离线改写/验证 → Turnip 驱动编译。Vulkan 运行时直接使用
SPIR-V；不是经过 MSL 再转回来。`dmsh_vulkan.py` 调用 spirv-cross 的
MSL 输出用于原有反射/证明产物，不能把它误认成 Vulkan 的执行代码。

`InstEmitMemory.cs:Ldc()` 对 B64 设置 `count=2`，生成两个 scalar load：

```c
wordOffset = (srcAddress + immediate) >> 2;
for (component = 0; component < 2; component++) {
    offset = wordOffset + component;
    value = constants[offset >> 2][offset & 3];
}
```

二级右移和 vec4/分量索引是翻译表示引入的。原始 `LDC.64` 可以直接使用
字节地址，并不要求原 Shader 执行这一串索引运算。当前已交付规则只化简
可证明等价的32位整数地址，未自行拓宽内存操作；之后由驱动恢复宽加载。

新增 NVIDIA 官方反汇编证据：精确源哈希 `1f73d855…` 的
`54c93fa22abff9ee` 完整原指令流，移除其声明的0x50字节 SPH 后，用
`nvdisasm --binary SM53 --no-dataflow` **exit0，显示全部24条 LDC.64**。
这是原文件，不是重写后的 SPIR-V。旧独立32字节束有部分因 VLIW 配对
跨束报 unaligned；它们失败不代表原 Shader 错误。第一 VS `3deaf…`
完整反汇编仍失败，原失败保留；其成功独立束确认22条 LDC.64，不冒称
全部原指令已由官方工具解码。

原加载为64位、翻译中拆成32位，不等于语义识别错误。它丢掉了便于后端
优化的分组表达。现有精确 native 结果为48条标量 LDC→12条向量 LDC、
349→142条指令；不能据此断言全部约3.1ms主批减少都是纯 ALU 或 cache
miss。数据量相同也不代表加载发射、缓存端口请求或依赖链相同：即使都
命中缓存，也可能节省这些执行成本。当前没有 cache/scoreboard 的独立归因。

按用户本轮追加要求及仓库 `AGENTS.md` 第16条，后续优先研究寻址/加载/
采样合并、重复读取、memory dependency 和 LSU/cache端口成本。单纯静态
ALU或总指令数下降不能作为高收益判断。这里同时区分三层：实际DRAM
总线字节、片上cache/加载单元请求、值就绪前的依赖等待；它们不是同一个
指标。现VS对照每帧读写量近似相同仍有收益，而旧纯SPIR-V缩指令实验
没有可靠涨帧，支持继续调查加载形态，但尚不能定量分解这三层成本。

不能把每个load解释成“整个GPU同步等待”，也不把store视为没有成本：
store可缓冲或延迟完成，后续仍可能因为队列、依赖或带宽产生背压。是否
是当前瓶颈需原厂 Profile 支持；不据本例断言Android全部场景都带宽bound。

## 各条路径的实际处理

| 原指令/路径 | 当前丢失或保留的表达 | 实证与优先级 |
| --- | --- | --- |
| LDC.B64 | 两个32位 Load，分别构造 word/vec/component 索引；原连续分组未作为 load 元数据保留 | 两VS、两HDR FS已有 exact native 和游戏对照；优先扩现有整数规则 |
| LDG.B64/B128/UB128 | `EmitLdg()` 的 `GetVectorCount()` 返回2/4，每个分量单独生成 GlobalMemory Load；随后各自走 GlobalToStorage | 官方原指令束确认 compute 中有 LDG.64/.128；还没有其当前 native 宽度或整帧预算 |
| LDL/LDS.B64/B128 | `EmitLoad()` 按2/4循环，每个分量重新构造 byte→word 索引，加载 Private/Workgroup 标量数组 | terrain composite 有原 LDS.U.128，当前 SPIR-V Workgroup Load 全为 scalar；驱动能否恢复未知 |
| ALD 多分量 | `Ald()` 按 AlSize+1 次调用属性 Load；原分组通常不进入普通 Load IR | 源码支持此现象，但本轮9个几何VS成功解出的 ALD 主要已是原始标量，未找到其遗漏大收益的证据 |
| STG/STL/STS 多分量 | 和 load 类似逐分量发射；8/16位 store 可能进一步转为读改写/CAS | 原 terrain 有 STS.128、depth-bounds 有 STS.64；写冲突/部分覆盖风险更高，后排 |
| ATOM/RED/CAS | 专门 atomic 指令、scope/semantics、可能的 CAS loop；不是可任意合并的普通加载 | 排除在第一版合并范围外，不能用宽 load+普通 store 替代 |
| TEX/TLD、ImageRead | TextureOperation 保留多个 dest 和 component mask；SPIR-V 先生成向量 ImageSample/Fetch/Read，再 extract/shuffle | 这里通常已经保留向量；分量提取不是重复采样的证据，不应重复“修复” |

源码锚点：

- `dm/third_party/ryujinx/src/Ryujinx.Graphics.Shader/Instructions/InstEmitMemory.cs`：
  Ldc:59、EmitLoadConstant:169、EmitLoad:308、EmitLdg:359、EmitStore:389、
  EmitStg:457、GetVectorCount:501、Get40BitsAddress:511。
- 同目录 `InstEmitAttribute.cs:Ald`、`InstEmitTexture.cs:EmitTextureSample`、
  `InstEmitSurface.cs:EmitSuld/EmitSust`、`InstEmitBarrier.cs`。
- `Translation/Optimizations/GlobalToStorage.cs:299–449,815–960`：逐 scalar
  来源推导、单/多目标选择、byte→word、range 检查及小整数 CAS。
- `CodeGen/Spirv/Instructions.cs:1676`：普通 LoadOrStore 根据 scalar
  pointer/result type 发射；`GenerateTextureSample:1172`、`GenerateImageLoad:688`
  则明确构造向量结果。

GlobalToStorage 尤其值得后续查：原宽 GlobalMemory 读取拆分后，来源匹配、
多目标分派或边界选择可能按分量重复。它也可能已经被单目标 inline 和
后端 CSE 消除。不能只看循环源码认定每帧执行了四倍检查；需实际 Shader
和 native 代码证明。资源声明也不同：UBO 是 std140 vec4 数组，SSBO 是
std430 uint 数组；直接把 SSBO 改成 vec4 会改变表示/寻址合同。

## 原始 Shader 与现有产物的覆盖

本轮核对9个已有实际 draw 映射的几何VS、两条HDR FS、完整22个游戏CS的
精确原输入和已交付 SPIR-V，共33个模块。原输入 SHA逐个匹配 cf83反射。
原完整反汇编失败时保留失败，不把空 stdout 记作无访存；另用原
`maxwell_refs`/InstTable **仅选取**完整32字节调度束，实际助记符和宽度
由 NVIDIA 工具输出，不自写反汇编器。

478个候选束中428个官方成功，50个失败均保留；这是静态抽取范围，不能
当完整指令普查、执行频次或动态热点。新增例子包括：

- `cs_terrain_composite`：成功束显示4条 LDG.64、7条 LDS.U.128、2条
  STS.128；已交付 SPIR-V 中有31个 scalar Workgroup Load。
- `cs_light_bin_depth_bounds`：LDG.64、LDS.U.64、STS.64；已交付
  SPIR-V 中有2个 scalar Workgroup Load。
- `cs_light_grid`、hair precompute、grass/clutter placement、PPR projection
  等成功束有 LDG.128 或 LDG.U.128。它们有源码与原机器码级的宽度丢失
  候选，**尚无当前每帧 dispatch 覆盖、native 合并程度和时间预算**。
- 两HDR FS成功束中的 LDG 主要是标量，已知 LDC.64 的确存在。不能因为
  compute 的 LDG.128 就推论当前HDR主要也有一大块同类漏优化。
- 本轮未找到 LDL 宽访问的成功实际例子；它是源码明确支持的候选路径，
  不能写成当前场景已经证实的性能问题。

`OpLoad Function` 的上千个静态临时变量不等于驱动寄存器 spill；`Private`
数组也不自动等于 Android 驱动产生的 spill。Input 属性常由顶点输入路径
提供，Workgroup 是着色器共享存储，均不能直接算成 DRAM 流量。两个HDR
FS的16次 typed buffer fetch 原本已返回向量；旧 identity→SSBO 实验未减少
fetch，反而 native 指令增约22%–24%，不再包装成未做的新方向。

## 已有精确整数规则的全量静态匹配

额外对原29e完整6931个 **base** SPIR-V 做只读扫描：每份输入尺寸/SHA都
核对冻结 `59320caa…` receipt，调用官方 spirv-dis 和未修改的既有规则。
未形成新生产链、未重建 spec/pred/tssbo、未 native 编译或游戏测量。

| 阶段 | 原base数 | 有规则匹配 | 仅c=0移位合成 | 含低位offset或非零component化简 |
| --- | ---: | ---: | ---: | ---: |
| VS | 2344 | 168 | 158 | 10 |
| FS | 4559 | 3235 | 240 | 2995 |
| CS | 22 | 2 | 1 | 1 |
| 其余/内置 | 6 | 0 | 0 | 0 |
| 总计 | 6931 | 3405 | 399 | 3006 |

全部变化结果的31812个直接使用都属于认证 UBO 的 AccessChain。也就是说，
这次全量扫描发现的仍是 UBO 地址规范化覆盖，**并未证明现成规则已经
解决 SSBO/Workgroup 宽读取**。399个只有 c=0 合成的模块可能只是一般
冗余移位清理，不能和3006个更能显露共同向量索引的模块混成等价收益。
后者也只是更强候选，不保证驱动确实合并或加速。

cf83相对原base仅两VS和两FS改变，因此强匹配集合里另有 **8 VS、2993 FS、
1 CS** 未应用。很多来自粒子变体：billboard强匹配1536个、uber mesh强
匹配1024个；`ps_models`还有192个同类base。**变体数量不等于每帧调用数
或工作量**，它们不构成营地又有一个15%可省预算。

选取三个未交付代表，原工具重新生成分析候选并执行 spirv-val、全32位
Z3 等价证明：`67e2ebc28c830f44`（VS default）96个结果、
`005b770708474315`（粒子FS）10个、`0f715b1a5e03f11f`（terrain CS）32个，
均通过。这证明这三个 base 的整数结果等价，不代替其驱动/图形验证；
旁边的 specialization 等配对文件没有改，分析产物不可直接部署。

## 推荐实验顺序与回归控制

### 排序依据：动态访存成本，不以总指令数排序

两条 VS 的 `349→142` 只能说明编译结果变化；`48 scalar LDC→12 vector
LDC` 才提供了更具体的加载形态线索。二者不能互换为收益归因。VS 执行
次数通常少于 FS，但还要看实际顶点数、各 pass 的覆盖和每次执行成本；
既不能因为 VS 较短便认定它收益小，也不能拿两个静态数字解释已测涨帧。
FS 减少很多计算仍没有可靠收益的既有实验，应降低继续纯缩指令的优先级。

后续候选按以下顺序审查，每项同时记录实际覆盖和仍保留的工作：

| 优先项 | 优先寻找的变化 | 必须分开的证据 |
| --- | --- | --- |
| 寻址和连续加载 | 共用地址计算、保留原宽读取、合并合法相邻分量、共用来源/范围判定 | 原生加载发射数、有效字节数、地址依赖、寄存器与占用；合并不自动减少 DRAM 字节 |
| 重复读取和采样 | 同一合法只读值供多个消费者使用，减少真实 fetch，而非只减少 extract | 实际执行覆盖、原生 fetch/load、cache/LSU/依赖停顿与可得外存计数 |
| 延迟隐藏 | 缩短关键读取依赖，保留独立请求并行，避免生命周期增长造成 spill | 依赖链、独立请求、寄存器/占用和驱动结果；不能只数 load |
| Store | 仅在写入流量、排空、读后写依赖或缓存背压证据明确时提前 | Shader store 与附件落盘分别统计；可缓冲不等于免费 |
| 纯计算 | 有算术瓶颈、寄存器压力或占用证据再优先；其余低于上述项 | 动态计算量、编译器是否实际消除及整轮变化；SPIR-V 字节/静态 ALU 数不够 |

“外存带宽饱和”“加载管线吞吐受限”和“依赖 load 的值尚未就绪”是三个
不同问题。缓存命中的 load 也可能有端口/发射/依赖成本；一次 miss 也不
代表每条 load 都在串行等同一次传输。工具未给出对应计数时保留未知，
不把当前访存优化方向写成已经证明所有 Android 场景都 DRAM bound。

### 具体候选

1. **先扩现有精确地址规则的热点覆盖。** 按已有捕获中实际发生的 Shader
   选择其他 models/粒子/CS代表，先 native compile 对照加载数量、寻址
   指令、寄存器/waves。单纯 SPIR-V 变小不通过性能筛选。已有9个几何VS
   仍仅两条匹配，不能把全量新8条说成当前营地遗漏。
2. **只读 LDG 的 load-group 保留。** 先保留原指令的分量集合、相对偏移、
   同一求值点和地址来源；让后端按合法性选择 scalar/vec2/vec4。不要
   把所有 `Load` 改成 vec4，也不要先扩大资源 range。来源/边界检查共享
   可另省成本，但需证明四个分量的选择条件完全兼容。
3. **重复读取与依赖链。** 查同一个只读来源的实际重复load和重复来源/
   边界解析，优先让多个消费者消费已经取得的同一值；不能只看等价地址
   就跨调用、写入或barrier做CSE。向量化后请求数、寄存器生命周期、独立
   请求并发度与依赖链一起看，避免少几条指令却串起更长的数据依赖。
4. **采样与布局。** 对真实texture热点查有效采样次数、LOD/格式/布局及
   缓存复用；已是vec4结果的fetch不因四次extract而变成四次采样。沿用
   旧typed→SSBO负证据，不为减少表面Load数量换成更多fetch/检查。
5. **有实际热点后再试 LDS/LDL。** 局限同一原指令、同块/同控制域、已知
   对齐且无中间 barrier/write 的组。保持 Workgroup 与 Private 独立，
   不凭静态31个 Load 推 FPS。属性路径更后排，目前缺真实宽 ALD 大头。
6. **Store、atomic、coherent image 不进入首版。** 它们的可见性、部分写、
   同一word内不同线程更新和顺序合同更复杂；节省指令不能替代证明。

ALU独立改写排在这些方向之后；只有它实际缩短地址/依赖链、降低寄存器
压力/提高占用，或有算术执行瓶颈证据时再提升优先级。不从FS静态少约百
条指令推整帧收益，也不将VS的349→142总数本身当作已经查清的微架构根因。

现有纯整数规则风险较窄：逐结果证明全部32位输入和模溢出等价，不改
浮点、访存范围、取值点或资源接口。真正新宽 Load 改造风险明显更大：

- 基址对齐、动态 component 可能跨 vec4；不把“当前值碰巧对齐”当证明。
- 保留32位回绕、40位地址高位/进位；不要用数学整数等式代替位向量等式。
- 保留最后一个有效分量、部分越界、缺失 buffer 及 robust 策略；一条
  全组范围检查不能把原来仍有效的前半部分一并丢掉。
- 不加载原来未读取的尾部分量，不强保死寄存器，不跨分支/循环/函数移动
  读取；注意新向量延长生命周期、增加寄存器后可能降低 waves。
- 保留 volatile/coherent、atomic、barrier、别名写的观察顺序。不能因为
  UBO自身只读就假定所有底层内存永远不被其他视图更新。
- 不将 texture 的隐式导数、LOD、gather、格式转换或稀疏/存储语义改成
  普通 buffer 读取；也不混入FP16、NaN或精度策略变化。

部署仍必须走原统一工具链，同时重建 base/spec 的绑定关系、driver-key、
span、pixel-local及完整manifest/chain；不能只替换几个SPV却保留旧配对
证明。每轮真实对照按仓库规约核整帧/全应用/线程/频率和可得GPU指标，
观察寄存器、加载和新增分支的净变化；工具不支持的项明确为未知。

本轮没有证明要重写整套 Ryujinx IR 才能继续提速。现有有限整数规范化
可以先扩大可验证覆盖；保留原 load-group 是更长期、通用的表达改进，
应由后端合法化，不能以“原指令宽”跳过 Vulkan 内存合同。

## 复核入口与工具边界

本地独占目录 `.local/logs/shader-wide-load-audit-v1/`：

- `static-inventory.json`、各key `prior/delivered.spvasm`、原 `.sass/.stderr`：
  33模块的精确源、官方输出、失败与静态分类。
- `original-bundle-inventory.json`：478束的原偏移、原字节SHA、官方命令与
  返回码。全流成功的模块不再重复算束，静态数字不混成动态执行数。
- `existing-rule-coverage.json`：6931个原base的SHA、命中和原规则记录；
  `existing-rule-classification.json`：c=0/非平凡分类、绑定身份与未部署范围。
- `representatives/receipt.json`：三个代表的原工具验证及Z3记录。
- `collect-existing.py`、`collect-bundles.py`、`scan-existing-rule.py`、
  `classify-existing-rule.py` 仅编排原工具/整理其输出，均留源码SHA。

当前 vendored 发射器不等于全目录未修改的原龙神：本仓库已经补过 AST
安全传播、quad等路径。LDC/LDG/ALD的上述关键发射逻辑与本机原源码副本
相同；所有结论仍以审查时的实际源码SHA为准。未启动真实游戏模块，故
本报告是优化调查，不是新的 Android 原生执行或性能验收里程碑。
