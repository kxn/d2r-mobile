# Turnip native-family：7c 离线组合候选

基线 `7c60ff432781d6b631676704b51e13047c8548a1`，不修改主工作区和设备。
既有 7c shared VkBuffer、CPU-observation/NC gate、BC/ASTC legacy、image binding classifier 和 balanced alias interval 均保留。
仅选择性迁入共同祖先 14dae 后的 native resource/family 能力及本次 Turnip 身份接线；没有把 fbd 旧 shared 实现覆盖回来。

新增消费者沿用 DMNRES1 / DMNTBL2 / 8-byte packaged marker。HLE 在 Builder GetStorageSize/Alignment 时按 full/tail 真实形状分类；full 使用实际 Vulkan queried 排布，thumbnail 恢复原 NVN 源排布。Pool 持有导入内存，VkImage 仅绑定同一 VkDeviceMemory 的经验证 offset；每次使用仍保留原依赖、buffer alias、writer publication 和生命周期保护。
编译选项默认 OFF；没有 table 时不启用 native image。表安装核实际三 UUID、完整驱动 SHA、实际 producer 和 pack SHA，不能用 BuildID 代替 UUID。

普通游戏冷启动沿原 launcher/Java/JNI，新增三个成组参数：
`--native-resource-table /data/user/0/PKG/files/runtime/native-layouts.bin`
`--native-resource-pack-sha256 <64hex>`
`--native-resource-producer-sha256 <64hex>`
并须显式 `--shared-guest-heap --vulkan-provider adreno`。不能 attach/热切；Intent 仅为请求，实际安装要读 native identity 记录。

## 资源阻塞边界

已证明真实 firepit ASTC4 256×256/9mip 在普通 UID 下，经实际 aa185 producer 和原 NVN family 307 assertions 正确；这是 fbd+Turnip 有限 owned oracle，不是本次 7c 真游戏验证。
当前 Q baseline 为 BC/UI4/950。已有 owned bundle 仅含一条 native 表和测试输入，`deployable_game_pack=false`，不能直接挂载游戏。
真实单 entry 事务必须包含：

- 保留现 Q 完整 texture_desc_cache 的全部未选项；该一个材质先按原资源工具形成匹配 ASTC4 的 format/header/thumbnail，再应用 native marker；不能保留 BC3 thumbnail 或复制全 ASTC cache。
- 同一材质完整 native .texture（135168-byte opaque payload、真实 mip table）与匹配尾级 thumbnail；marker/index 与 DMNTBL2 一致。
- 1216-byte 单 entry DMNTBL2，以及实际 producer JSON/wire/driver SHA/三 UUID 的来源记录。
- 原 CASC overlay 的实际 data archives + merged index/TVFS/完整 cache 等闭包，沿 `casc_overlay.py`；精确备份和回滚原资源，不改游戏 binary。

旧 compose-native-resource-overlay.py 有 PanVK driver/producer、31072-fullASTC4、NC flags11 等验收硬编码，不能直接用于当前 BC/Q/Turnip。当前 pack-native-texture 的 base-cache guard 也有意拒绝除 layoutData 外的变动，不能放松以偷换 BC descriptor。需先做可复现的单材质 BC→ASTC4 资源组成，再喂严格 native pack 阶段；这轮没有制作或部署 gamepack。

## 成本与验收限制

表常驻 160+1056×family_count 字节，本单 entry1216B；启动文件快照+安装副本短暂双份。image 记录、mip histories、真实 acquired list 和 pool ref 保留；冷创建查询和 per-use alias/dependency/ownership 操作仍存在。不会省原文件读取/解压/写进 shared heap，也不保证比原 NVN padded89600B 更小（本 native payload135168B）。不把单 backing 当 RSS 或帧率收益。

该组合待独立 review / canonical ARM 编译 / 7c owned GPU 回归后才可讨论实际游戏单 entry；旧 fbd PASS 不替代本候选实际验收。无设备动作、无性能声称。
