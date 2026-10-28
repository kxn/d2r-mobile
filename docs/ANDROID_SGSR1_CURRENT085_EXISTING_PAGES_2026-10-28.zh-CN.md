# 当前0.85原放大接口：既有停帧页核验（2026-10-28）

**当前0.85的真实原放大draw、程序、输出、viewport和纹理句柄已经确认；48字节UV常量与输入descriptor内容不在既有捕获页，尚不能宣称运行合同完全齐备。** 本轮只读旧文件，未ADB、重抓帧、连接调试器或改生产代码。

输入是已有 `store-forward-read-reuse-capture-v2/ACTUAL-ONE-STOP1/capture/` 的22266条planner-expanded实际操作及586页；APK f3576a49、host26a104a8、shader chain cf83dfece8e7。launch同进程PID31135记录实际game/surface均1968×900、scale0.85，启动日志另有3D目标1673×765。

复用既有 `shader-dynamic-load-candidates-v1/map-current-compute-snapshot.py` 的原样snapshot/hash校验与页读取前缀，沿已经展开的操作顺序跟随程序和绑定。没有新写NVN命令解析器。附加类型读取使用官方LLDB仅离线打开该capture准确ELF，从DWARF解原 `tex_t`、descriptor和状态类型；没有创建/附加进程，没有target表达式、目标调用或内存读取。新增脚本只访问已归档的页。

## 实际当前调用链

| expanded seq | 程序和真实状态 |
|---|---|
| 22200 | 原FXAA：FS `ebcfcd1fa0a77dbc`，program serial80；输出tex serial484、NVN format63（R11G11B10F）、**1673×765**；viewport/scissor同尺寸 |
| **22217** | 原upsample：FS **`52aac1f2306b8fc5`**，VS `69cfdc6545a66915`，program serial42；输出tex serial496、NVN format37（RGBA8）、**1968×900**；viewport/scissor同尺寸 |
| 22254 | 原compose：FS `9a650561e5c40791`，同一VS，program serial88；输出tex serial5、RGBA8、1968×900 |

原upsample draw为一次triangle-list、3个index、1个instance。FS已绑定纹理槽10/11的真实handle分别为705、703；不是历史抓帧中的VkImage ID，不应把两者混用。当前反射仍是set1/binding1、2两个sampler2D，binding3原c7，原VS location2 `vec4`，与已通过host回放的候选接口一致。

### 两个容易混淆的slot编号

编译器原c7在NVN状态里对应 **UBO index4**。既有后端 `constant()` 对slot>=3使用 `reg=slot-3`，所以检查NVN index7会找错数据。实际seq22201绑定FS index4到 `0x753f115e00`，长度48字节；它就是要验证的三组vec4。

该slot7/c7三vec4中的UV缩放、物理texel步长、锐化、有效裁剪边界都未知，**不能从历史1.0补值，也不能凭输出比例0.85自行构造后称为当前原值**。

## 确切缺口

以下地址均为该历史停帧PID/地址空间的证据，不能拿到将来重启的进程直接使用。

| 必需字段 | 已知地址/范围 | 既有页缺失 |
|---|---|---|
| 原c7三vec4 | `0x753f115e00`…`0x753f115e2f`，48字节 | `0x753f115000` |
| texture handle705的descriptor | `0xb4000073a5b5f9f8`，184字节 | `0xb4000073a5b5f000` |
| texture handle703的descriptor | `0xb4000073a5b5f888`，184字节 | 同上 |
| 可辅助确认705/703 serial的store footprint buckets491/514 | `0x763b4726b0`／`0x763b472a48` | `0x763b472000` |

descriptor全局指针本身已从捕获页读到，为 `0xb4000073a5b3ff40`，准确ELF给出descriptor stride184，因此上述两个entry地址可核查；缺的是entry内容。不能仅凭前一FXAA输出尺寸就宣称705一定引用该纹理，也不能把703视为某个固定尺寸/格式的增益图。

末端NVN `UpdateUniformBuffer` 只写另一个地址的4字节计数，不覆盖这48字节；已捕获的三个BufferCopy也不提供该c7源数据。因此现有操作载荷不能补回缺失的三vec4。原只读捕获主要为store/descriptor重复工作调查而选页，本来没有承诺包含任意UBO与纹理descriptor，缺页不代表捕获损坏。

## 当前可用与不可用的实验资格

**可以离线准备原/候选两条PSO的真机编译输入。** 创建shader module、descriptor layout和graphics pipeline不需要实际UBO字节或纹理内容；当前原VS/FS已经固定，候选反射也匹配。独立、不绘制的编译探针可查询真实12b驱动、QCOM扩展及features/format，不能把它写成0.85画面/性能通过。

**当前不能做已核准0.85输入的替换绘制或画质结论。** 保留原捕获缺口；仅在后续有必要的真机实验时核准同一实际draw的纹理descriptor与c7，补齐有效区域、采样步长与SGSR更大footprint的条件。不为此在本轮再抓一次，也不退成最终Present全屏锐化。

机器可读结果 `.local/logs/snapdragon-super-resolution-current085-contract-v1/actual085-existing-pages.json` 含真实seq、program、typed target、绑定地址、缺页和准确DWARF字段。离线LLDB命令日志、复用organizer/ELF/原capture SHA及全部产物在同目录 `receipt.json`。这是身份/接口证据，没有时间、带宽、调用耗时或新FPS测量。
