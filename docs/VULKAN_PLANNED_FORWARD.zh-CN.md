# Vulkan 首遍计划节点写入

基于 main `36bb836`。d5 官方 NvN 用户态 624 个样本中，walk 为 14 个 self，其中 3 个 PC 位于节点写入；这只用于定位，不能把 14 或 3 个样本称为可省时间。

`planned_op` 仍为 16 字节。把首遍需要初始化的 draw、drawing_writes、opcode、sample_counter 四字节放在一起，用字节数组的 memcpy 写入。逆扫输出 need、sample_live、writes 不再在首遍清零。实际 NDK 28.2 O3/API29 游戏宏的独立目标文件中，节点初始化从 5 条 store（共 15 字节）变为 2 条（共 12 字节：8 字节指针及 4 字节 forward）；打包 opcode 多一条移位，没有新增条件分支、查询、资源、锁或元数据。此对象对照不是完整 Android canonical 构建，也不是实际游戏提速证据。

成立条件是：所有根 handle 的 walk 均成功后，完整逆扫对每个节点都写入这三个逆扫输出，之后才建立 store index 或回放。walk 失败立即撤销 valid，旧计划不可被读者使用。所有读者按字段名读取；clear-group 使用实际 opcode 字段地址和 16 字节 stride，未依赖原偏移。原 header 校验、CALL/JUMP 深度和拓扑限制、逐 op 游标与 done、失配回退、RT 变换及 raw header 重验均保留。没有证明原始游戏命令字节在 index 到 exec 之间为 owned immutable，因此不复用 raw header 验证。

宿主独立测试用真实 submission_index_build：复用存储先填 0xa5；交替初始 DS、命令内 DS、stencil 写掩码、RESET/REPORT；同一 child 在多个根及 CALL/JUMP 展开位置被复用。逐节点验证全部 forward 和 reverse 字段，验证失败 prefix、短 BIND、递归 CALL 后的保守读者及下一次成功复用。测试入口只编入宿主 DM_VULKAN_TEST_EMBED，不编入 DM_ANDROID_APP。接着运行原有 DS 两格式/独立平面 STORE/NONE 像素和 counter 前缀、跨提交、重复 child 两半像素 oracle，异步/串行及 ASan/UBSan，Core/SYNC 验证。复用的 DS fixture 零数量顶点状态绑定改用非 NULL stand-in，避免原 HLE memcpy(NULL,0) 的 UBSan 属性报错；数量与所有期望不变，未修改生产 HLE。故障对照让默认 sample_counter 保留非零，分别由复用字段及原真实 GPU query 前缀 oracle 拒绝；不改变像素/计数预期。

源与证据入口：`.local/logs/planned-op-forward-write-v1-*`；最终独立 source-ready 包含完整相对路径归档和所有哈希。宿主 Lavapipe 结果不是 Android 原生游戏验收；候选此阶段不构建 APK/rawNSO，不访问设备，不声明 FPS、功耗或热收益。
