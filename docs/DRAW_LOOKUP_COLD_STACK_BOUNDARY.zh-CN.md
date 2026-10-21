# 绘制热栈与可选 lookahead 分析边界

本候选基于冻结 f066f92，仅将 `bq_prove`、`pl_plan` 标为 noinline。纯逻辑 descriptor owner cache、原 lookahead 条件和结果、物理 binding scope、资源/hazard/CPU ready、ghost 回退均不变。没有修改全局优化、TLS、ABI 或任何游戏 binary。

原 API29/O3/TLSDESC APP 编译命令的独立 draw TU/ELF 差分显示：f066 的 `vk_draw_record_impl` 固定帧为 33,456B；只隔离 bq 入口仍为 25,072B。O3 IR 证实另一个大局部对象是 `pl_plan` 的 `vk_state scan`，而不是已经证明的递归展开。选定边界后 impl 为 6,496B，原 scope wrapper 仍为 48B；bq proof 自身 29,552B、PL planner 自身 20,096B，只在对应分析调用期间存在。633 的热 draw 帧为 6,464B，但其独立 bq 入口本来也有大帧；这项改动不声称降低整个调用链峰值栈、每 draw 少复制 27KiB，或已经降低真机 CPU 时间。

比较的两个最窄变体完整保留：A 为 `bq_before_draw` + `pl_plan`，热帧 6,480B；B 为更内侧 `bq_prove` + `pl_plan`，热帧 6,496B。选择 B，使 bq 的原早拒/已证明链处理仍可内联；PL 原 draw 调用点已有 enabled gate。代价是实际进入分析体时的普通函数调用/返回，未新增锁、资源、查询或运行时统计。B 的这三个命名分析/绘制函数代码大小合计 32,972B，对照 f066 impl 33,260B；这也不是执行时间测量。

证据入口为 `.local/logs/draw-lookup-cold-stack-boundary-v1-source-ready-agent.json`。它保留原 flags 下的编译/链接 argv、A/B 和首个不充分变体的源码/ELF/反汇编，以及官方 LLVM IR/DWARF 证据。所有差分产物输出到独立目录，未替换 f066 canonical 的对象、ELF 或 APK；本候选尚无完整 Android canonical、真机、原游戏执行或性能验收。

复用原 host GPU logical（包含真实 ghost、重注册/retire/CPU 与 GPU 写入）和 native retirement fixtures，异步/串行各一组，在 Lavapipe/LP2 Core+SYNC 与 ASan 下通过。logical 各 74,652 checks；retirement 各 3,120 checks，真实 RGBA 导入/像素，非 ASTC 硬件验收。未增加计时器、采样器或 mirror 测试实现。父级先前原因报告 `draw-lookup-compiler-frame-root-v1/result.json` 保留为定位线索，其中的递归内联猜测不作为本候选已证因果。
