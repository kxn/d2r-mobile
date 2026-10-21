# Operation-owned Buffer geometry packet（宿主候选）

本候选基于 `6b12d7f167561104999248eff2bb31c191bc03db`，另独立 cherry-pick 已审 `fa18943994d9bcf3831fb47f5cab6434e66bc89a` 的四处零长度 memcpy 保护（本分支 `71e1cfe`），它不是 packet 机制。只针对已有 window operation 准入和随后的 Buffer 消费重复解析，不改变 shader ABI、游戏模块或资源格式。未构建 Android canonical/NSO，未部署或测得 Android CPU/FPS 收益。

## 保留的合同

原 `pool_operation_pin()` 在 `V.resources` 锁内为每个精确需求解析 Pool/window，并独立增加 Pool 引用。本候选把该次成功解析的 owner 索引、backing、物理 offset 和 generation 保存在本 operation；只给普通 Buffer 需求保存几何，native-only image 需求仍不隐式创建 Buffer。成功准入后保存 registry epoch。

`pool_address()` 仍先执行 mip clear/shared linear alias 检查，再以当前 recording 的 registry epoch 查最内层 operation 的精确 `(address,size)`。只有完全相同范围可命中，不沿父 operation 搜索。registry 变化、哈希碰撞、未列举范围或子范围全部返回原路径。Pool 独立 pin 保证内部 commit/completion/reopen 后 pointer 存活；它不等于旧 recording 的访问 ledger。新 recording 首次访问仍在资源锁内校验 epoch、初始化原 CPU 元数据并调用原 `record_pool_use()`。

原 CPU dirty/upload、range/stage/initial-contents、native alias、hazard、writeback 和每 submission 页集合继续执行。没有缓存 writer epoch、ready/visibility 或 GPU 查询结果。`buffer_recording_owner()` 的地址复用歧义检查与 named dependency 保持原样，不能直接用当前 owner 替代。原 operation pin/unpin 与 `pool_cv` 广播未改。

## 成本和适用边界

每 range 增加 32 字节几何；每个曾使用的 depth 增加固定 256 槽、2 KiB direct-map。scratch 按高水位惰性增长，最多 4096 range × 8 depth，几何上限 1 MiB、槽表 16 KiB，另少量 plan 字段和分配器元数据；原 ranges/pins 不重复复制。每次准入仍保存每个普通 Buffer 的几何并写一个哈希槽。32 位 stamp 避免每 draw 清 2 KiB，只有回绕时清表。

后继消费增加一次哈希及精确/epoch/generation 校验，完整 Pool/physical extent/offset 一致性在每次准入保存几何时验证一次，减少再次判断多个 Pool 区间及解析同一 window。当前 recording 已有 owner lookup 时仍最多遍历原 8 个 owner 槽以取得该 recording 自己的 use。未命中多付一次有限探测，然后原路径照常执行。它不消除 CPU 脏页工作、GPU barrier 或整体 operation 收集。当前 profile 的 inclusive 父函数百分比不能当作可回收时间；真实净 CPU 成本需后续现成 Simpleperf/Perfetto 验证。

## 验证范围

新 HOST-only 用例使用原 NVN Pool/operation/实际 VkBuffer copy/fill/完成机制，覆盖两个窗口 offset、精确/零/子范围、CPU 改写后重新提交、GPU-only 内容、独立 pin 跨完成和 reopen、8 层嵌套、direct-map 碰撞、registry 变化及同地址删除/重新注册。原 production policy 严格 oracle 覆盖 graphics/compute/UBO/SSBO/typed-texel/VBO/index/ghost 与显式 promotion。软件 Lavapipe Core/SYNC 与 sanitizer 结果、原始失败和负控见独立 source-ready 报告。

测试 getter/实际消费计数只在 `DM_VULKAN_TEST_EMBED && !DM_ANDROID_APP` 编译；APP 预处理核查排除了两者。计数只验证功能路径使用，不作性能测量。

组合 UBSan policy 在原 HLE `nvnCommandBufferBindVertexAttribState(0,NULL)` 的零长度 memcpy nonnull 合同处中止（`hle_nvn.c:842`），初次失败原样保留。随后纳入上面的独立已审 HLE 保护，以完整 ASan+UBSan 重建并复测原 oracle；最终 source-ready 仅据真实新结果记录资格，不把原失败改为成功。

以上记录源码阶段资格；当时尚未构建 Android 或执行真实游戏。后续主线组合版本的实际验证见文末，软件 GPU oracle 仍不能替代真机结果。


## 后继：把不可变范围证明留在准入（e0 保持冻结）

初版 e0 在 exact-key 命中后又重做 Pool 和 physical extent 的约十余项检查，再查原 8 owner 槽；在旧 retained-span hint 正常命中时，这可能比旧路径更贵，路径覆盖计数不能证明节省 CPU。后继只把这些完整范围一致性校验移至准入记表，不删除 pins、epoch 或 backing generation。

旧 `pool_address` 常见命中先逐 owner 检查三项 Pool coverage，再由 `pool_span_retained_hint` 检查 span 是否部分窗口、四周范围、generation/handle、physical extent，计算 offset。后继新增 direct-map 哈希、stamp/row、exact address/size、plan epoch、owner index 和 generation 检查；然后只按 owner 指针相等取得当前 recording 的 use，复用已保存 offset。它减少重复区间计算，但仍有表维护和探测开销、首次 recording use 仍需锁，未命中时还会回退原路径。它不是必然更快的证明，需同条件官方 CPU 结果判断净成本。

可转移的证明：成功准入已精确解析此 `(address,size)`；row/key 与独立 pin 绑定同一 Pool/backing。Pool 删除先移出 registry/推进 epoch，然后等待全部 references 归零；ordinary migration 发布时只允许自己的唯一 reference；shared union 自持一个 reference 并等其他 references 释放。operation 的独立 pin 因而同时阻止对象释放和物理替换。单纯补充不相交窗口可推进 epoch，让旧表保守失效。`p->guest/size` 及已存在 backing 的 `begin/end/buffer` 不在有效 pin 期间原地修改；generation 仍逐次核对。故完整 key 的原范围与 offset 无须每次重算，不能类推为 ready、writer 或 stage 的稳定性。

仍保留表行/owner 索引边界与 generation 检查；子范围、碰撞、registry 变化、operation 结束保持原回退。生命周期与 hazard 不改。新增构建校验每 operation 每普通 Buffer 一次，不称作罕见/零成本。单个 range 只消费一次时可能没有收益；没有实际使用到本表的 range 还要承担保存成本。独立后继源码资格与下面的主线组合真机资格分开记录。

## 主线组合 848f47c：Android 实际执行与 CPU 边界

`848f47c` 合并了此后继、纯图像别名空洞证明和零数量绑定的 HLE 保护。完整原构建入口生成 Java/APK 与六个 SO，O3、原编译选项、16KiB 对齐不变；最终 APP 的热绘制栈仍为 6496B，测试 getter/消费计数未进入 APP。三个未加工 NSO 重新生成后，独立 verify-only 核验 28 项部署产物。

Q735 使用 f830、native6217、1MiB 窗口、2750×1260 输出及固定 0.60 的 3D 比例。真实原游戏完成三段营地移动；两张截图未见明显黑块或几何错误，原生命周期确认两次存档提交，随后恢复原 D7 包与驱动并停机。这不是全部场景或长时间游玩的正确性证明。

现有官方 Perfetto 的同一 Surface 窗口包含 232 个完整显示间隔：整个 NvN 线程实际 Running 为 26.079ms／间隔，replay 为 25.010ms／间隔；平均显示间隔 33.424ms。频率跟踪覆盖完整窗口，按 Running 加权约 1611MHz。CPU 预算仍明显超过 16.7ms，不能据此宣布解决帧率或发热。

Simpleperf 的 stat 窗口包含 PSO 174→181、变体 31→33 的变化，之后 record／Perfetto 窗口稳定在 181／33。历史 98a 的 26.875ms 来自不同窗口，不能把差值当作此优化的净收益。新增的官方 `instructions:u` 事件实际有非零结果，但输出没有完整的 PMU 缩放覆盖率，不能保证所有计数器都在整个窗口持续计数。

本机证据入口：`.local/logs/native-candidate-geometry-848-Q735-v1-root-functional-review.json`、`.local/logs/operation-buffer-main-848f47c-root-canonical-raw-review.json`、`.local/logs/native-candidate-geometry-848-Q735-v1-analysis/actual848-vs98a-frozen-ready-agent.json`。这些记录分别对应实际执行、构建来源和官方工具分析，均不把局部少算等同于已验证整帧提速。
