# 共享图像 CPU 观察：实际命中的整页假依赖

## 真机证据

使用原游戏 runtime `4422f768`、f830 Turnip、共享 Pool、full2838 ASTC6 资源、0.60 渲染比例，在高通 Q735 执行原三段移动及稳定营地准入。官方 LLDB19 只设置一次实际等待分支断点，读取元数据后立即 detach。没有同时使用 GPU layer、Simpleperf 或 SDP；调试期间的时间不作为性能数据。

停点是 `vk_texture_cpu_preflight → vk_pool_shared_cpu_observe_prepare → vk_internal_wait`，实际 PID 7684、NvN TID 7752，ELF BuildID `b68b362a5bd53a568911497e73106741e86841d9`。请求 queue serial 18681，当时已完成 18673。观察结果：

| 对象 | 地址／大小 |
|---|---|
| CPU 将读取的纹理 | 起点 514692498944，13,312 字节 |
| 纹理信息 | logical serial 118，128×128，8 mip，NVN format 73／VkFormat 139（BC4 UNORM） |
| 等待的 GPU publication | 起点 514692433344，65,536 字节，结束于 514692498880 |
| 实际距离 | GPU 写入范围结束后还有 64 字节才到纹理起点；没有字节重叠 |
| 触发等待的范围 | CPU 源向外取整后的 4KiB 页，与 GPU publication 重叠 |
| CPU effect | 9 个 pending submission 的 CPU-effect 前缀计数均为 0 |
| Pool | identity 5，大小 744,488,960 字节，实际 properties 15，包含 HOST_COHERENT；完整页包络位于 Pool 内 |

因此，本次等待来自整页保守判断，不是 CPU 真正需要读取 GPU 正在修改的纹理字节。这个具体案例不需要新增 GPU deswizzle 或暂存副本才能解除假依赖。

## 最小修正及边界

只在已保留的 live shared Pool、实际 coherent backing、完整宿主页包络都在同一 Pool 内的条件下，区分两类范围：

- GPU publication 与 CPU 实际读取字节比较。
- CPU writeback/effect、写保护和配对文件写 lease 继续使用整页包络。

未知属性、非一致缓存、跨 Pool 页包络保持原行为。测试强制 prewrite gate／cache 的路径同样保持原行为。prepare 和 begin 必须使用同一判定；begin 仍只验证，不能在已经借用物理资源时补做提交或等待。

原 pending negative cache 的 key 只有一个地址／长度，不能表达这两种范围。新查询不得读写该缓存，否则可能把“实际字节无 GPU 写入”错误复用成“整页没有 CPU effect”。保留原调用者的缓存语义，不新增缓存或扫描框架。`shared_access_host_phase`、dirty gate、mprotect、FS lease 和 GPU 内容依赖保持原机制。

## 改前预算

可以预期取消符合条件的整次假等待以及由当前 recording 假冲突造成的提前提交；不增加图像副本、compute dispatch 或常驻分配。冷观察路径仍需现有的 pending 检查，且双范围查询不能借用旧 negative cache，这项 CPU 成本必须保留计入。

本次停点只证明一个真实案例，不证明发生频率、此前约9ms/出图等待的覆盖率或 FPS 收益。等待停点持有 completion mutex，completion worker 当时也在获取该锁；不能据此把 worker 栈解释成 GPU fence 时间。没有持续游玩、散热或 6GB 启动验收结论。

## 复现与清理

工具分支 `b07ef0f0` 沿用 `native-bulk-game-v1` 的原构建记录、原始 NSO 验证、路线、存档两次、停止和六目录／APK 恢复。实际事务终态 0，独立确认游戏 PID 消失、该次调试转发移除、cleanup 为空。临时关闭的遗留 EGL profiler 属性已恢复原值。

前两次失败保留：第一次在启动前正确拒绝遗留 profiler；第二次读取应用私有驱动哈希时使用了 shell UID，已用原 `run-as` 权限读取修正，SHA 校验未放宽。第二次亦完成原路线、存档和恢复，没有启动调试器。

本机证据：`.local/logs/shared-cpu-observe-page-only-4422-root-actual-review.json`（SHA256 `926c45a32c8f39d8d4ce67ed8ea48c299eb2002e678ecce3e42abd282b67f40c`），引用实际 LLDB 输出和完整事务记录。

## 实现与当前验证

核心 `d5f9b9b2` 已实现，Android 启动传输及 APP 测试代码排除后的候选为独立分支 `codex/shared-image-observe-android-transport-v1` 的 `09a9ad03558b2f97f39cac7dde558e19bbeff570`，基于原 `4422f768`。尚未合入或默认启用。启动开关缓存一次；首状态只报告政策是否启用，不把它当成每个范围均已合格。

有一项实际测试发现并修正了分类遗漏：当前 recording 的共享 GPU 写入，flush 前也暂存在名为 `writebacks` 的链表；flush 对这些节点只做 GPU 依赖及转入 `shared_publishes`，不执行 CPU 回拷。因此，这类共享节点也使用 GPU 字节范围；非共享的真实 CPU writeback 仍使用整页。

22 项宿主 GPU 内容／异步依赖测试通过，包含同二进制 ON/OFF、相邻／真实重叠、同页 CPU effect、负缓存隔离、强制 cache／prewrite gate、CPU-only 图像源、原别名及 BC 拷贝用例；其中相应用例也经过 ASan/UBSan。另有两个合法编译配置对象通过。这些是后端语义验证，不是 Android 原游戏或实际非一致缓存硬件验收。

原正式 APK／原始 NSO 构建入口已经完成该候选，严格先 canonical、再 fresh RAW／独立核验。六库、实际 APP 预处理排除、签名及 16KiB 对齐通过；28 项部署文件核验通过，23 项模块／规则产物与原 `4422f768` 字节相同，来源记录为本次新构建。根代理重新核对 5,694 项文件哈希；只有一个新增 JNI 导出，旧导出没有删除。实际游戏尚未证明 CPU、FPS、PSS 或散热净收益。

前一版 `4eb7c1b` 的 Android 链接确实失败：APP 包含的旧测试入口仍调用新宿主测试 helper。`09a9ad0` 只为该调用补齐 APP 排除，不改变生产依赖逻辑；失败源码与日志保留，没有用失败产物部署。

源审：`.local/logs/shared-image-observe-09a9ad0-root-source-review.json`。实际产物根审：`.local/logs/shared-image-observe-09a9ad0-root-canonical-raw-review.json`（SHA256 `048904cd572229059b6b54df464a5418f5f4f5b7d905eaab0fd986a0867ab56a`）。后续在同一 APK 内固定 f830、完整 2,838 项 ASTC6 资源、0.60 比例、1MiB 窗口与 lazy ON，只切换本项冷启动政策；沿用原三段移动、官方 Simpleperf／Perfetto、存档两次、退出及原包恢复。所有独立窗口须保留，不将一次调试命中外推为每帧等待。
