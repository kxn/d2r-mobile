# Driver Uniform 读取范围与分配容量

## 改动前预算

当前 Q 原游戏对照记录的 driver arena live 为 20,971,520 B。原 c0/c2 不可变常量每份只初始化原有 0xa00 B ABI 数据，却保留 65,536 B、其余为零的槽位。独立离线证明全部可执行模块的所有读取均落在 4,096 B 内时，可以同时缩小槽位与 descriptor range；不改变游戏、SPIR-V、常量内容或地址语义。

若所有实际槽位都有资格，这一类存储的理想预算为 1,310,720 B，减少 93.75%。这不是整进程 PSS、CPU、带宽或 FPS 的实测收益。分块粒度、未覆盖模块、设备对齐和驱动分配成本均影响实值；现有 profile 未证明这类常量构造是翻译线程的主要时间来源。

## 证明与运行时合同

- 可选 `.dspan` 为 96 B 小端 DSP1，独立于原 `.dkey`，不改变原 DDK1 格式。记录 stage/set、原五类 artifact 的指纹、c0/c2 binding 和最大读取结束位置，及完整性摘要。
- 原 SPIR-V 分析器使用官方 `spirv-val` / `spirv-dis`，覆盖 base、specialization、predicate、texel-storage base/spec 模块的全部函数和分支。零尾读取也计入范围。动态地址、未知指针、缺失／歧义绑定及未知模块保留 64 KiB。
- 原模块身份与证明绑定；未覆盖的 pixel-local/DPL 或其他浅复制后替换模块不能继承 4 KiB 权限。已覆盖 paired texel-storage 模块显式授权。
- shader 的真实绑定必须与证明一致，设备 Uniform offset alignment 不超过 4 KiB。旧资产无 sidecar 或文件无效时维持原范围。
- 槽位 range 纳入 last/content/hash 缓存身份，避免同 stage/slot/schema/content 的 64 KiB 读取者错误复用 4 KiB 分配。
- 两种槽位各有 active arena 链，完成后的 idle arena 共用原 32 chunk 上限。仅在原 completion/recycle 边界复用或释放，GPU 在途常量保持不可变。
- 原 support、c1、普通复制 UBO 和 reference 合同不变。4 KiB 槽保留完整原 0xa00 数据和所需零尾，不依赖越界读取或 robustness。

新增成本：每个 shader 的两个范围和模块身份；加载时可选文件读取、指纹／摘要／绑定核对；每次 driver constant 的范围选择及缓存身份比较／hash；取得 arena chunk 时在最多 32 个 idle chunk 中匹配大小。减少的是符合条件的存储容量和首次零初始化字节，不承诺削减每次原 0xa00 数据构造。

## 当前证据

原 Q baseline 中 6,931 个 SPIR-V 及其实际模块变体已检查；6,927 份原 DDK1 均重建字节相同。实际 c0 binding 239/239、c2 binding 2,784/2,784 的范围合格，最大读取结束分别 1,400/1,468 B；合计 2,926 个 shader、3,023 个槽位具备静态资格。这不是运行时分配命中权重。

离线 23 项原 DDK1 与新增 DSP／部署正负测试通过。宿主原 NVN→Vulkan 实现的普通、ASan/UBSan 构建均完成：

- 新 fixture 的 serial/async 两种执行各通过 GPU copy/readback：c0/c2 内容与全部零尾、大小缓存隔离、坏／缺／高尾证明回退、模块身份变化、混合 arena 在途保护、完成后回收与原 idle cap。
- 原 driver sparse/full GPU shader fixture 在普通及 sanitizer 构建通过，实际执行 Uniform 读取，对照 reference 内容；动态 c2 与 65,520 B 的已知零尾读取保持 64 KiB，缺失／损坏 DDK1 不影响独立 DSP 的正确性。
- 新 copy fixture 仅在测试中给 UBO 额外声明 TRANSFER_SRC；不能据此声称验证了 Android 真实内存布置。真实 Uniform 读取另由上一项原 shader fixture 验证。
- 首次新 fixture 因 Vulkan 初始化顺序错误在进入测试前失败，原 rc139 记录保留；修正测试入口初始化后再跑通过，未据此改生产逻辑或删断言。

上述均未装载游戏模块，不代替 Android 原游戏验收。还需原 Android 构建、原始 NSO 重建、合规部署新证明、原游戏对照，以及持续温度与 6GB 设备验收。

## 工具链边界

新源码编译可在原入口生成 DSP。旧 baseline 的独立静态 DSP 目录仅为证明结果；当前整库部署入口仍要求当前 compiler/source/tool chain 一致。禁止给旧缓存更换 `.chain` 冒充重建。真机使用前须完成原入口新链构建验证，或独立定义并验证保持原编译资产与来源不变的 sidecar 产物部署合同。
