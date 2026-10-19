# 缓冲区写入历史直接保存结束地址

本项以 `79d5a9c` 为基线，只改变通用 Vulkan 缓冲区写入历史的内部表示；不改游戏逻辑、命令顺序、Barrier、CPU/GPU authority 或资源生命周期。没有增加缓存或环境开关。

## 依据与范围

已有 `e341eea` Mali Simpleperf 的 NvN 线程共 618 个 `cpu-clock:u` 样本。官方 ReportLib、LLVM inline 与对应冻结源码核对后，`buffer_stage_pending` 的 14 个 self 样本中，11 个位于 `buffer_write_start` 的二分查询内联代码；其中 5 个 PC 落在原结束地址加法上。这里已有按地址排序、合并的历史区间，以及精确的二分 lower-bound 分区复用；不是线性扫描，也不需要另加预测缓存。

同一数据中的 `record_pool_range` 16 个 self 样本主要是已有 page/stage 位图和最后覆盖证明，`mtl_sub_index` 17 个样本来自反向提交分析及附件 Store 索引，不能混称为缓冲区查找开销。本项不改这两处。

这些是用户态 CPU 采样，不是调用次数或可直接移除的耗时。PC 落在 ADD 上也可能反映前一条读取的延迟，不能把 5 个样本换算为预计提速。

## 表示与不变量

私有 `vertex_buffer_writes` 表项从 `address,size` 改成 `address,exclusive_end`，区间统一表示为 `[address,exclusive_end)`。原写入入口已经检查 `size > UINT64_MAX-address`，并在合并时计算最终 `end`，现在直接保存它。

二分探测、分区上下界和合并逻辑直接读取结束地址。物理 backing 归属核验仍调用原 `pool_span_resolve`，在该路径计算 `exclusive_end-address` 还原长度，额外减法没有隐藏或省略。

所有字段读写点均在同一静态翻译单元内。以下逻辑保留原状：

- 空范围、地址溢出及 `UINT64_MAX` 尾部分区处理。
- 排序、重叠/合法相邻区间合并、256 项上限后的保守 overflow 回退。
- producer stages、当前 visibility、pool identity、backing generation、VkBuffer 归属检查。
- 所有几何变更后的分区失效，以及全局依赖完成后的历史清理顺序。
- 同地址销毁重建、不同物理池、未知旧 owner 的保守同步。

每项仍为 48 字节，256 项共 12 KiB；静态断言检查布局大小。没有增加对象分配、扫描或生命周期状态。

## 实际 Android O3 代码

用 `79d5a9c` 发布产物记录的原 Ninja clang 命令，分别编译冻结原文件及本候选文件。NDK 28.2、ARM64/API29、`-O3`、TLSDESC、frame pointer、stack protector 均保留；原生成头文件固定到同一 source-input 冻结集，并核对实际依赖哈希。

二分循环的 `LDP address,size; ADD end,address,size` 变为单条 `LDR exclusive_end`。分区上下界各减少同样的重新计算。`buffer_stage_pending` 的代码大小 424→412 字节，producer 968→960 字节。

这减少的是逻辑寄存器读取与整数加法，并缩短局部依赖链；两个字段原本相邻，**不能据此声称 DRAM cache line 流量下降**。

Clang 同时改变了已有 `buffer_read_producers` 的内联决策：独立 helper 原 368 字节，新版本内联至两个读取分支，`vk_buffer_shader_read` 724→1356 字节，其栈帧 160→144 字节。整个 `nvn_vk.c` text 83,056→83,272 字节，新增 216 字节；没有额外强制内联/禁止内联属性。物理归属检查的长度还原仍有一次减法。本项未测整帧 FPS 或真机 CPU 净收益。

## 已有 GPU 字节回归

在独立 Linux 原 NVN/Vulkan 公共夹具上运行 Lavapipe，开启官方 Vulkan validation 与 synchronization validation，限制 `LP_NUM_THREADS=2`：

- 原 `buffer_partition_test` 与 `buffer_scope_test`，异步/串行各 16,626 检查。覆盖大小变化跨区间、原分区变更、visibility 变化、历史合并与溢出、GPU 实际复制读回，以及旧池销毁/同地址新物理池等。
- 完整异步套件：22,031,792 检查；完整串行套件：21,929,813 检查，包含 Pixel Local 与 shared depth/stencil 路径。
- 均无 `VUID-` 或 `SYNC-HAZARD`。这些结果是公共夹具的真实 GPU 字节与元数据回归，不代替 Android 原生游戏测试；Lavapipe 的原生 ASTC decode8 路径不受支持，未虚报覆盖。

新增 `DM_BUFFER_HISTORY_FIXTURE=1` 仅复用原两个测试，先完成一份空提交来初始化实际同步模式，不改变原测试断言。默认完整套件仍运行同样测试。

```sh
cmake -S dm/gfx/vulkan -B /tmp/dm-buffer-history -G Ninja
cmake --build /tmp/dm-buffer-history -j2
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json LP_NUM_THREADS=2 \
  DM_VULKAN_VALIDATE=1 DM_VULKAN_SYNC_VALIDATE=1 \
  DM_BUFFER_HISTORY_FIXTURE=1 DM_VULKAN_ASYNC=1 \
  /tmp/dm-buffer-history/vulkan_host_test
```

本候选未操作设备、未执行游戏模块、未改写或部署游戏 binary。下一步由主线安排原 Android 游戏与已有官方 CPU profile 对照。
