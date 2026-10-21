# Pool 准入几何证明合并（source/host 候选）

依据 actual8480 Q735 官方 Simpleperf 的 605 个 NvN 用户态样本：`pool_operation_pin` 和 `pool_address` 各 21 个独占样本（3.471%）。pin 样本分散于 owner 区间查找、原 span 提示验证、几何记录等；不能认为本改动消除整个 pin 叶，更不能把其 inclusive 比例换算成已省毫秒。来源：`.local/logs/native-candidate-source-reuse-8480-Q735-v1-analysis/native/official-full-samples-callers.json`，ELF `93d4381e1406b157a89c9da20c73a9e562c703972b2b1c6d62fa46794d77562d`，Build ID `7ba1705f53ff41ff44899ff1a1d54af58faced95`。

`pool_operation_geometry_remember` 只有 `pool_operation_pin` 一个调用点。调用方在同一 `V.resources` 锁内，先用 `pool_find` 或本 operation 已 pin owner 的范围检查证明请求属于 Pool，再用 `pool_span_resolve_operation` 证明请求在一个真实物理 Buffer 中，随后增加本 operation 的引用并记录几何。中间没有 callback、解锁或可改变 backing 的操作。

原记录函数又逐项重复以上证明。本候选让该私有函数只打包刚验证的 span，保留 row/owner index 表边界检查。没有放宽上游 resolver 的任何拒绝路径：

- 单节点/whole 分支的 `pool_span_resolve` → iterator 检查 Pool 请求范围、实际节点 handle/generation/范围、Buffer extent，且拒绝跨节点/空洞。
- 既有 locked/recording span hint 分支检查同样的物理包含关系、generation、handle 和实际 Buffer extent。
- search/index 分支同样验证实际节点，OOM/大链仍用原解析器。
- 每个成功输出都由原解析器构造 requested logical/size 和 `logical - backing.begin` offset；记录函数不接受外部 span。

因此删除的是同锁同范围的第二次验证，不是首次验证；未来若新增调用者或在两步之间插入 callback/解锁，必须重新建立这一前提。原 independent Pool pins、registry epoch、实际 geometry 命中 generation、当前 recording ledger、CPU initialized/dirty、native/alias、hazard、GPU last_use 和 completion 均不变。无新增生产内存、无新 cache、hash、计数器、线程或计时器；原 geometry row 仍 32 字节。

资格使用原 C NVN/Vulkan 后端，在 x86 Lavapipe 软件设备执行，不运行真实游戏模块，也不是 Android/GPU 硬件验收。原 geometry 用例增加通过真实 pin/resolver 路径拒绝坏 generation、空 handle、过小 physical buffer、坏节点区间、跨空洞、pool tail 越界和地址溢出；故障限于未使用的 fixture Pool，恢复后才允许后续真实 GPU 消费。原 GPU 字节、嵌套 pin、completion/reopen、同址注册更换继续测试。测试 helper 仅 `DM_VULKAN_TEST_EMBED && !DM_ANDROID_APP`，不在生产 APP 中。

现成 profile 里显著的是 CPU Running，不等于已排除所有细粒度同步成本。pin/unpin 每个 operation 仍分别持一次 `V.resources`，unpin 保留原引用递减和 `pool_cv` 广播。引用计数为锁内普通整数，registry epoch 是 acquire load，几何 generation 是普通读；这些不是每 binding 新增的原子 RMW。completion 也会更新同 Pool 引用，可能有 cacheline 迁移，但当前采样不具备其量化证据，不把未测 false-sharing 或锁等待当作 26–27ms 的主因。近期 shared native camp 已不是旧平板 startup 的整链扫描负载，历史 43% 不重复算作当前收益。

成本边界：减少少量重复分支和地址运算，不新增 metadata；没有声称已降低整帧 CPU、带宽、发热或 FPS。后续若 root 采用，沿现成官方工具检验适用性，无需为此新建计时器。

进一步按 inline PC 拆分 pin 的21个样本：只有4个归到 `geometry_remember`，其中2个明确在本次删除的重复 bounds（原50/51行），1个在仍保留的哈希写表（原58行），另1个无行号不可归属。2/605=0.331% 是本次采样的明确PC覆盖，不是测出的 CPU 节省。当前候选冻结为低成本正确性已证明的小修，因用户要求先审整个执行路径，暂停 Android 构建和真机部署，不能作为下一大收益方向。
