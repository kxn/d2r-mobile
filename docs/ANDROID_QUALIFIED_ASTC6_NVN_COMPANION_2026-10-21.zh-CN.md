# 合格 ASTC6 资源的原 NVN 独立资格输入

这是工具和资源准备候选，不修改、不编译 APK，不访问设备。使用已通过 ASTC4 资格的原薄包 `com.d2rmobile.nativewindowfamilyfixture`，APK SHA `0c5fbf47…ed459f`、六个库和全部着色器保持原样。实际 ASTC6 NVN/GPU 消费尚未通过，不能把已有 API matrix 或 ASTC4 收条换名当成新验收。

`prepare-NVN-companion.py --mode qualified-ASTC6 --qualified-format 165|166` 是显式新入口；旧 `new-positive`、`old-AA-negative` 输入和格式约束保留。两个新输入均来自已经完成的 pilot32 原 f830 真机 producer：

| VkFormat | 实际资源 | Builder | 原始 mip 链 | 原生不透明 payload |
|---|---|---|---|---|
| 165 / ASTC6 UNORM | `andariel_armor_KTINT.texture` | 256×256 / 9 mips | mip0…8 | 53,248 B |
| 166 / ASTC6 sRGB | `act1_town_camp_firepit01_a_alb.texture` | 256×256 / 9 mips | mip0…8 | 53,248 B |

只选择已在32条实际烘焙和6217条完整混合 composition 中的两项，不增加或重新编码资源。原质量筛选、normal/array/UI 排除和来源约束由不可变 preparation/composition 收条保留。源逻辑名按原 serialized case 留在单条 pack 中，不手改原 full-family 表。

## 原 C 夹具的适用范围

实际编入原 APK 的 `native_vulkan_native_resource.inc` 从真实 `nr_input` 读取 VkFormat、NVNformat、block 大小、各级宽高/偏移/字节；只有 shape 固定为256×256/9mips。`nvnTextureBuilder`、readback 和普通参考图像上传均用该真实值。`native_vulkan_native_previous_astc.inc` 没有 ASTC4 格式硬编码，使用真实压缩块和 native recipe 的实际物理字节。

两项均保留完整 `DMRSRC1` header/原始压缩 mip 字节，原 strict packer 从实际 source/CFT/native wire/metadata/payload 重新生成单资源1216B表，不叠改已有表。family 的原 NVN thumbnail 起点仍为 mip4，完整 tail 有5级；原检查数447、image-only36和previous-writer40，不降低这些断言。

保留原所有 mip/baseMip 取样与 ReadTexels、CPU同值写入并原 flush、无 Buffer 时取样、late真实 Buffer 全payload拷贝、两个 window union/lifetime、原 packaged tail swizzle+B2T、native与普通图像拷贝、FS/CS/transfer producer依赖与真实改变像素的正负检查。原 sampler 为 NEAREST；该 NVN 测试本身不冒充任意过滤质量验证，已有四格式 bilinear/trilinear API参照是独立证据。

原 previous-writer 夹具要求 mip4 的前两个16B ASTC块不同，随后还要求实际 FS 像素变化。pilot32 第一项165 `status_mask_tint` 的这两个块相同，因此主动避开；本次选定的两个资源块不同，但没有在 CPU 上编写 ASTC解码或过滤器，不能据此预先保证像素变化。真机若原 CHECK 拒绝，应按真实错误调查，不改小断言或制造新像素。

## 严格来源和模式隔离

新工具钉住32条 root实际验收、全部实际 batch receipt/source/input/opaque wire/metadata/payload、完整 composition、d1fa SourceReady及其已验证输出清单、原 strict packer和 f830 build manifest；三个真实 UUID 与 producer8064一致，拒绝旧AA表或错配的4×4资源。

每次离线/执行前都验证选中项确实属于32条实际 producer 和6217组合。单条表除了自身摘要/pack/driver/producer，还逐字对照原 `packer.load_entry()` 从实际已烘焙材料得出的 recipe 和 family。不能靠同时更改 JSON/table自报SHA让另一份表混进来。

原 row guard 默认仍只接受158；只有显式 qualified-ASTC6且严格资源准入通过后才接受其精确165或166。447/36/40、9mips、5tail、完整像素/字节、实际driver/PID/maps/ordinaryUID均不变。新成功阶段为 `actual-qualified-ASTC6-original-NVN-native-window-family-owned-passed`，不使用原ASTC4成功名称代替。

安装、首PID所有权、90秒单次功能上限、正常/崩溃收尾、卸载独立包、删除自有tmp、原游戏只读前后校验与同一APK身份的全部函数复用原控制器。默认输出离线计划；两个格式应由 Root 串行分别运行，完整清理/原环境校验之后再决定下一次，不自动批量启动。原游戏必须停止，不接管他人进程、不改系统验证设置。

## 离线验证与证据边界

使用实际两项材料的 source/header/mips/table/pack 资格与变异拒绝测试；原 ASTC4 48项 row guard 保留。新测试124起草后补充非qualified格式注入门，最终125项，涵盖错format/block/shape/mips、相同块前提、错producer/proof、同时改写table+JSON自报SHA的recipe/family变异。合成的terminal行仅测试控制器拒绝规则，不作为 GPU 证据。

保留准备v1的工具 `identity` 字符串被当字典读取失败及单次测试脚本的语法错误；实际材料未改，修正工具后在全新v2输出目录完成，不覆盖失败记录。没有APP/驱动/游戏字节更改，没有设备、GPU执行、游戏资格或FPS/DRAM/发热/整体内存收益结论。32条payload减43.3%只属于该32条不透明分配，不能外推整个2838项或游戏总内存。
