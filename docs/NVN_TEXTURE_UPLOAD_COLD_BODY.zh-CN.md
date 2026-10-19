# 纹理上传检查与实际上传主体分离

此变更只把 `image_upload` 中真正执行 deswizzle/staging/copy/protect/stamp 的原有尾段，抽成私有 noinline `image_upload_contents`。高频 `image_upload` 仍原样执行 source-authority 验证、has_layout 判断、pending mip clear、global/range dirty negative proof 和原 dirty-since 扫描；只有原本确实要上传时才进入 contents。

没有增加预测状态、cache/tag、锁、原子、计数器或计时器。原 guard/check 前缀与 contents 主体有逐字相同的源码核对记录；新冷函数仅重新声明同一个 `&i->tex` 指针。原回调次序、对象借用规则、上传前 proof 清除、全部 mip/layer/slice copy、protect、stamp 和 CPU rebuildable/external_access 规则不变。

## 实测依据与实际代码生成

沿用 add4bb1 官方 Simpleperf：676 个 NvN 用户态样本中 image_upload self 为 8 个。精确 frozen ELF 下，6 个在入口/initialized/negative-proof 检查，另 2 个在公共 epilogue；不能把这些样本全部算作本次可省成本。原函数源码在后续 recipe 字段删减版本仍相同。既有更宽 public snapshot bracket 的 texture-dirty negative hits 为 771695（232 game ticks），证明快返回确实频繁；不是准确工具区间的调用率、显示帧率或物理 DRAM 量。

NDK 28.2.13676358/clang19、Android API29、O3/TLSdesc：

- 原 image_upload frame 336B，保存 x19..x28、FP/LR、d8/d9，早返也承担全部保存/恢复。
- 新检查 wrapper frame 80B，只保存 x19..x21 和 FP/LR；stack canary 仍在。快返回少保存七个 GPR 和两个 FP 寄存器，即少 72B stores + 72B loads 的逻辑栈传输；不能将此称为少 144B 外存流量。
- contents 保留 336B frame。实际编译是 `BL` 而非尾调用，真实上传多一次 call/ret，并有两个活跃 frame 共 416B（相对原函数多 80B，不含后继 callees；重入时每层同样计入）。保留此冷成本，不强加 musttail 或额外 helper。
- 静态指令总量 466→116+371=487（+21）；没有新持久分配或结构字段。hot frame 变小不等于整帧 CPU 已改善。

## 原 GPU 验证与边界

独立 Lavapipe、LP_NUM_THREADS=2、编译-j2、官方 validation + synchronization validation：full async 22031790 / serial 21929811 checks；PL subset async 398402 / shared-DS async 398486；延期 pool publication async 4249，均零 VUID/SYNC。

复用现有真实 GPU 字节 oracle，不增加实现镜像测试。覆盖重复无脏读取、跨池无关写、同池 invalidation、真实保护页 fault、host CPU edit、延迟/out-of-order publication、range 生命周期/overflow/untracked；同 descriptor CPU/GPU 变化、mip/storage/format promotion、nested ghost 导致碰撞和回调提交重入；PL ordinary fallback、history 和 shared DS；实验延期 CPU authority 仍必须在任何 fast return 之前检查。

候选没有新 APK/游戏/设备采集。原游戏当前使用 add4/c578 之后构建的包，源 profile 窗口早于本候选；这里仅证明减少高命中检查的栈保存工作和正确性，不宣称 FPS、温度、功耗或净 CPU 时间收益。原 dirty-proof reference 与不支持/untracked 的实际上传回退均保留。
