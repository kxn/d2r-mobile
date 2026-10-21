# 复用既有 binding consumer 的逻辑 Pool 准入

基于冻结53c7d26；只将既有8字节 `vk_binding_consumer` 内部定义前移，供 Pool range 收集与 descriptor 消费共同使用。不创建第二份 schema、不增加 shader/operation 常驻内存、缓存键或资源 epoch。

完整 schema 与 pool mask 均有效时，Uniform/Storage 准入从既有 consumer.source 取得传入 `vk_state` 的字段，使用 consumer.op 的 writable 与 aux 的 bias slot。每次仍读取实际 address/size；传入状态可以是 ghost 的独立保存状态，不能改成读取全局 S。替代 shader 使用其自己在最终 constructor 生成的 consumer。未知/未编译/mask 未证明或 reference 模式保持原解释路径。

零缓冲、完整 UBO/SSBO 声明范围、溢出拒绝、sink 输出顺序和失败、prefix 对齐、texel/native 枚举、物理准入/pin、原 CPU dirty/ready 和 GPU hazard 全部保留。真实每 consumer 的 resource effect 不能因 schema 命中而跳过。新增成本为每活动 stage 一次既有缓存 reference-mode 读取、既有8字节 consumer row读取及 opcode 分支；不会每 draw 重新 getenv。零新增 retained bytes 不等于零指令成本，净 CPU 结果仍待原官方采集核验。

现有 d148 真机632 NvN样本：operation_begin50（7.91%）内含 collect23、pin23及其他4；unpin7在操作尾，record_pool_range15和shared_pool_cache_locked14不属于准入。collect23中明确 type/register 解码 PC6（占全NvN约0.95%），构造输出4、native/source9、wrapper/dedup3及归属不明1。这是候选覆盖边界，不是6个样本全部消失或预计节省毫秒，更不能把包围的3.64%或7.91%算作可全删。

已有 pin-first、owner去重、span hint/index、operation geometry 和 source handoff 已排除重复建设。独立操作 pin 必须跨内部 commit/wait/reopen 保留；提交引用可能在该边界完成，不能直接替代它。collect 中存在可加资源锁的 lookup，不能将整 collector 放进 V.resources 锁来消去第二遍。

验证沿原 CMake host、LVP Core/SYNC、原 range 与 GPU 内容夹具；加入 live state addr/size/zero/overflow、保存状态、prefix、各stage及 sink 失败与原解释器逐字段对照。host/LVP 不算 Android 原游戏验收，不访问设备或改游戏模块。
