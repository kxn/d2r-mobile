# Local shader compiler compatibility changes

Remove the unused `gl_CullDistance[1]` member from the SPIR-V per-vertex output
block. The translator has no CullDistance I/O mapping or stores; the actual
Position, PointSize and eight ClipDistance members retain their indices and
semantics. Declaring 8+1 distances violates Adreno 650's combined limit of eight.
The original shader build fingerprints this source and the compiled library;
regenerate the complete validated cache instead of editing deployed shader bytes.
The original Metal entry point retains its debug default; the validated Vulkan
wrapper defaults to `XLATE_DEBUG=0`, with an explicit debug reference available.

The Vulkan toolchain can also test `XLATE_DEBUG=0`. Local AST propagation now
retains calls, address-taken inputs/outputs and the evaluation point of expressions
reading mutable locals. Memory accesses, derivatives and subgroup operations are
not moved into a consumer's execution context. Only known pure operations over
stable operands are propagated. For a single consumer in the same basic block,
mutable local reads may also propagate after checking the complete interval for
writes, calls and control-flow boundaries, including nested consumer expressions.
The original debug/reference path remains
available. Regression cases under `dm/gfx/tools/tests/shader_ast` cover call
outputs changing a previously captured expression, multiply-defined locals and
derivatives consumed inside a conditional. Vulkan fingerprints both this source
and the compiled shader translator; this is GPU shader compilation only.

A single-use constant-buffer load can additionally fold into the immediately
following pure assignment in the same block. No AST statement, call, write,
barrier or control-flow edge may intervene; other memory kinds and non-pure
consumers retain the snapshot. This narrower proof replaces no existing memory
ordering. An official whole-frame replay is RGB-identical; five hot shaders have
unchanged MOC machine-code cost, so this is not a measured spill/FPS improvement.

# 本地着色器编译器兼容修正

删除 SPIR-V 逐顶点输出块中未使用的 `gl_CullDistance[1]` 占位成员。翻译器没有此项
I/O 映射或写入；Position、PointSize 及八个 ClipDistance 的成员索引和实际语义不变。
原先 8+1 的声明超过 Adreno 650 合计八个距离的限值。构建同时登记此源码及编译库指纹，
须重新生成完整并校验的着色器缓存，不能修改已部署字节。Metal 入口保留原 debug 缺省值；
已验证的 Vulkan 包装入口缺省 `XLATE_DEBUG=0`，仍可显式构建 debug 对照。

Vulkan 工具链可以显式使用 `XLATE_DEBUG=0`。本地 AST 修正保留调用与传址实参/输出变量，
并保留读取可变变量的表达式原求值位置；不将访存、导数或 subgroup 操作移动到消费者的执行上下文。
仅传播操作数稳定、已知无副作用的运算；同一基本块内的单个消费者另可通过完整区间证明：
从定义到使用点不跨改写、调用或控制流，检查包含嵌套使用表达式。
`dm/gfx/tools/tests/shader_ast` 包含调用改变已捕获值、
多次定义及分支内使用导数的回归。Vulkan 缓存指纹同时覆盖此源码和编译后的着色器翻译库；
此改动仅涉及 GPU 着色器编译，不引入任何 CPU 模拟逻辑。

另允许单次使用的常量缓冲读取并入同基本块紧随其后的纯计算赋值，不能跨越任何 AST
语句、调用、写入、屏障或控制流；其他访存与非纯消费者仍保留快照。该规则不削弱访存
顺序。官方整帧回放 RGB 相同，五个热点的 MOC 机器码成本未变，因此不宣称减少了实际
spill 或提高了 FPS。
