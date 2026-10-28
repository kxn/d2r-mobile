# v3：用有用 ALU 填关键 read 的硬等待槽

输入：完整核验的正常 v2 / 2480 源树、工具、ELF记录；唯一新增源码差异 ir3_postsched.c。v1/v2/诊断源与ELF全部保留，不动生产runtime、shader、图像布局、hostimport、WSI或UBWC。

实际原因见 fs-critical-schedule-v2-diagnostic-v2/DIAGNOSTIC-COMPILE1-analysis：四native准确等于248，官方head日志证明t6 priority3/parents0且UV14前缀tag选择生效；硬delay降至2后，原expensive-texture偏好先发3/4个无tag材质，delay不降，随后才选ready ALU填2槽。这不是producer/copy/alias丢tag。已有可用hard0且无SS/SY的mul/mov/mad被官方同一snapshot实证。

v3仅在原关键hard-ready分支没有选到指令后增加：若有带tag read已经是DAG head但hard-delay>0，优先选同DAG中hard0、无SS/SY、is_alu且非scalar的有用ALU（正对应原legalize count_instruction的ALU计数条件）。按原max_delay选择，原meta/input/kill优先不动。如果没有该read或没有合格ALU，原fallback不变。

约束：不插NOP或idle、不减硬delay、不丢SS/SY、不删/造依赖、不改变寄存器RAW/WAR/WAW/特殊寄存器规则。新标记/SSA合同沿v2，既有实际184B结构不变。没有纹理ID、shader key、游戏场景或地址判断。没有跨物理false edge回传、不全局限outstanding、不关闭postsched。新循环仅编译期，不加游戏每draw/每帧CPU管理。

收益条件与风险：若空隙本可由ready ALU填满，非关键纹理无需先发，关键读取能更早启动并避免过早合入同SY producer组。这是顺序/并行性优化，不减少load数量或计算量；可能损失其他纹理的提前重叠，最终GPU收益不能从静态顺序推断。必须先实际两PSO检查t6、首UBO、材质顺序、GPR/waves/mov/nop/alias/代码尺寸，再决定整游戏profile。

完整源ledger前后复核，官方cachepolicy/tu-build-id独立，正常fs探针不含诊断schedmsgs。实际driver/layout/pipelineUUID等待标准属性查询；未改UUID算法或重用旧cache。
