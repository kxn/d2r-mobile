# 仅 BC3 的原生共享验证

这组实验保留现有共享内存路径，将 native image 的资格精确扩展为 BC3 UNORM/sRGB 两种格式。游戏原始资源保持 BC3，不写纹理 encoder、tiler 或新 Vulkan loader；不修改游戏模块，也不关闭共享来获得“同编码”对照。

生产工具改动只有 `scripts/dev/prepare-native-image-resource.py` 的显式原始BC3入口；runtime/packer 的两格式映射和拒绝测试由同轮其他任务实现。Mesa 仅增加两种格式白名单，独立源码/构建记录在 `.local/build/turnip-native-bc3-v1/`。日常 B3/8064/ASTC 包、资源、profile均保留，不给未知驱动冒用旧native表或已验证coherent身份。

## 原始输入与身份

game61→NVN0x44→Vk137 BC3_UNORM，game62→NVN0x48→Vk138 BC3_SRGB，均16字节/4×4。完整原cache实证当前6217条转换纹理全来自这两个原格式；不是扩展所有BC家族。原 NVN C 布局、full_texture/header/mips、deswizzle/swizzle原样复用。

```sh
.local/venv/bin/python3 scripts/dev/prepare-native-image-resource.py \
  --original-bc3 --producer-sha256 <新producer源码SHA256> \
  --romfs /path/to/original/Update/RomFS \
  --logical data/hd/character/common/status_mask_tint.texture \
  --output /new/private/input
```

该模式拒绝与 `--pack-index`、`--data-dir`、pack/composition manifest混用，要求显式新producer指纹。原CASC目录只读，核原manifest的decoded size/MD5、实际源archive SHA、原cache/header/全部mip与有用块roundtrip。读取会话只哈希各源archive一次，结束前再查输入变化；不会每个小纹理重复扫同一archive。声明使用 `source_profile=original-bc3-raw-casc-v1`、`intended_producer_sha256`、`linear_compressed_bytes`，不声称拥有旧ASTC编码/静态只读权限。原ASTC入口与其 `linear_ASTC_bytes` 字段保留。

## 已完成的真实设备证据

精确新driver ELF SHA：`0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024`。query及maps均在普通app UID实查；没有从ELF SHA推导UUID：

- layout UUID `ab91c540847448360a44496b50045159`
- device UUID `1e63ed8a4bbaa1bb6e51b8904fb7eb2f`
- driver UUID `9a7f2756bbbe90edffc4f16731ce2be4`

v1使用独立producer `9a8a5a64bc4dc8b9e19fb35d2ff1c0d0bd72f307f2e772f8a18e4e9d6f99019b`。原generic8×8/4mip fixture保留ASTC4/6回归并新增BC3两种，native-import共24个格式/生命周期/pattern组合，303012 checks通过。两个真实256×256/9mip原BC3资源各228 checks，原host-copy→native opaque→compact useful block读回通过。注意这一阶段本身不证明真实游戏采样或NPOT。

v2为独立producer `9194548d1b1c897d299d6f42b9336aa445a48f39c21f099a9b806b77fbace8fa`，保留v1所有原产物不动。沿原同一采样shader、host-copy和普通fixture入口，增加BC3-only13×9/4mip、非端点RGB565颜色及sRGB CPU oracle（RGB允许1字节量化误差，alpha精确）。每次import样本均与同块、同mip的**普通非external optimal VkImage**读回SHA精确相同，8个格式/生命周期/pattern参考全部通过，native-import 113948 checks。没有使用另一轮native export/import作唯一像素oracle。

v2也重新烘焙两份相同真实BC3输入，两个物理payload SHA与v1逐字节相同；recipe/metadata因新producer身份有意不同。UNORM payload为`cd564282af3313d0ed1a12ae396aceabeca1170ce7517817afef8061640b0b57`，sRGB为`bc0b0da917fd9ac31f474039985cf6e30ef008aa1949171b636e1dd60bbc9bef`。

两轮各一次安装、四次独立冷启动，均只使用 `com.d2rmobile.turniprealproducer` 原Java/JNI/6库loader，不加载游戏。已停止/卸载该测试包，清理本次确切临时目录；原391 APK/437 ELF、28项module、shader链29e56、profile4ccef、receipt5b285、native表0c24全部只读核验不变。原系统保护、驱动选择、主包和存档没有改写。没有FPS或游戏可玩性结论，也没有给新driver添加resident/coherent生产白名单。

后续原NVN full/native、thumbnail/tail、Buffer GPU writer/alias/publication/retirement已对BC3两格式真机通过；初次旧fixture在采样前因不满足现有committed RW alias约束失败，修复只接回原guest heap/SVC入口，没有放宽生产准入。再以同一72ad APK对线性0e53与平铺3e937两driver完成resident+lazy正向验证，每次mip采样后检查实际owner==2，并检查现成startup统计lazy active/64KiB。所有GPU/CPU发布与释放检查保留，未使用provider override。原391及其资源/profile/28模块在这些独立测试后仍保持。

两driver均已对完整6217名单重新烘焙并打包，平铺使用新layout UUID和新opaque payload，未重标线性数据。两完整cache/原始压缩块/thumbnail/顺序相同；物理payload分别443,588,608与696,385,536字节，不等于实际游戏驻留。原CASC合成两份完整候选overlay均逐项读回通过。真实游戏及SDP整轮收益仍待验证，不能以这些正确性测试替代。

## 完整名单的原始 BC3 输入

`prepare_full.py` 固定使用当前6217条native名单及顺序，复用原始CASC入口、NVN布局库和128项batch协议。它先检查完整名单的尺寸、格式、mip及thumbnail资格，再准备全部输入；遇到不支持的条目会失败，不将剩余子集标成完整。`verify_full.py` 调用冻结v2 producer的现成CPU解析模式，核全部batch及输入SHA，不进行GPU加工。

```sh
.local/venv/bin/python3 scripts/dev/experiments/native-bc3/prepare_full.py \
  --romfs /path/to/original/Update/RomFS \
  --producer .local/build/native-bc3-producer-v2 \
  --selection .local/build/sparse-native-rebake-v1-inputs/batch-prepare.json \
  --output /new/private/bc3-inputs
.local/venv/bin/python3 scripts/dev/experiments/native-bc3/verify_full.py \
  --producer .local/build/native-bc3-producer-v2 \
  --prepared /new/private/bc3-inputs/batch-prepare.json \
  --output /new/private/bc3-input-verification
```

实际完整结果为4048份UNORM、2169份sRGB，共49批，位于`.local/build/native-original-bc3-full6217-v2-inputs/`。全部53701个mip的有效原始BC3块401,317,552字节；相同名单当前混合ASTC块318,250,336字节，少20.70%。这是离线整包有效块量，**不是每帧读写、显存驻留或帧率改善**。全部输入已通过原NVN往返和v2 C解析，两driver真机49批native布局加工已完成；游戏使用与性能仍待验证。

## 复现入口与产物

- `prepare_producer.py`：从原b111473 git archive及冻结7源码清单建立新v1副本，真实tuple、BC3负例、sRGB块生成/skip门控的完整差异在 `producer-v1.patch`。
- `prepare_app.py`：只扩原fixture Java/JNI现有native-bake/native-import模式白名单，仍强制run_game=false、validation=false、冷进程、原loader。差异在 `app-entry-v1.patch`。
- `prepare_reference_producer.py`：由已验证v1副本创建独立v2 NPOT/reference源和新身份，原采样shader未改。
- `prepare_two_real.py`：只准备两份原始BC3资源，不编码、不烘焙。`seal.py`核实际APK/6库、CMake七源/ELF嵌入producer身份、driver build/真实ELF SHA和完整源ledger。
- `prepare_controller.py` 复用原 frozen ordinary-UID控制器，`--reference`生成v2；`run_probe.py`、`run_reference_probe.py`默认仅输出离线计划，显式 `--execute`才操作手机。保留原PID/crash归属、明确安装确认、失败停止、单次owned包清理逻辑；一轮四个模式共享一次安装和driver push，降低重复准备。没有自写profiler/计时器。
- `verify_baseline.py`按原trusted rewrite manifest汇总本机产物，再一次 `run-as sha256sum`核手机所有模块及独立profile/table锚点，只读、不构建/部署。

本机产物：`.local/build/native-bc3-producer-v{1,2}/canonical/ready.json`包含APK、6库、源码/补丁/producer/driver/输入SHA；v1真实源在`real-inputs-v2`，v2在`real-inputs`。完整真机证据 `.local/logs/native-bc3-gpu-probe-v{1,2}/actual/result.json`，独立前后原包身份报告在各log目录。它们包含私有资源，不提交到Git。

owned测试（两个host producer版本各14项通过）：

```sh
.local/venv/bin/python3 -m unittest discover \
  -s scripts/dev/experiments/native-bc3 -p 'test_*.py'
DM_BC3_HOST_PRODUCER=/path/to/v2/host/native_image_payload \
DM_BC3_PRODUCER_READY=/path/to/v2/source-ready.json \
.local/venv/bin/python3 -m unittest discover \
  -s scripts/dev/experiments/native-bc3 -p 'test_*.py'
```

CMake producer的 `native_image_payload` 是可执行文件，`native_image_payload_embed`才是原Android App嵌入的OBJECT。shell UID执行前者只可作为输入/host断言证据，不可冒充ordinary UID GPU通过。独立source是git archive，原packager可能发现父工作区HEAD；seal会保留该ambient值，并明确真正源身份来自b111基线、独立补丁、源码ledger和实际编译依赖清单。
