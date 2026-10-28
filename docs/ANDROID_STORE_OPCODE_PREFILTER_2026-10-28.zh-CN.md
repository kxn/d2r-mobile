# Store 前向解释器：跳过无效 opcode dispatch

`walk()` 已为每条命令加载 `plan_properties`。现在 dense 和 sparse 两种
计划表示都用其中的 `PP_STORE` 位决定是否进入 `store_index_op()`；此前只有
sparse 使用这层过滤，dense 的 UBO、顶点状态、Sampler、Stencil 等命令仍进入
该解释器的空 `default`。

这只减少已有 Store 前向解释器的空 dispatch。没有新增运行时开关、环境读取、
预测、key 或查找表；不改变原命令序号、节点、cursor、逆扫、CALL/JUMP、Store
事件或消费者。没有关闭 planning，也不删除原 NVN 命令、GPU 同步或绘制。

## 完整性合同

生产解释器的16个有效 opcode 全部已经带 `PP_STORE`：

- `SET_RT`、`SCISSOR`、`BIND_PROGRAM`、`BIND_TEX`、`BIND_IMG`。
- `DRAW`、`DRAW_IDX`、`DISPATCH`、`CLEAR_COLOR`、`CLEAR_DS`。
- `COPY_T2T`、`COPY_T2B`、`COPY_B2T`、`CLEAR_TEX`。
- `SET_TEX_POOL`、`SET_SAMP_POOL`。

`tests/test_store_opcode_properties.py` 从实际生产解释器提取有效 case，从实际
`plan_properties` 提取 `PP_STORE` 集合，要求集合相等且 `default` 无副作用。
新增有效 case 却漏标、移除已有标记、让 `default` 产生副作用，都会被测试拒绝；
不靠一份与实现一起改的 opcode 白名单维持“通过”。

```sh
python3 dm/gfx/vulkan/tests/test_store_opcode_properties.py -v
```

## 验证

4项静态合同测试通过。另从冻结的真实生产源构建原
`DM_STORE_WALK_FUSION_FIXTURE`，使用 Lavapipe、Vulkan 同步验证、GCC
ASan/UBSan，在 dense/sparse × serial/async 四模式运行：serial 每模式
136,398 checks，async 每模式137,003 checks。原独立前向解释器逐项核对
bucket、事件、alias、全局观察序号及 GPU Store 覆盖内容；没有降低原断言。

在独立源副本去掉 `OP_SCISSOR` 的 `PP_STORE` 后，原事件差分 oracle 按预期
拒绝，明确在事件字段比较处失败；不是 shader 或驱动失败。生产源未接受该负控。

四个正模式均有原夹具已存在的精确“第49次提交／53号 Metal 栅栏”诊断。
初始过宽的日志汇总门将其拒绝，原失败汇总保留；后续只对这一条与既有基线日志
逐字相同的夹具编号诊断作范围审查。GPU 内容／索引断言通过，且无新的 Vulkan
validation、ASan 或 UBSan 错误。没有豁免其他错误，也不把夹具通过当成游戏
同步验收。LeakSanitizer 按原 Vulkan 夹具设置关闭，不声称完成泄漏验证。

全部构建、正负例、原 stdout/stderr、源指纹及范围审查存于
`.local/logs/store-op-prefilter-v1/`；独立构建位于
`.local/build/store-op-prefilter-v1/`。没有执行游戏模块、ADB 或安装 APK。

## 收益边界

c09 的真实 ARM64 ELF 中 `store_index_op()` 已内联到 `walk()`，所以不能把这项
改动说成消除了函数调用。成立条件是 Store 索引启用、索引仍有效、当前 opcode
没有 Store 语义；这种命令可以直接跳过原内联 switch 的空分支。已有 properties
仍承担其他 planning 用途，未增加额外表读取。

尚未测得这项改动的 Android 净指令、时间或 FPS 收益。它是较小的分支／dispatch
省工，不能把正式 profile 中整段 planning 的约6.57M指令预算算给它。更高层
Store 消费者与整段预扫描的取舍是另外的研究。
