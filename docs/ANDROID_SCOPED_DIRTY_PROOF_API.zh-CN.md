# 自有页排除的固定范围脏页凭证（独立实验）

基于 `9f930e94c46b3783378e940fee1c764794371f52`，仅移入第一层 owned dirty API 并补齐本层。不包含旧 NVN import/JNI/driver 原型；无设备操作、无 guest 地址或分配器 hook、未修改 Metal。默认游戏没有调用 owned exclude 或新 scoped API，主线未合并。

## API 与使用契约

```
nvn_dirty_scope_observe(addr,len,&proof,&epoch)
nvn_dirty_scope_refresh(addr,len,&old,&new,&epoch)
```

`proof` 为 48B 的不透明凭证：slot、lifetime、range changes、exclusion version、固定 begin/end。必须由 observe 产生、保持原样，以同一真实查询 addr/len 传入 refresh；它不是接受任意伪造字段的安全认证。refresh 校验 envelope，不能把已证明的页面挪用到其他地址或长度。

成功只表示该查询目前不与 owned 整页区间重叠、其跟踪身份仍有效，提供负 dirty 测试用的变化序列。它不表示字节已上传、内容本身已被检查、GPU fence 完成或 CPU/GPU 权威。消费者仍先取凭证，再执行原 `dirty_since`；缓存命中比较原上传 stamp、变化序列/版本，不能把 epoch 当 upload stamp。失败时 epoch 清为 0，必须按 unknown/live 处理。新消费者不能在验证 scoped 凭证之前先按旧全局 epoch 返回。

- `observe` 是冷 trylock/find；跨 tracked range、未跟踪、空/溢出、slot/代数/版本饱和等返回 unknown。
- `refresh` 直接索引已知 slot，只读原子 metadata 和一个固定窗口，不拿 slot 锁、不 find、不扫 registry/页/内容、不等待。真实线程持有 slot mutex 时仍可刷新其他稳定非重叠查询。
- 新版本为不回绕的发布序列；begin/release/失败 rollback 转换中为 odd。原子边界和 identity/version 的 SC 读写避免 C 数据竞争与“无排除→有排除→无排除”的 ABA。odd、变化中、耗尽、旧 lifetime、无效字段均拒绝，不自旋。claim 提前保留 begin 和 release 的版本空间，临界上限仍能正常释放。
- 旧无地址 global epoch 只要有 owned token 仍为 0；旧无 envelope range proof 在 containing range 仍不可用，保持已有调用者的保守契约。
- 稳定情况下，`dirty_since` 仅重叠 owned 页时强制 dirty；非重叠仍使用原 latest/gen。`dirty_visit` 仅对重叠页无条件访问，其他页保留原代数判断。转换中的不稳定 snapshot 保守处理整次查询。
- 不受保护的 owned CPU 写不会虚构 dirty generation；非重叠查询可持续刷新同一个 seq。普通保护页的实际 fault/prepare 仍更新原 gen/range/global序列，不能漏掉。
- release 仍要求调用者先证明 quiescent 并退掉 import；本 API 不做 GPU 操作。开始/失败回滚/释放发布原变化序列，释放恢复原 page generation/protect 流程。

## 开销与实现边界

实测 `sizeof(scope_proof)=48`，`sizeof(range_t)=104`。基线 range 为 72B，第一层为 96B；本层增加 8B version，4096 槽再增加 32KiB；相对基线总静态增量约 128KiB。owned 边界改为原子不改变其大小。

scope refresh 稳定成功路径：无 active window 时约 9 次原子读，有 active 且非重叠时约 11 次；失败可早退。无内容读、分配或循环，但不是免费。现有 `dirty_since/visit` 也新增固定 owned metadata 读取，即使没有 token 也有开销。这是保持实验独立、未合 main 的原因之一，尚无当前 Android Profile 收益验收。

当前没有给 image 增加持久字段，也没有接入 renderer 的 global/range cache。后续若用 scope48B 替换 image 的旧 range-proof24B，理论每 image 再增加约24B；必须另行独审/实测。不能把本层 scope 通过说成已有 image fast path 已提速。

## 验证

- owned/API 单元最终 164 断言，包括 begin/release 真线程暂停于 odd version、同池非重叠与同页重叠、实际写故障、PG_BUSY 第二页拒绝和成功前缀 rollback、原 N/N+1 乱序 publication、slot/lifetime 重建、跨 range/unknown/overflow、版本临界/饱和与全局/范围饱和。另有 cached reader 10,000 次与同址 slot 100 次 remove/add 的真实并发，旧 lifetime 无一次成功刷新。
- 两个预期 SIGABRT 子进程验证 held-token overlapping registration/remove 拒绝，不是宿主测试异常退出。
- 原 owned-image fixture 扩展：同一大 pool 不重叠普通图像与 owned 页、直接 scoped API、实际 CPU fault、新 GPU 图像字节；释放后才首次把原 owned 页作为图像源。Lavapipe + 官方 Khronos validation/sync、async 下 7,248 检查通过。**无 import、无 renderer scoped-cache 接入，不代替 Android 实测。**
- 原 memory-pool visibility 在同一官方 VVL/sync/async 下 447,763 检查通过。首次调用漏传既有 shader-cache 路径，在入口断言退出；原失败日志保留，补齐原 fixture 路径后通过，没有改断言或数据。
- NDK 28.2.13676358、API29 的 AArch64 dirty module 对象编译通过；不是完整 APK 或 Android 验收。官方 llvm-objdump 留存 refresh 反汇编，确认生成代码来源；不是自定义计时/采样。

前一层 PanVK 非 coherent 自有页真实验收与 31cab9 报告保持原样。本层不更新该真机结论，也不接真实 guest/NVN import。下一步须先独审 API/publication 契约，然后才按已冻结闭包处理 image/writer/CPUeffect/alias 的 preborrow demotion。
