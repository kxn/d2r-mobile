# Persistent real-SSA chain / postsched v2

状态：独立 host driver 构建完成，编译探针 APK 已准备；最终 native 顺序与 GPU 性能尚未验证。保留 v1 源/ELF/收条，未在设备操作或交付游戏。

输入是完整核验的 v1（592a）源树和原工具链；全源 ledger 验证后复制，唯一源码差异三项：ir3.h、ir3_sched.c、ir3_postsched.c。旧 v1 和候选全树均在真实构建后再次核验。UBWC、host import/coherency、WSI、UUID 算法、图像布局、emitter、SPIR-V、普通库及游戏原始资源不改。

实现：

- 在 `ir3_instruction` 增加专用 `uint8_t dm_memory_chain_priority`，不复用 MARK、flags 或 pass-private `data`。
- 前级 schedule 仅依据已经存在的 `critical_chain_node(ctx,n)` 资格存 `memory_depth+1`，0保留为未选中。此深度来自前级真实 SSA 数据边，原 false-dep/WAR/WAW 不用于推断。
- 后级只在同一通用结构条件（FS首个非preamble block）选原物理 DAG 的现成 head，且 `node_delay==0`。原 meta、input、hard-ready kill/demote 仍优先。其他候选按携带的真实链深度排序，再按原max_delay打平。
- 所有原forward/reverse物理依赖、特殊寄存器、kill限制、required SS/SY与hard delay计算不改。关键consumer允许越过**soft** SS/SY延迟隐藏偏好，但必须保留该consumer原需的SS/SY标记，不能无等待读未完成结果。soft bias和硬件合法性不是同一件事。
- metadata 不参与硬件编码、不制造新DAG边、不删除依赖、不全局减少outstanding，不按纹理/Shader key/游戏地址做判断。

传播与保守边界（按真实源）：

- ir3.c `instr_create()` 使用 linear_zalloc_child，新建copy/spill等默认0。
- `ir3_instr_clone()` 的 `*new_instr=*instr` 保留专用字段，随后只复位原有数组/uses/repeat链；没有指针悬挂或复用pass data。
- rpt group 是原instruction的prev/next连接，没有另建遗失tag的成员。
- 原RA通常原地修改物理寄存器；真正新插入的copy/spill不猜测继承。若它们挡住关键节点，原DAG仍保证合法性；可能影响收益，但不能为了优先级跳过这些依赖。
- 新元数据是排序提示，不是资源语义。RA删除某个旧计算时，该指令及其tag一起删除；不按地址把tag移到无关新指令。

成本：官方clang采用两版实际target编译命令 `-fsyntax-only -Xclang -fdump-record-layouts` 独立核验：ir3_instruction **均184字节、align8**，专用byte位于原padding（见 instruction-metadata-layout.json），没有每instruction分配体积增长。新增编译期每节点一次资格写入，以及postRA每次选指令对现成DAG heads的一轮筛选；这可能增加驱动编译CPU时间，本次没有计时或测量。运行时每帧CPU路径没有新管理层。最终GPR/mov/nop/alias/指令次序可能变化，必须用实际native看，不声称一定改善。

缓存：新policy hash通过原 `tu-build-id` 标准入口进入pipeline/diskcache身份，IR3已有ELFBuildID也独立变化。v2 tu_build_id94941f4e…；新ELF SHA24802641… / BuildID0b41d0ee…。不改UUID生成算法，不复用v1 pipelinecache；实际UUID须由探针标准属性取得。source-only图像布局未变也不能预先替代实际layoutUUID资格。

下一步最小资格：仅运行 `.local/logs/fs-critical-schedule-v2-compile-v1/run-compile-probe.py` 两个原始PSO（只有fs官方IR3输出，无draw/submit/timing）。先看最终t6/首动态UBO是否早于材质sam，19full/10waves是否保持，新增mov/nop/alias/代码尺寸。若机制未传到最终代码，沿既有阶段诊断，不直接重采游戏；若成功再做整轮游戏profile。原始driver/数据从不手工patch。
