# 实际 lane 地址与当前 Compute 原生代码审查

结论：不能用“VS divergent、FS uniform广播”解释已有VS约15%和FS约1%的收益差异。两个已交付VS的目标是直线路径的48次divergent读取→12次vec4；两个HDR FS真正减少的是主路径7→3和条件loop中的16→4。当前CS原生输出也已经取得：此前疑似丢失的LDS64已经恢复，LUT/light-grid的scalar SSBO多数已在uniform preamble，不能按全体线程数放大其预算。尚无新的、已证明可以省数毫秒的CS候选。

按AGENTS第16条调查真实寻址/加载/依赖，不以静态指令总数解释收益。本次只做身份、控制与原生形态分析；不从单次draw/dispatch时间推导纯Shader成本，不新增计时器、不改游戏或Shader生产链。

## 原始证据与资格

本次真实游戏诊断：`.local/logs/current-compute-native-device-v1/CS-NATIVE1/`。APK `c185e219…`，host `ee1f924c…`，source `f57b3a8`，shader `cf83dfece8e7`，driver `12b8b54a…`。营地area/client_area1、5225/6289；game/swapchain均1968×900、scale0.85。诊断APK与普通2be1的六个native库、DEX/资源相同，只新增官方Mesa wrapper及必要签名，不能作普通FPS基线。

- 已按原生命周期save2停机，原私有目录的日志哈希匹配：`ir3.log`=`3a5056c8911c3dc4a0f03c7a81ad7d5fcccb5938650d5ba3097d4cf538470f34`；CSV=`4be7ab41b30dafc32c7b1b619f947dcffb8de5f2451be9f7b8de58e305eb2218`。
- 复用原`select_complete.py/analyze.py/link_draws.py`，选末尾3个完整49RP CB，batch10923/10924/10925；147个RP的draw计数全匹配，无模糊/未归属draw。整份CSV原有1条partial row、1个缺边界batch，保留原文件，不修补成成功数据。
- 原controller收尾把`,compute`当事件检查而报错；实际是`start_compute/end_compute`。失败保留，已经静态验证的文件可以独立分析，未因此重采。
- 独立复核确认原CSV中两种compute事件各19583条，四个私有文件的本地归档与设备原SHA清单相同；清理前再次核准设备库存。现已恢复原c09 APK及本次入口的22个用户存档文件，游戏、工具和本次私有诊断目录均已退出／清理；收条为`current-compute-native-device-v1/final-restoration.json`。
- 95个实际VS/FS/CL native identity中90个能精确对应canonical payload；实际12个compute native hash全部有CL编译输出，其中10个精确匹配本次运行使用的10个源码CS，另2个不在canonical模块中，暂不猜功能。12个compute hash与之前真实营地捕获逐个一致。
- 14个准备中的FS现在全部有本次当前chain的实际payload/native资格。HDR10共145 draw/CB；EXTRA3为23+3+3；fullscreen attenuation为1。draw数不是fragment/lane-iteration或时间权重。

完整文件在`.local/logs/shader-current-compute-native-analysis-v1/`：

| 文件 | SHA256 |
|---|---|
| `current-native-evidence.json` | `3ec3e91b65d2ad11e9dce7940b32c84aa66d10ac39f45bfc048aa496cd64cda1` |
| `source-map.json` | `a0c93483a3dc3f0cb17a535f3f2bae96df0896ada7b046d756c52f3f5b213809` |

`source-map`使用原官方NIR的source_blake3匹配准确SPIR-V payload，再以native blake3匹配实际compute tracepoint，未按dispatch尺寸猜源码。`current-native-evidence`记录逐load原行/flag/控制上下文、完整native memory操作数、模块与实际dispatch关联。原controller receipt在分析目录内冻结，失败/恢复均可核查。

## con/div与实际广播的区别

exact12b8的`src/compiler/nir/nir_print.c:124–149`在divergence有效时打印`def->divergent`为div，否则con。这是编译器的静态判定，div不证明本帧每个lane实际地址不同，也不证明cache miss。

`src/freedreno/ir3/ir3_compiler_nir.c:1173–1205`对UBO明确按`!def.divergent && has_scalar_alu`选shared寄存器和`ldc.u`。而`copy_ubo_to_uniform`/`ldc.*.k`属于另行统计的常量预备。SSBO的con不能直接等同某一种硬件广播：还须看它是否在elect/preamble，以及实际原生操作数。

旧及当前官方输出组织在`.local/logs/shader-lane-load-budget-v1/evidence.json`，SHA=`a552ab03b325b3188d347e94db3e2a87c409819be87a31c899c2e094c50adf8b`；仅组织原NIR flags/SSA，未自行计算divergence。两个VS候选与已交付cf83 SPV、两个FS候选与cf83 spec payload逐字节核对，编译driver均12b8。

## 两个VS：减少确实落在直线的逐顶点读取

| VS | 原→当前load_ubo | 原/当前标志和控制 | 分量数 |
|---|---:|---|---:|
| `3deaf68fd65b5298` 阴影模型 | 48 scalar→12 vec4 | 全部div，preamble外，无if/loop包围 | 48→48 |
| `54c93fa22abff9ee` 主模型 | 48 scalar→12 vec4 | 同上 | 48→48 |

地址从`VERT_ATTRIB_GENERIC2` uint4输入乘48、加矩阵行偏移得到，原和当前均div；这支持逐顶点蒙皮索引的解释，不能说它只是每draw一份的常量。实际原native48次非uniform LDC，当前12次vec4；源读取分组、加载发射和地址链都改变，数据量不变。即使cache命中，也可能减少片上请求/依赖开销；没有计数证据则不把该收益归因为cache miss或DRAM流量。349→142的静态总数不是3.1ms的根因证明。

## 两个HDR FS：减少的16次分属两个动态预算

实际原生compile的active `/1`，Spec0=1；不混safe-constlen `/2`。

| 每个FS的部分 | 原→当前load_ubo | lane/控制 | 分量数 |
|---|---:|---|---:|
| 主路径 | 7→3，少4 | div，无loop；当前4+2+1分量 | 7→7 |
| 条件loop体 | 16→4，少12 | div，loop及额外div if | 16→16 |
| 5c1 uniform preamble | 9→9 | con，不是目标 | 15→15 |
| 38f uniform preamble | 11→11 | con，不是目标 | 20→20 |

主路径UBO地址来自插值数据和一次显式LOD的2D `txl`；循环内UBO地址来自fragment coordinate/SSBO→loop内SSBO→Buffer `txf`→地址计算。两处前段读取和依赖并没有被末端UBO合并消掉。当前仍有19个full寄存器/10 waves，循环结构未跨阈值。这些是小收益的结构解释，不能在没有活跃lane/迭代覆盖时断言循环很少执行或某级cache是根因。

预算只能写成：VS每次确实执行路径固定少36次加载发射；FS主路径少4次，另每个真正进入条件体的lane-iteration少12次。未知invocation/分支分布，不能乘draw数换算毫秒。

14个新FS除grunge外，实际16条div UBO全在条件loop体；grunge有主路径7条+loop16条。没有现成“只改主路径”组。两hair控制更深，fullscreen1draw也不证明条件体覆盖全屏。全部已有host候选仍保留，未新增分组或构建。

## 当前CS的工作量与原生核查

一次真实22266-op停帧中每项恰好dispatch一次。group来自原页，local来自相同SPV的官方`spirv-dis`并核对canonical元数据。下列是启动的逻辑invocation，未量有效线程、循环内工作或GPU时间。

| 源/key前缀 | groups / local | 逻辑invocation | 当前真实NIR/native |
|---|---|---:|---|
| LUT0 `890de4` | 4×4×4 / 8×8×8 | 32768 | 主要为3D采样/输出，无新的宽SSBO问题 |
| LUT1 `552f45` | 同上 | 32768 | 19个scalar SSBO全部con/preamble，elect后store_const；未合宽，但不是每线程重读19次 |
| fluid update `da5e3b` | 8×1×1 / 64×1×1 | 512 | 5个SSBO在con/preamble |
| fluid cull `4639dd` | 4×4×1 / 8×8×1 | 1024 | 96个con SSBO在loop；32full/6waves；不能因静态96就判它最重 |
| fluid accumulate `098a12` | 32×32×1 / 8×8×1 | 65536 | 主体div SSBO/workgroup，包含local atomic/add，非只读合并入口 |
| depth bounds `6a7a30` | 32×20×1 / 32×1×1 | 20480 | SSBO3个con/preamble；Workgroup已经vec2，native `ldl.u32 …, 2`；LDS64丢失假设被排除 |
| light bin `b54d19` | 4×5×1 / 8×4×1 | 640 | 16个div SSBO（8在loop、8在外），大量同址RMW，需按内存版本看 |
| light grid `1188ea` | 4×8×4 / 8×1×8 | 8192 | 8个scalar SSBO均con/preamble；主体6个Buffer txf是con/loop，已vec4 |
| terrain query `913751` | 1×1×1 / 64×1×1 | 64 | 2个div SSBO主体读取；线程工作范围小 |
| 无游戏源元数据 `16479c` | 1×1×1 / 32×1×1 | 32 | canonical字节/原生准确对应；功能不猜 |
| 总计 | — | 162016 | 不代表各项成本相同 |

LUT1和light-grid原LDG128的宽度确实没有在这些scalar SSBO指令上恢复，但已被preamble提取；它们作为主线大预算的优先级下降。`.v`并不代表这里已经四分量：当前原生明确是`isam.v … (u32)(x)`。depth bounds则反向给出已合宽的证据，不能继续以SPV scalar计数声称驱动没恢复。

## 下一候选与边界

1. **先查HDR主体前段的只读依赖读取**：当前末端UBO已合并，而SSBO→Buffer txf→UBO仍在。应按实际NIR查源wide group、重复同址读取、共享地址解析和可独立提前发起的读取；con/div、loop、volatile/coherent、可能别名、控制支配和robust范围分别保留。标准texture采样与structured/Buffer数据区分，不能为“预取”把条件内可能越界/有副作用的操作无条件发起。当前缺的是这些链的动态迭代/lane/缓存预算，不再按总指令数挑选。
2. **CS小范围可实施机制候选：同invocation的store-to-load forwarding**。b54中的14个div load反复读取同两地址，但有load→+1→store及输出写入。通用memory-SSA若能证明同一资源/地址、所有路径的最新值、其他store/atomic/barrier/coherent/volatile都不影响，才能用SSA值代替后续依赖读取。跨loop须正确phi；不同descriptor可能别名，没有Restrict就不能假设不别名。不能把重复字面地址直接CSE，也不能只看单个营地绑定后固定成生产规则。
3. 该CS当前仅640逻辑invocation，真正RMW命中及loop迭代未知；没有可以承诺的毫秒预算。可先做只读证明范围统计，若绝大部分因alias/跨路径不能证明，就不先写昂贵管理/分支来强行落地。最初的readonly一对LDG64也可保留源group研究，但理论覆盖更小，不能解释主绘制的大差距。
4. 正式GPU profile与本次身份捕获分开。这里的nop/sstall/systall/native寄存器是编译输出，不是测到的运行周期/停顿。只有实际加载、外存/片上停顿与整帧数据支撑后，才升级某项为高收益主线。

本轮新增的是更准确的排除和排序，不声称已获得新FPS收益。两VS、两FS及14FS原候选保留；没有因当前小收益丢弃机制，也没有为填满清单硬扩一批不在当前运行的Shader。
