# 实际 Shader 身份、地址候选与下一轮访存优先级

## 结论

以下按本仓库 `AGENTS.md` 第16条的访存优先约束排序。

1. 现有合法 UBO 地址规则不能再从营地的其他 VS/CS 里直接扩出一批同等级候选。当前停帧的 20 个 VS 与之前真实 GPU draw 映射全部对应，只有已优化的 `3deaf/54c` 两个命中该规则；本次 10 个 CS 均不命中。不能用全库另 8 个 VS、1 个强 CS 的静态匹配替代实际覆盖。
2. CS 现在已经有当前运行的源码身份，**不用为确认 guest CS 再采一次**。`f357/26a1/cf83` 的 22266-op 原始停帧内，共 10 个 dispatch；9 个有游戏源元数据。它们都能复用旧官方 GFXR 中真实的 compute 创建上下文。但尚缺同一 12b8 驱动下 CS 原生 ISA，不能声称 SPIR-V 的 scalar 已经造成硬件的 scalar 读取。
3. 三组 FS 地址候选全部经原入口、完整验证准备完毕。这些改变的是 **LDC/UBO 寻址**，不改普通 texture sampling；14 个 FS 在当前停帧也全部实际执行。此处仍未做真机候选编译、画面或性能对照。
4. 12b8 的 Q735 默认开启有限的 FS texel pre-dispatch 能力，但实际旧营地编译日志的 55 个已映射 FS native identity 中，final NIR **没有 `tex_prefetch`**。有大量 descriptor prefetch，二者不同。不能据此说 GMEM/tilebuffer 已自动预载所有 texture；也不能直接宣布这是驱动 bug。

## 证据范围

- 当前源码身份：`.local/logs/store-forward-read-reuse-capture-v2/ACTUAL-ONE-STOP1/capture/snapshot.json`；APK `f3576a49…`、host `26a104a8…`、source `62839889…`，启动记录确认 shader chain `cf83dfece8e7`、同一 PID 31135。`complete=true`、`replay=0`、22266 ops。这是一次停住后的只读序列，不是时间/频率样本。
- 顺原 expanded sequence 更新 `OP_BIND_PROGRAM=14`，在 `OP_DISPATCH=27` 时从 `programs → key → shaders[stage=5]` 得到真实源码身份；dispatch payload 只读原已采集并逐页验证哈希的 `pages.bin`。10 个 payload 的 x/y/z 都在原页中，无缺页，也未发起新目标读取。draw 的 VS/FS 也按同一程序状态归属。
- 真实 GPU native 身份：旧 a5e4 APK/e46f host/29e shader/12b8 的营地捕获，3 个完整 CB，147 个 RP draw 数一致；原早期读取失败及后续同 PID 停止文件恢复均保留。它的 per-draw 时间不拿来当纯 Shader 成本。
- 旧官方 GFXR：480ce73c/F830。复用官方 JSON、官方提取的 144 个原始模块，恢复 16 个真实 compute PSO、全部布局、105 次 dispatch，无未知 pipeline handle。13 个 PSO 匹配 canonical SPV；3 个未匹配。它可以提供真实 API 编译上下文，**不是与 12b8 相同的 native 编译结果**。
- 原 nvdisasm 宽读取证明来自 intact 调度束的官方成功解码，仅作为覆盖到的原指令证据。3 个重点 CS 的完整流解码均未成功；成功束不能冒充全流成功。

主要结构化文件：`current-compute-snapshot-map.json`、`current-load-priorities.json`、`existing-API-compute-manifest.json`、`qualified-candidate-manifest.json`、`host-ready-groups.json`。每个 organizer 和输入均留哈希，原数据不改。

## 当前 VS：有明确的负证据

当前 20 个 VS 无新 key；几何部分的未改 7 个 VS 都不命中现有合法地址规则，粒子/界面/全屏实际 VS 同样不命中。

例如，旧精确 native 映射中：

| 实际 VS | 旧捕获 draw/CB | 原生读取形态 | 判断 |
|---|---:|---|---|
| `ed8df…` 阴影模型 | 420 | preamble 有 `ldc.4/ldc.8.k` | 非旧 48 条 dynamic scalar LDC 链 |
| `54f730…` 主模型 | 331 | preamble 宽 LDC，cat6=0 | 同上；cat6=0 不代表没有所有内存工作 |
| `b28286…` 阴影模型 | 238 | preamble 宽 LDC | 不凭静态总指令大就改 ALU |
| `af64b4…` 主模型 | 236 | preamble 宽 LDC | 同上 |
| 6 个粒子 VS | 各 1～2 | 原生含 `isam.v` buffer fetch | 数据链值得保留清单，但尚无 invocation/时间预算，不因采样条数多而高排 |

这些 draw/count 不是唯一顶点次数；indexed draw 重用、instance、分支执行均未由此量出。旧 native 主阴影组与当前未改 SPV 有字节桥接，但不能拿旧 `3deaf/54c` scalar ISA 当当前 cf83 优化后的 ISA。

## 当前 CS：准确候选和下一步

本次每项 dispatch 一次；只有下表“本次确实执行”的 10 项。静态库中有 32 处地址匹配的 terrain composite **本次没有执行**，不能作为当前营地优先级。

| key / 源 | 本次 group 数 | 已知访存线索 | 优先级 |
|---|---|---|---|
| `6a7a308d61928982` depth bounds | 32×20×1；local 32×1×1 | 原成功束有 LDG64、LDS64/STS64；SPV workgroup 2 条 scalar load；有 atomic min/max | 第一批核原生；共享读取可独立检查，atomic/同步不可合并或跨越 |
| `552f45c348a61b86` LUT combine1 | 4×4×4；local 8×8×8 | 原成功束含 4 条 LDG128；当前 SSBO 是 uint32 runtime array | 第一批核 SSBO 地址/载入形态；不是现有 UBO 规则的适用域 |
| `1188eaedc8db3285` light grid | 4×8×4；local 8×1×8 | 原成功束有 LDG128；当前有 SSBO 和 6 处 Buffer `OpImageFetch` | 第一批区分 global load 与查表依赖链 |
| `b54d192f7c620ded` light bin | 4×5×1；local 8×4×1 | 原成功束有 LDG64；含 SSBO/Buffer fetch | 第二批，先拿原生与成本证据 |
| `098a126f92bf533a` fluid accumulate | 32×32×1；local 8×8×1 | 原束 LDS32；workgroup scalar 本来就可能合法 | 不仅因 scalar 而改宽 |
| `4639dd6d3e3f0c1b` fluid cull | 4×4×1；local 8×8×1 | 原成功束大量 scalar LDG | 数据读取链可查，不能称原宽度丢失 |
| `da5e3bfe00b88521` fluid update | 8×1×1；local 64×1×1 | 原成功束 scalar LDG/STG | 同上 |
| `890de4b2c99eb9ba` LUT combine0 | 4×4×4；local 8×8×8 | 当前源确定，无现成原宽束证明 | 未知不补零 |
| `913751f67bc50268` terrain query | 1×1×1；local 64×1×1 | 原成功束有 LDG64 | 有结构线索，但覆盖小，须看真实成本 |
| `16479cc6d75c2b28` | 1×1×1；local 32×1×1 | 当前 key 与原 API 模块准确，canonical 无游戏源 json | 不猜成具体游戏/内部功能 |

这里 `Uniform` 存储类别**不能直接等同 UBO**：旧 SPIR-V 1.3 的 SSBO 使用 `Uniform + BufferBlock`。例如 552f 的 `cp_s0` 是 SSBO uint32 array；6a7 的两个全局池也是 BufferBlock，另有真正 cbuf。必须按元数据、descriptor binding 和 Block/BufferBlock 区分后再改。

下一步先复用已有 `existing-API-compute/pipeline*.json` 和官方原 SPV，在同一 12b8 驱动原合法编译环境中取得上述三个 CS baseline 的原生 ISA/寄存器/宽加载结果。我们本次**没有创建/运行任何新 pipeline**；现有 host 工具只有 disassembler，不能把 SPIR-V 静态分析假装成实际 native 编译。若原环境编译工具当前只支持 graphics，应由主线补一个最小 compute 入口，使用现成原创建状态，不编造布局。原 native 结果若已合宽，则不为“保留宽度”重复改；若仍标量化，才据地址对齐/robust/偏移依赖定局部合法实验。

暂缺动态调用/时长/LSU 停顿预算，不承诺这些 CS 能省多少 ms。原图形区间的收益不能移算到它们；内存位数相同也不代表发射、依赖或片上 cache 端口开销相同。

## FS 三组 host 候选已准备

原始源链 B=`29e56b51bce5`，输入完整 receipt SHA `59320caa…`。每组从 B 经原 `dmsh_derived_address_combined.py build` 重新生成；保留 cf83 原 2VS+2FS 字节完全一致，不向 cf83 产物叠补丁。

| 组 | 新 FS 数 | 完整派生 chain | 验证 artifacts |
|---|---:|---|---:|
| HDR10 | 10 | `baa43ea3120e` | 52282 |
| EXTRA3_PAIRED | 3 | `11ebecf599fa` | 52261 |
| FULLSCREEN_UNPAIRED | 1 | `e634271f9608` | 52254 |

位置 `.local/build/dynamic-ubo-address-candidates-v1/<组>/vk-<chain>`；命令、builder 和产物完整哈希在 `host-build-commands.json`、`host-ready-groups.json`。14 bases + 13 actual spec payloads，共 27 份模块、446 个 changed SSA results，经原 normalizer、官方 `spirv-val`、既有 Z3 全 32 位证明通过；13 个 recipe 保留原 selector，不重新选择。

- 原真 GPU 身份证明 HDR10 合计 146 draw/CB；本次当前停帧为 145，差在 models112 15→14。14 个源 key 当前都实际执行。**当前停帧没有 FS specialization payload/native variant 选择**，所以它补强 source coverage，不能替代旧实际 spec/新 native 编译证据。
- EXTRA3 当前 23+3+3 draw；fullscreen attenuation 当前 1 draw，像素覆盖/耗时未知。不能因它 draw 少而自动低排，也不能据“全屏”猜测节省时间。
- 实验只改 LDC/UBO 地址规约，期望让相邻 scalar 访问显露共享 vec 索引；不是改贴图采样、lod、纹理 cache 或输出精度。新 native 是否合并尚未知。
- `11edd0a` 只给原 toolchain 补“原本没有 spec”的合法路径，未伪造 spec。paired 丢失、残余 selector、非诊断 skip、dangling driver key/span、schema 均拒绝。最终 15 项 host 测试通过，额外覆盖 key/span 篡改；没有 runtime 默认开关变化。

## 12b8 的 FS prefetch 到底覆盖什么

已逐文件匹配 `turnip-internal-queue-sync-v1/candidate-source-ledger.json`，driver SHA=`12b8b54a…`。实际 pipeline-cache device ID `43030b00` 对应源码 FD735，继承 a7xx_base 的默认 `has_fs_tex_prefetch=true`。这只是默认启用资格，未从目标导出实际运行时 override 值。

源码实际规则：

- 仅 FS、`nir_texop_tex` 的简单非 array、非 sparse 2D；不支持该通路的 explicit LOD/bias、shadow comparator、projector、offset、显式 gradients、MS index、动态 texture/sampler offset。
- 坐标必须直接来自支持的 `load_interpolated_input`，或者 contiguous、未乱序的 vec2 分量组装；支持 pixel/sample/centroid barycentric，smooth/noperspective 等限定插值模式，要求常量 input offset。
- 只在入口最外层第一个 block（跳过 preamble）找候选；选择单一 bary 模式。依赖前次 UBO/SSBO/texture 读取、坐标 ALU、分支内采样均不能一概当作这个简单入口的命中。
- descriptor handle 编码另有限制；最多 4 个。小 Shader 的发射限额是 2 或 3，大 Shader 才到 4。Turnip 把命中的实际 texel prefetch 写入 `SP_PS_INITIAL_TEX_LOAD_*`。
- `@prefetch_sam_ir3/@prefetch_tex_ir3/@prefetch_ubo_ir3` 在 preamble pass 中预取的是 descriptor。旧原始 dump 分别出现 966/181/149 行，但整个 dump 的 `tex_prefetch` 是 0。55 个动态映射 FS final NIR 同样 0。两项统计的分母不同，前者包括整个编译 dump，不能当逐帧执行次数。

tile/GMEM 是 attachment/render-target 片上存储，普通 sampled texture 走其纹理读取/cache 路径。不能把 tile 内的 render-target load/store 优势扩张为“所有任意纹理都已预读”。也不能因为这个 pre-dispatch 没命中，就说明普通 sampler 无缓存/每读必然等 DRAM；实际 cache 命中和依赖停顿还须硬件 profile。

目前不为命中 prefetch 而扩大全局重排；先按用户方向查 UBO/SSBO/structured/依赖查表。普通直接 varying texture 仅作驱动实际处理范围核对，不用总 SPIR-V 指令下降或“texture 很多”预估收益。

## 实施顺序与保留边界

1. **主线先核当前 CS 原生宽度/依赖**：6a7、552f、1188，复用原创建上下文；不先做一轮广泛改写。现有 UBO 规则对当前 VS/CS 没有新增覆盖是明确结论，不硬扩。
2. 三组已准备 FS UBO 候选可各自独立受控编译/对照，避免一次合并多组后无法归因。baseline native 身份先复现；对照同时看整帧 CPU/GPU/提交/频率/实际读写而不是仅看 native 静态条数。
3. 若 CS 原生证明宽度未恢复，再保留源 ISA group/对齐语义到 IR，让后端在 robust 合同下合法化；首先是窄小、同对象、同支配范围的相邻读取。不要盲目改 128-bit 读取扩大越界范围；volatile/coherent/atomic、跨 barrier、32 位 wrap、动态跨 vec 和未对齐分别处理。
4. 普通 ALU 删指令不作为主线，除非减少真实地址链、寄存器/占用或已有算术瓶颈证据。349→142 的静态总数不作为 3.1ms 收益根因，FS 的小增益也不能用静态总数外推。

本轮没有新 GPU 性能结论，没有装包、停启游戏或改生产 shaderchain。host 工具补强已独立提交；其余均是 `.local` 证据和原入口候选产物。

## 下一次诊断的最小计划

- 使用原 `scripts/dev/build-android-apk.py --tlsdesc --shared-pools --shared-import-windows --native-resources --opt-level 3 --native-jobs 4 --mesa-draw-identities`。仅原诊断 wrapper 进入 APK，不改正常 APK 归档、shaderchain、资源或游戏模块。
- 本次 wrapper 补为 `IR3_SHADER_DEBUG=vs,fs,cs`；exact12b8 的 `ir3_compiler.c:24` 明确定义 `cs` 为 compute shader disassembly flag。其余官方 tracepoint/私有冷缓存流程不变，没有新增计时器或采样器。
- 诊断 APK 六个 native library 须与 `.local/build/store-attachment-domain-default-on-production-v1/canonical.apk` 全部逐字节一致；同时验证原 DEX/assets/resources/AndroidManifest。只允许新增 `wrap.sh` 和必要签名变化，构建记录及所有源码输入都验证；诊断 manifest 必须标 `performance_baseline_eligible=false`。
- 主线另开一次 fresh 受控事务，用原工具启动同一 12b8、cf83、BC3 native shared、营地/真实尺寸相同环境。冷缓存确保真实 CS compiler NIR/ISA 出现，原 compute tracepoint 保留实际 native hash；一次采集兼作 14FS 的当前 payload/native 资格核对。
- 到原 readiness 后保留一个有限窗口，先通过原生命周期存档并停止 owned PID，再一次拉取本次私有日志/CSV。避免边写巨量 per-draw CSV 边经 ADB 全量回传；不以诊断 FPS 作性能基线。
- 以实际 source_blake3 对原 SPV payload，actual native hash 对 compute tracepoint；原 CS key/模块/driver/输入条件均一致后才讨论宽度恢复。无源码、无原生、编译未出现或 hash 不同各自保留为缺口；不按 dispatch 尺寸猜映射。

本子代理只准备/认证 host 诊断 APK；不连接或操作设备。安装、一次正式采集及普通 APK 的对照由主线继续。
