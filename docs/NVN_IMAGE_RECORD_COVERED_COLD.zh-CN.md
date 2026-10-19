# 图像访问记录的 covered-read 入口与 cell 更新分离

基于 `9f930e9`，只在 `nvn_vk_image_dependencies.inc` 将原 `image_record_normalized` 的非 covered cell 遍历和原 proof 尾部移入 `noinline image_record_cells`。原前缀保持原顺序和表达式：写访问撤销 CPU 可重建证明、查找 exact-range read proof、whole-image/use clock、原 mip stamp 条件及循环、zero_mips、access generation、covered-read 返回。原非 covered 尾部也逐字保留。两个函数之间无回调、提交、等待或新共享状态；传入原局部 `read/writing/mip_stamped`，范围仍按值传递。

这不增加、预测或扩大 reader/visible 证明。未知范围、原普通 `image_record` 的归一化、所有 aspect/mip/layer cell 读写、write-clock、raster/operation writer、layout ordering、精确证明失效和替换都沿用原实现。已有 reference 路径继续使用同一原语；没有新增 runtime 开关、缓存字段、标签、锁、原子、计数器或分配。

## 依据和成本

已完成的官方 Simpleperf `translation-cpu-9f930e9-mali-on1` 使用精确 BuildID `0df07a616b5cdd803b32c0ffcf75a98861cf8d21`。580 个 NvN 用户态样本中，此函数有 14 个自身 PC 样本，分布在 proof 查找、mip stamp、cell 更新等。它们不是 14 个可消除样本，也不能从这次采样反推 covered 命中率或调用总数。报告在 `.local/logs/translation-cpu-9f930e9-mali-on1-analysis-ready.json`。

NDK **28.2.13676358 / clang19 / Android API29**，原 O3、TLSdesc、frame-pointer、strong stack protector 参数的独立 TU 编译：

| 条目 | 原始 | 候选 |
| --- | ---: | ---: |
| `image_record_normalized` frame | 160 B | 80 B |
| 热入口整数寄存器保存／恢复（各方向） | 96 B | 48 B |
| 非 covered 组合 frame（不含调用者） | 160 B | 80+96=176 B |
| 非 covered 寄存器保存／恢复（各方向） | 96 B | 48+88=136 B |
| 这两个函数静态指令总数 | 944 | 174+773=947 |
| 整个 texture TU 的 llvm-size text 列 | 72,251 B | 73,703 B |

covered 返回每次少 48 B stores 和 48 B loads，这是逻辑栈访问，不是已测 DRAM。非 covered 增加 BL/RET、20 B range 传递、16 B 组合栈和各方向 40 B 寄存器保存量。优化器也改变部分调用者：`image_request_add_normalized` frame 144→96 B，`vk_texture_shader_record` 128→144，`image_promote` 224→256，B2T 240→256，T2T 352→384，`vk_texture_native` 112→96，并单独生成 80 B `image_unknown`。不能只报告入口减少、隐去这些成本。

这次改动没有省略任何访问记录运算，仅把完整 cell 路径的寄存器需求移出已存在的 covered 返回。实际净 CPU、帧率、发热和能耗收益尚未测定；不能将不同频率、不同限帧状态的新旧样本数相减作为收益。

## 验证范围

使用独立 host build、既有 shader fixture pack，`LP_NUM_THREADS=2`、Lavapipe 和官方 Vulkan validation/sync validation；执行完整 async、serial、16 MiB window 以及 pixel-local/shared-DS 现有 GPU 字节 oracle。完整套件包含 readonly/depth/array/history、mip-stamp/write-stamp、跨提交 recipe、dirty publication、真实 nested recipe 重入等原正负用例，不改 expected，不写映像实现的测试。精确命令、检查数和哈希由对应 source-ready 报告记录。

这是原 NVN/Vulkan 后端的宿主测试；没有执行真实游戏模块、没有 Android 安装、设备访问或新采样。Android APK 和真机验收由主代理统一安排。
