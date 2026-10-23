# 当前默认版本的 CPU 余量与稳定 Pool Buffer 实验

截至本轮，Q735 的翻译线程尚未达到 16.67ms，不能宣称 60FPS 目标完成。本轮完成了当前默认版本的重新归因，以及独立驱动的 sparse host-memory GPU 功能验证；没有新的整帧性能收益结论。

## 当前版本的实测职责

实际安装 APK 为 `b8f4020f77d9c82de22dd51ba2a5ed0a09c1a14531402b7412c2c090ad536f82`，host ELF BuildID 为 `d34eeef5781b67efc32d04f8d04b7491090074a8`，运行时源为 `1bb086a`。原 f830 驱动、原始改写模块、950 shader 链、native 资源包不变。固定 .60、2750×1260、target60、1MiB shared lazy、span reference，raster run、UBO plan、attachment job reuse 与 state segments 均实际开启。

同一 PID 8495 的三份官方 Simpleperf 30 秒 `cpu-clock:u`、99Hz、FP 记录，NvN 样本为 2187 / 2190 / 2167。记录无丢失或截栈，按本次精确 ELF 的 LLVM 信息归因；三个 8 秒 Perfetto 窗经官方时钟转换证明在样本内，target60、174 PSO、32 variants 均稳定。原生命周期完成 fresh save commits=2 后停止。部分其它 APP 线程缺 vendor EGL 符号，不能补造祖先。

|完整翻译线程的互斥用户态职责|三窗份额|
|---|---:|
|图像 prepare / View|8.60–9.54%|
|Buffer Pool / dependency / plan|9.22–9.56%|
|CPU source|5.17–6.46%|
|图像 group / dependency / history|4.75–6.51%|
|绑定组装余项|9.73–10.34%|
|驱动余项|14.44–15.89%|
|规划|6.55–6.86%|
|其余 draw|22.15–23.20%|
|其余 replay|10.82–12.04%|
|单列 PSO / pipeline 准备|1.71–1.87%|
|未知 / 其它|1.94–2.70%|

这些是采样职责，不是重复调用数，也不是全部可删预算。附件 actual-access 解析已仅为 0.09–0.32%；不能继续用旧 OFF 的 0.80–1.91% 当当前预算。其余 draw 已再按当前源码区域与真实祖先拆开，没有找到一个尚未缓存的 20% 循环。

|相同 Perfetto 窗|Surface 间隔数|Running ms / Surface|等待调度 R 与 R+ 合计 ms / Surface|Surface 平均 ms|
|---|---:|---:|---:|---:|
|record 1 内|236|26.214|0.264|32.982|
|record 2 内|236|26.043|0.277|33.035|
|record 3 内|236|25.829|0.318|33.031|
|无 Simpleperf 的前书挡|236|26.309|0.261|33.034|
|无 Simpleperf 的后书挡|236|26.546|0.258|33.033|

上表 Running 是真实调度时间，不乘独立 30 秒样本份额制造函数毫秒。睡眠约 6.2–6.9ms / Surface；阻塞 D 约 0.002–0.004ms。该场景不能通过只消除阻塞锁等待补足到 16.67ms。驻核、频率和 GPU 负载仍可能影响执行效率，不能把频率变化直接认定为温控原因或线性归一。

现有 owner 发布计数在各自较宽的 record 前后状态窗中也稳定：每原 guest submit 的提前提交 draw 累计约 2395、raster 热复用约 2341、texture prepare 早命中约 4186–4187、View recipe 命中约 4145–4147、Pool range 约 6153–6156、PSO lookup 约 390–391。提前提交 draw 不含最终剩余块，不能作完整帧 draw 总数；View 消费还须加约 695–696 次 MISS。这些不是函数采样次数，也不与内层 236 个 Surface 混用分母。多数 PSO 已提前复用，不能再以所有 draw 数预支 key-cache 收益。

原始及分析目录：`.local/logs/current-default-on-callchain-v1/`。其中 `root-quality-review.json`、`scheduler/summary.json`、`state-work-brackets.json`、官方 Inferno / callgraph HTML 与逐栈互斥归属可复核。`report-path-erratum.json` 保留 wrapper 结果字段中的 config 路径笔误；实际加载输入、选项与 APK 均已核验，没有为修正文案重采。

## 两项没有升级为大改的调查

- 扩展动态状态：f830 支持相关扩展，宿主尚未启用。按本次 ELF 独立核验实际 pipeline bind 返回点后，该职责为 2.33–3.47%，与上述 PSO 准备互斥；明确动态状态复制仅 0.50–0.82%。旧同停命令的 2387 对相邻 draw 中，仅 11 对基础 shader 相同而静态 raster / depth 状态不同，vertex-only 可合并为 0。大多数切换本身换 shader，不能预期靠动态状态消除大量 pipeline bind。报告：`.local/logs/extended-dynamic-state-audit-v1/`。
- CPU source 类型准入：shared lazy 的 handoff 目录没有后续 collector 消费者，但其中 source 值仍作跨 draw 描述符缓存。只删除目录的明确预算为 0.55–0.69%，不能连同缓存一起删。权威类型位可能免去部分完整遍历，旧命令在资格过滤前约 5033 槽可覆盖，却新增 36946 次 setter ID 比较、4009 类别读取与注册 / restore 维护，尚未证明净省。未为这两项启动新的运行时构建矩阵。报告：`.local/logs/cpu-source-type-authority-v1/`。

## 稳定整池 Buffer：已证明的 GPU 功能

当前 shared lazy 给各有限窗口 / cover 建独立 VkBuffer；同一逻辑 Pool 的物理句柄会随范围变化。新的表示是每 Pool 保留一个连续虚拟地址的 sparse VkBuffer，按首次实际访问映射原共享内存 child。它避免必须物理导入整个大 Pool，且能跨 child 保持同一 Buffer 身份。

直接可定位的旧 span 选择预算只有约 1.2%；不能把整个 Pool 父成本算成潜在节省。页账本、CPU 发布、真实 hazard、native Image 别名和资源保活仍必须保留。稳定身份能否额外降低 descriptor 变化和 overlap cover 管理，尚未量化。

实验驱动基于实际 f830 完整编译输入，仅修改 `tu_buffer.cc`、`tu_buffer.h`、`tu_queue.cc`：允许受限 Buffer 的 HOST external + SPARSE_BINDING / 可选 SPARSE_RESIDENCY 组合，保留 Image sparse、SPARSE_ALIASED、capture replay、dedicated sparse 等限制。没有修改 KGSL feature 探针或强行开启硬件能力。

- 原 f830 SHA：`f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce`。
- 独立实验 ICD SHA：`a90c1b4debbf71a7b88cf7c5daa8bd0e71a03b7747629b40906ef81921d824ff`。
- 构建闭包：`.local/build/turnip-sparse-host-buffer-v1/source-ready-freeze/driver-source-ready.json`。根核验其 25 项输入 / 产物引用；32 项实际策略源码的 host ASAN/UBSAN 检查通过，这些不算 GPU 验收。
- 独立测试 APK v3 SHA：`584baa678350755b34a9550391dba9c1ecfbf126beca5fbda102be59e4e2288c`，源码 commit `9f8f16b`。仅 `com.d2rmobile.sparsehostfixture`，无游戏、ROMFS 或存档内容。

同一 Q735 的有限真实 GPU 结果：

|路径|结果|
|---|---|
|原 f830 普通 host import|CPU pattern→GPU copy 与 GPU fill→CPU 校验通过|
|原 f830 普通 child sparse|跨 child copy / fill 通过|
|原 f830 exact host+sparse external query|合法拒绝，未提交非法绑定|
|实验 ICD 两个 host child 组成同一 Buffer|完整 copy 与跨边界 fill 通过|
|8MiB virtual Buffer 仅映射第 1、第 6 个 1MiB 窗口|只访问驻留范围，读写正确；不声称未绑定洞返回零|
|完成旧访问后解绑 / 换 child 重绑|相同 Buffer 读到新内容，旧 child 哨兵不变|
|非零 child offset|绑定 2MiB import 的后半，正确读写且前后哨兵不变|
|实际解绑 / 对象清理|全部完成后销毁，进程退出|

实验臂 22 次真实 fence wait 全成功；copy 检查 7,348,224 字节，fill 与哨兵检查 8,388,608 字节，非零偏移检查另 2,097,152 字节。驱动路径由实际 dispatch 来源、私有 ICD 哈希、maps 和 UUID 核验，禁止系统 fallback。没有启用 validation layer，不能声称零验证错误。

原始证据及根复核：`.local/logs/sparse-host-child-device-v3/`，其中 `root-final-review.json` 与两个独立 arm 的 events / maps / identity / bytes / fence / cleanup 均保留。

早期 v1 漏原 JNI 的 `TU_DM_HOST_IMPORT=true` 启动选项，只到能力查询就退出。v2 的 malloc 来源 host allocation 在普通导入前置失败，未执行 GPU 数据操作；随后 v3 改用原 KGSL probe 的匿名 mmap 并记录真实对齐 / high byte 后通过。v2 未记录原指针 high byte，不能把 MTE/TBI 解释写成已证原因。各失败与后继分开留存，未反复覆盖成 PASS。新包安装还遇到 Android installer 确认与 PM 默认查询其它用户的权限问题，均在独立 fixture 范围处理，未修改系统服务。

## 后续接入边界

v4 后继现已在同一 Q735 完成 Shader 与异步排序资格，没有重复 v3 transfer 对照：真实 compute 从跨两个 child 的 UBO / SSBO 读取，向另一 sparse Buffer 的跨 child SSBO 写入。同一个 descriptor set 只初始化三项，三个 dispatch 仅改变动态 UBO offset 0 / 256 / 512 和 salt，并在中途换 child 后还原。三轮 384 个 uint 结果、各 6,291,456 字节输出 / 哨兵和只读输入检查全部正确。word-address 哈希输入可检出动态偏移被忽略的错误。

三轮分别使用 binary / timeline / binary semaphore：`vkQueueBindSparse` 不带 CPU fence，直接信号连接消费者的等待；仅最终消费者 fence 由 CPU 等待。三轮 GPU 执行与最终解绑均成功，不能将 GPU semaphore 依赖描述为没有同步。v4 APK SHA 为 `465a676ef6c854f1220349736ea56b61bf610d949f0069125554304ef078f174`，源码 `a557408`，实际结果在 `.local/logs/sparse-host-child-device-v4/`。这些仍未覆盖 graphics VB / IB / texel、native Image 别名或生产 Pool 生命周期。

生产接入已在独立 `codex/pool-sparse-backing-v1` 编写并通过初次 host 编译，尚未完成真实游戏 GPU 验收：首次页映射、每 recording 的 GPU 等待、旧访问结束后的解绑回收、unknown-length VBO 尾部和 native Image 别名均须保留。没有把 probe 当原游戏里程碑。实验 ICD 补丁与基线来源校验保存在 `tools/experiments/turnip-sparse-host-buffer/`；它不由默认构建应用。

根已核对日常 APK 与 f830 未更换，游戏和本轮 fixture 均退出，暂停借出的设备未使用。本轮没有新增 FPS / 温度 / 全场景稳定性收益，16.67ms 目标仍未完成。

## 生产接入的首次实际启动：未通过资源身份门

后续接入冻结到 `0730e5c`，增加了映射票据的独立 Pool 保活、真实 consumer fence 与 CPU effects 后释放、关闭时锁外解绑等待，以及所有存活 sparse Buffer 的虚拟地址总额检查。canonical APK `ab776770535307b636944d65c7c4c0b28d1053c3ddb4f48039fc3839914927c7` 的 1035 项源码、六库和原始 NSO 重建结果经根独立核验；24 项执行／规则产物与原安装相同，保留原安装的已验证 provenance。

同一 Q735 实际安装该包与 a90c 后，先以 sparse OFF 验证同提供者的参考路径。原 memfd / overlapping import GPU byte gate、shared lazy 均通过；随后原生纹理表主动拒绝新的驱动身份，报 `Native resource table identity/producer/pack/recipe rejected`，在进入游戏前退出。**这不是 sparse ON 游戏结果，也没有性能数据。** 原 6217 条组合表和其中各 recipe 均绑定 f830 身份，2838 只是其 ASTC6 替换条数，不能仅重产 2838 条再混入旧 3379 条。

另有一个已确认的冷路径缺口：native resident 策略只认 aa185 / f830；即使有新表，a90c 仍会走 FOREIGN 交接。因此候选 `c3a3469` 另行添加 exact a90c 的 resident 合同，两臂共用；严格的 table / recipe / producer / pack 校验不变，实际 native Image 资格仍待验证。不得把 a90c 的 FOREIGN 与 f830 的 resident 差异算成 sparse 开关收益。

原始失败、安装 CAS、恢复证据在 `.local/logs/pool-sparse-game-device-v1/`。失败后已恢复并核实原日常 APK、f830 驱动及原 28 项模块／规则／provenance 身份，游戏退出；不为已退出进程重启存档。旧表和纹理没有被修改。后继拟复用全部 6217 条已有压缩输入，经原 8064 producer 在 a90c 上真正重新生产并保存 roundtrip / 查询记录，不重编码 ASTC、不改旧 recipe 的 UUID 或字节。

## 逐 draw 准备的次数与可删除预算

当前三窗每 guest submit 的提前提交 draw 累计约 2395（不是完整帧总 draw），texture prepare 早命中约 4186，View recipe 命中约 4146、MISS 约 696（合计约 4842 次 View 消费），Pool range 约 6154。这些是不同语义计数，prepare 本身只计早命中，不能把它当全部调用数或慢处理数，更不能当重新建图或上传。采样能定位职责，不能单凭调用栈得知对象内容或类型是否变化。

当前 image prepare / View 父桶的互斥细分为：shader 遍历／组准入 0.91–1.15%，逐 image prepare 1.34–1.65%，View 消费 6.04–6.89%。View 还包含布局／反馈、请求构造、物理元信息、持久 View 查询、上传前置、native 保活和逻辑 recipe 交付；并非全部是 Vulkan 对象创建。详见 `.local/logs/current-image-prepare-roles-v1/`。这些是整线程份额，不与父桶再次相加，也未全部认领可省。

规划和回放的当前互斥下钻已保存到 `.local/logs/current-planning-replay-reuse-v1/`。Store 收集／逻辑解析为 3.11–3.43%，每 guest 约 11997 次 descriptor bucket 访问，其中约 529 次 miss；有高频命中，但命中之后的必要事件交付尚未分离为可删除成本。forward walk / proof 为 1.69–1.87%，余下 replay 中提交／资源收尾实际占整线程 3.25–4.25%，不是重复命令解码。旧完整快照只有该次提交内的地址身份，没有跨帧命令版本证明，不能直接缓存整份 plan。

另对已有完整 OP 快照核查了整组绘制失效：152 个宽松“后续同纹理 clear”匹配中，141 实为不同 depth layer，8 个涉及实际被读取的 mip 链，另有原 zskip 路径及未知附件。没有建立新可安全删除的正覆盖；`storeOp=DONT_CARE` 不等于产生该结果的 draw 可以删除。见 `.local/logs/dead-draw-group-qualification-v1/report.md`。没有据此增加通用 pass-culling 框架或认领 draw 父桶预算。

## 新驱动完整资源重产与首次游戏正资格

严格身份拒绝后，沿原 8064 producer 对当前表全部 6217 条资源重新执行实际驱动生产，而不是重标旧 recipe。49 个冷批次全部通过，原 oracle 检查合计 2,894,062 项；输入共 325,014,432 字节，保留 2838 条 ASTC6 与 3379 条 ASTC4，不重新编码压缩块。首个真实完整产出锁定三 UUID，后续全部一致；布局 UUID 仍为 `ab91c540847448360a44496b50045159`，a90c driver UUID 为 `26054bbdfd44cbda9282e14995b69a68`。每批的实际私有驱动 maps、UID、源／产物哈希及压缩块 roundtrip 均留存。原日常 APK／模块／shader 身份前后相同，生产器包和临时文件已清理。

实际结果在 `.local/logs/sparse-native-rebake-a90-actual-v1/`，根复核 `root-finite-review.json`。根执行原 packer 的完整封装，6217 条新旧 payload、完整 CFT、descriptor、thumbnail 和 family／ID 均逐条相同；只有新的资源表及身份元数据需要单独发布。该结果允许保留原 CASC／descriptor cache，不是把原压缩数据伪称为新生产。封装和逐条比较在 `.local/build/sparse-native-compose-a90-v1/`，入口／闭包在 `.local/logs/sparse-native-compose-v1/`。

新表 SHA `95f31c0e88a1b6a920a28cb7b35efb7cb8c49cac6f47ffbef7febd67937ad8b2`，pack 身份 `36a01e28d8e21738602c0655152a62e5e9811e8eb7829c295a831964b6d6d1a5`。表部署到独立文件，原 f830 表不动；两个受控臂均为同一 `c3a3469` APK（SHA `c2a79af0055df6822e57fb11f84527a72d8645a74340de1aed2c8ce40f4218a1`）、a90c、相同新表／shader／原执行模块／原 CASC。

真实游戏 sparse OFF / ON 分别以 PID 11439 / 13246 进入原角色选择与营地，实际 shared lazy 两臂均开启，ON 的 sparse startup GPU copy/fill gate 和 first append 均出现且 `shared_sparse_pool.active=true`。两张营地截图中的角色、场景及原 HUD 可见，没有已见明显黑块／缺几何；两臂均经原生命周期 fresh save commits=2 后退出。资格仅覆盖该场景和保存，不代表战斗、全场景或长时稳定性，也不是性能结论。ON cover 节点为 0、原有限 child 仍保留；`import_allocation_bytes` 是导入范围计账，不可直接当物理 PSS／DRAM 节省。完整记录在 `.local/logs/pool-sparse-game-device-v2/limited-game-qualification.json`。

同包 OFF / ON / ON / OFF 的系统 Simpleperf group read 与 Perfetto 对照已完成，使用原 reducer 的完整及机械可观测窗口；四轮均存档两次并退出。

|臂|开关|Surface 区间数|用户指令 M / Surface|用户周期 M / Surface|NvN Running ms / Surface|Surface 均值 ms|
|---|---|---:|---:|---:|---:|---:|
|A1|OFF|222|57.658–58.108|34.354–34.652|26.737|34.871|
|B1|ON|235|57.702–58.128|33.709–33.983|27.006|33.075|
|B2|ON|236|57.500–57.966|34.651–34.962|26.712|32.940|
|A2|OFF|235|56.809–57.149（覆盖子集）|34.834–35.122（覆盖子集）|25.782|33.075|

A1 / B1 / B2 的实际驻核计数覆盖 100%；A2 完整窗只有 98.90768%，CPU1 / CPU7 未齐边界，表中不是整 TID 上界。预先规定的 observable 裁剪到 165 个区间后仍缺极少 CPU1，覆盖 99.995595%，不能用它制造完整上界。A1 / B1 的 wider state variants 均 30→32，后两臂 32→32；实际资源集合和驻核／频率也有差别，全部保留，不按 MHz 归一。

首对整线程指令区间重叠，周期较少 1.08–2.72%，Running 反而多 0.269ms；第二对没有完整双侧总工作量界，Running 多 0.930ms。**本次没有证明可重复的整线程指令、周期或 Running 减少，更没有达到 60FPS。** 结构证据是 ON 的 cover 为 0，导入记账 347–349MiB，OFF 为 119–122 个 cover、555–559MiB；不能称物理内存节省。OFF 两臂分别出现 24 / 226 个 queue-completion wait 相交标记，ON 未观察到该标记；尚待按线程、父调用及具体原因审查，不把标记 wall 总和当独占 CPU 或直接认定涨帧。

完整对照在 `.local/logs/pool-sparse-game-device-v2/four-arm-offline-review.{md,json}`，134 项原分析输入哈希经离线复核。根恢复并核实原日常 APK、f830、原 28 项模块／provenance、旧表及三个原 CASC／配置文件，独立新表已移除，游戏退出。该实验分支已推 `fork` 的 `codex/pool-sparse-backing-v1` 作备份，主线默认未开启，60FPS 目标仍未完成。

## 等待位置的后续核查

原 TraceProcessor 下钻确认 OFF 标记发生在同一翻译线程的 replay 内，调用共用 `vk_internal_wait`；不是本次 WSI/acquire、guest fence 或 recording 资源组压力。trace 没有调用方、目标资源或 serial 参数，本次指令 PMU 也没有 callgraph，因此还不能唯一归因到 CPU Image observation、host phase 或 commit。不能凭 sparse 地址稳定就取消这些等待。

A2 的 queue-completion slice 内 Sleeping 合计 1651.805ms，同时该窗 `vkQueuePresentKHR` 内 Sleeping 为 0；B2 对应是 0 和 1406.789ms。这是**等待出现阶段变化的线索**，不能称消除了等量整帧等待。A1 / B1 的 present 睡眠也均约 1.3 秒。相关 slice 的 Running 只占很小部分，wall 总和不计作翻译 CPU，也不能与其再相加。报告及官方 SQL 见 `.local/logs/pool-sparse-wait-origin-v1/`。

当前已证物理 union 查询只占整线程约 1.19–1.74%，Store 收集约 3.11–3.43%，图像 prepare / View 也已经拆成多项已缓存职责；没有发现可直接删除的 10–20% 单一循环。后继实验需要同时解释必要工作量与实际执行速率。早先现成 `run-as taskset` 在 `sched_getaffinity` 即被 Android 拒绝，没有改变亲和性，也没有有效 A/B；其记录在 `.local/logs/nvn-affinity-diagnostic-v1/session.json`。独立启动时 owner-self affinity 诊断用于弥补这一工具权限缺口，默认不改变调度，不加 worker／计时器，不升频或改温控；尚未完成真机对照，不能称优化或默认绑定大核。
