# 当前高通 GPU 主批次与着色器优化预算

本记录继续相同实际分辨率／比例下的性能根因任务。原 BC3 对照已经基本
持平上游，不作为任务完成依据。当前关键是削减实际主批次，而不是用更小
SPIR-V 文件或 CPU 等待迁移解释涨帧。

## 计时与身份来源

原厂 Perfetto/KGSL SOP/EOP 校准后，主 GPU 批次约 22.85ms，观测内区间
批次执行覆盖约 99.65%；包含 GPU 内部等待，不等于 ALU 利用率或 DRAM
饱和。内部同步队列实验确实允许下一次提交提前，但主要将背压迁移到
Acquire，增加排队深度，FPS 没有显著改善。详见同日
`ANDROID_OWNER_RUNNING_BOUNDARIES` 和 `ANDROID_INTERNAL_QUEUE_PRESENT`。

同 9ea4f9e7 宿主／12b8 driver 的独立 Mesa pass 诊断：653 个完整主 CB，
每个实际 49 pass、15 dispatch。工具开启时主 CB 均值约 25.44ms，不能
与普通运行 22.85ms 直接相减；Mesa 整数 ticks×52ns 也不是 BOOTTIME。

| 实际组 | 诊断区间均值 | Draw / CB |
|---|---:|---:|
| 1673×765 HDR，两附件，ordinal 10 | 7.912ms | 642 |
| 1024² Z16 深度，ordinal 4 | 2.872ms | 107 |
| 1673×765 深度，ordinal 7 | 2.387ms | 642 |
| 1673×765 三附件，ordinal 22 | 1.101ms | 29 |
| 1968×900 UI，ordinal 1 | 0.991ms | 约 811～812 |

这是组区间，包含边界／同步开销，不能叫纯 Shader 时间。分组及子阶段
嵌套，不能相加；各组 draw 数不能当作片元调用数。当前实际 pass 都为
SYSMEM；不能继续假设每个 Vulkan pass 边界都是一次 GMEM 整图往返。

另外一次 draw 身份诊断只用于匹配 stage/native hash，再用原厂 IR3
`source_blake3` 关联到完整原 SPIR-V。所选三完整 CB 的 147 个 pass
中 draw 配对数量均符合 pass 元数据，没有未归属 draw。逐 draw 时间
不参与热点排序。

## 优先候选与明确缺口

1. **顶点常量地址归一化。** ordinal 4 两个实际 VS 都有约 349 条 native
   指令，动态常量读取为十二组四条标量 LDC。原 SPIR-V 的移位／分量加法
   掩盖了相同向量下标；普通 `spirv-opt -O` 仍有 72 次右移／44 次整数加。
   通用已知低位归零证明工具将两模块各改为 12／8，保留原 float、范围、
   robust 和 32 位回绕语义。官方 validator 通过，标准 Z3 对各 48 个
   重写结果证明全 32 位等价。随后普通 UID 独立四 PSO 编译已验证真实
   12b8：第一原 VS 精确复现阴影组 349 条指令／48 cat6，候选为 **142 条
   指令／12 cat6**，12 full 寄存器／16 waves 保持；robust 仍开启。第二
   VS 配同阴影 FS 为 413→195 条、48→12 cat6，16 full／12 waves 保持。
   第二份不能冒充完整 HDR PSO 的原 485 条，因为 fragment link 条件不同。
   夹具仅调用标准 pipeline 创建、原厂 IR3 输出，不 draw／submit／计时；
   证明机器代码削减，尚未证明游戏画面或整帧收益。真实游戏链 A/B 待完成。
2. **HDR 已有谓词特化的实际输入分布。** 两个 canonical fragment key
   `5c1ca4e7c3e4657c`／`38f344adf801b153`，含各自多个 native PSO，合计
   496/642，即约 77.26% HDR draw。不是 77% 片元或时间。当前原 Spec0
   已选 1；原工具可证明另一个 Uniform 的两个谓词。静态冻结 mask0 可
   将约 3255／3205 个规范化 SPIR-V op 降到 2148／2073，并由四个 loop
   降到两个；mask3 则只少约 1%。原 shader 本来会动态跳过未走分支，
   不能当作已消除三分之一动态运算。现成工具没有当前 Uniform payload，
   已补默认 OFF 的只读计数诊断：保留原 module/spec/PSO，不为数据未就绪
   flush、提交或等待。还须派生经过完整来源验证的新 recipe 链并采集分布。
3. **HDR typed Buffer fetch 的替代表示。** 两实际主 FS 各十六个
   ImageFetch 能逐条对应原 SPIR-V；并非后端额外增加。各六个结果直接
   进入整数位操作，不能随意 quiet NaN 或降精度。12b8 新独立普通 UID
   探针已逐 scalar 位域验证 typed/raw：2^32 texel、171.8 亿分量全零错，
   注错负控检出；quiet 模型失败。这个结果限 probe 的 uint4，不能替代
   生产 float4 SSBO／真实 alias／范围／base-spec 配对资格。readonly SSBO
   在本编译器也可能使用 isam.v；换 API 表示不等于少十六次访存。

第三项完整证据 `.local/logs/texel-storage-current-turnip-v1/RESULT.zh-CN.txt`。
前两项及实际源匹配位于
`.local/logs/native-bc3-three-arm-game-v1/current-draw-identities-v1/selected-three-v1/`，
地址证明位于 `.local/logs/robustness-buffer-access-audit-20261028/`。

## 本轮排除与保留

上下游都实际启用 core robustBufferAccess，没有单边多开这个特性的差异。
当前 VS 没有符合现有单 Uniform 分支选择机制的大段可裁骨骼分支：七个
源 VS 没有条件分支，另两份不能用该资格扫描器证明。不能据扫描失败断言
绝无其它优化，但也不能给它估一块没有依据的时间预算。

把不可变 C1 常量交编译器，在两侧同等 `spirv-opt -O` 后，主 FS 各只少
32 个静态 op，循环不减，不作为大收益主线。混合 ASTC/BC 的数组展开在
当前六份 BC3 日志中没有事件，不能继续拿旧初始化搬运解释稳态帧耗时。

首 draw 清屏省略已实现并保留：正式 20 秒约 43.18→43.25 FPS，没有
显著涨帧；原厂写入 44.25→44.28MB/Surface，未见下降。成本／工作量与
整帧分别记录，见 `ANDROID_COVERED_CLEAR_EXPERIMENT_2026-10-28.zh-CN.md`。
后续优先验证以上两项较大候选的实际机器代码和适用分布，不重复该小预算。
