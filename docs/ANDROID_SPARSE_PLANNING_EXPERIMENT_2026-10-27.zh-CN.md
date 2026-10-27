# 将规划表示压成语义节点与连续状态段：实验边界

本轮来自同record真实PC/LLVM源码归属：cacheON 的 planning 约6.974M
sampled instructions/Surface，其中 walk SELF约3.389M、mtl_sub_index SELF
约1.861M。后两项不等于可删5.25M：walk包含内联Store解释器，
mtl_sub_index包含多种逆扫和控制职责。pure_state_opcode分类约0.739M，
只是一部分预算，不是整个逆向过程。

原诊断计数中UBO约7580、Sampler1016、VBUF2432、VATTR2424次/帧，
约13452/31600=42.6%的新record对DS/store本体无直接语义。
但纹理/Image/Program绑定仍影响未来Draw/Dispatch资源集合，不能当无关项删掉。
pixel-local、mip-clear、bloom和state-segment消费者还要求原始完整顺序。
因此可行实验是保留原始序号和边界的连续raw-span，不是删除这些NVN指令。

## 已完成：真实生产消费者的表示正确性试验

独立host harness直接包含生产`nvn_vk_plan.c`，先执行原planned_forward和
store_walk fixtures；候选消费者由生产函数体机械改写访问器，不另写DS/store算法。
GCC ASan+UBSan下，原fixture1405检查、19个混合提交、157251次API对照、
482215断言通过。覆盖原始future ordinal、query live、DS need/write、
state tail、replay/done cursor、indexed和reference Store、真实mip-clear正例
及256条lookahead拒绝边界。

首次显式piece为40字节，相比原16字节可能抵消节省；后继16字节tagged union
已通过相同生产消费者对照，不必扩大每节点大小。但顺序决定是否能压缩：
连续UBO组318→19 pieces，UBO/TEX交替618→618。
只看调用次数，不能证明实际游戏流能减少42.6%规划节点。

原始证据`.local/logs/sparse-planning-design-v1/`：
`real_planner_oracle.py`、`real_planner_run.stdout`、
`selected-actual-planning-PCs.json`、`official-source-symbolization.json`和
`source-budget.json`。

## 没有完成：省掉规划构造工作的实现与性能

上述oracle仍先运行原dense构造，再压缩；它只证明新表示能承载这些生产消费语义，
不证明CPU变快，不是可上线优化，也未运行真实游戏或Android GPU。
将其称为已节省规划时间会是错误结论。

后继已并行开始的独立候选必须在walk时直接输出16字节anchor/span，
并让逆扫消费piece，避免先dense再压缩；原始ordinal的future/store/query/DS/
state-segment契约须保留。冷开关默认OFF、不得关闭原GPU优化。
还须检查稀疏游标、随机lookahead、span内原始解析的新增成本；在交替退化场景
不能仅依靠相同节点大小假定没有CPU退化。
通过host语义试验后仍需真实GPU回归和同APK真机工具对照。

另一路前端候选在独立worktree核查Stencil和顶点布局的同段精确状态去重：
目标是先不录制重复setter，而不是跨帧猜资源不变。约4880次Stencil及2424次
VATTR只是适用次数上界，尚未取得该扩展的实际命中或性能，不纳入已发布收益。
本轮已发布的仍是六类固定状态去重，见独立报告。
