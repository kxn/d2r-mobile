# 可逆压缩 page-array 实验（989320e 后继）

独立冷环境选项 `DM_VULKAN_COMPRESSED_PAGE_ARRAYS=1`，默认 OFF。原 legacy ASTC
开关与实现不变。尚未进行 Android canonical、真机或真实游戏验收，不能默认启用。
设计复核 v2 与首版报告保留；本文件说明实际源码与验证边界。

仅实际无 BC3 sampled 支持、baseline 已解压 RGBA8 的 fresh 私有 2D array 可选采用
ASTC4/6 canonical；shared、native/external、已初始化/有 writer/已定义 alias、非 BC3、
未知 CPU 版本、不同色域、HDR/无源证书均走原路径。首次分配失败保留原图。正式代码
不伪造 ASTC/BC 能力；host fixture 的 BC3 恢复 stand-in 明确不经过压缩 admission。

源 LDR 分类只读现有上传 staging 一遍，包含 sRGB，沿既有解析器拒绝 HDR/坏块。
源任何实际写入仍清除证书。目标初始 defined 集合为空；以后仅同实际块格式、同色域、
块对齐/合法 mip 边缘、非过滤/非缩放 copy 保持压缩。并未假造 84/252 层全覆盖证明。
compressed 目标不复用全图 `astc_ldr_proven`（未定义槽不构成 LDR 字节证书）。

其它 copy、CPU dirty、B2T、clear、Storage、attachment、不同格式 view、原始/呈现/trace
出口在借句柄前全 mip/layer nearest blit 回原 RGBA，之后此 image identity 不再压缩。
保留原 access history、旧 view/descriptor/request 指向及真实 completion 退休；不新增
内部 submit/wait，不复制 guest bytes，不建立永久 shadow，不移除现有同步。
上传后的缓存句柄若变化会丢弃旧 binding recipe；旧句柄只供其先前命令存活。

采样色域保持，LDR 避免 HDR 限幅语义变化；压缩直接采样少了 baseline 中间 RGBA8
量化，可能出现合理的低位/过滤误差，尚无本次硬件视觉证明。first admission 前同时
查询压缩 sampled/transfer/BLIT_SRC 与原 RGBA sampled/transfer/BLIT_DST 及完整 image
format 支持，能力不满足则不替换。future allocation OOM 沿原平台分配失败规则；
demote 的暂态需同时容纳旧压缩和新 RGBA，不声称峰值恒降。

ReadTexels 原 decoded BC 路径物理 copy 与逻辑 BC 回调宽/行距合同不一致，独立待修。
本实验先恢复 baseline 表示，不声称修复该 API；T2B 保留原 decoded recompression
拒绝。Pool/raw CPU 私有别名仍按原私有图像规则，不把 ASTC 写回冒充 BC 字节；本实验
不是共享 retile/CPU 编码器或全别名一致性补完。

DWARF image 904→920B（+16B）；无每 cell bitmap。OFF 路径仅新增冷选项一次解析、
状态分支/字段，未做整帧成本测量。ON 初次源上传多一次原 staging 的块分类；首次
admission 有原 fresh cell/alias 校验及双格式能力查询；常态 sampled 不扫 coverage；
实际压缩 copy 沿原 hazard；demote 有全图 blit/分配/退休成本并 sticky 避免来回转换。
稳定 useful storage 模型 RGBA8 128²×84/7mip 7,339,584B → ASTC4 1,835,904B /
ASTC6 881,664B；不是 VMA/驻留/PSS/DRAM 的实测收益。R8/BC4 不在范围内。

主机 LVP 验证使用实际 BC3→RGBA 来覆盖相同恢复/退休代码，原 3×3 mip/layer GPU
像素 oracle、不同 BC 源 ROI、同值 CPU 写与缓存 view 失效、B2T、clear、Storage、
attachment、raw escape、sRGB/serial 均通过；ASan 覆盖 CPUdirty 与 mixedcopy。
元数据策略检查拒绝 initialized/prior-writer/sticky/BC4/色域不符、非法块 ROI/越界，
6×6 合法边缘与内部整块分别检查。原 ASTC metadata 4661 检查通过。
LVP 未提供实际 ASTC，本次 ASTC admission/采样/6×6 硬件 demote 均记 unsupported；
旧 1a 的 42-layer 实机仅是适用性线索，不能代替此实验的硬件/游戏验收。

初版9428cace及其归档保留。v2只把 cached physical-handle 重验移至 upload 与既有
trace readback 两者之后，覆盖trace主动恢复RGBA的情况；原TRACE_FRAME/DIR有限
功能测试验证旧recipe不用于本次binding。不新增捕获器、计时器或实际设备采集。
