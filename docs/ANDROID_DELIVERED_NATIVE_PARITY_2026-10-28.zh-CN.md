# 交付版与上游官方引擎：同尺寸最终对照

2026-10-28。当前交付的高通原生版本在本轮同尺寸营地对照中，已经快于所测上游官方引擎。独立官方GPU工具窗为 **50.191 对43.419 FPS，约快15.60%**；普通20秒窗为50.477对43.449 FPS。每帧GPU写入只少3.36%，不能继续用“写带宽解释全部帧率差距”。

本文完成的是实际交付版身份、同条件对照与恢复验收。**共享内存单独节省多少CPU指令、额外准备是否吃掉其中一部分，仍未隔离证明**；这个问题不能借不同CPU频率下的Running时间下降替代回答。后续优先复用官方PMU核实净CPU工作，不为强行开关共享而改变资源格式或制造额外拷贝。

## 哪个上游，哪个交付版

- 本地普通APK：`c09ff45b3a39146c7b6f5ef612da1d867406cbababdc7ab178dc9d9fd8fce24b`；host ELF `b828245443280f607c24075e7bd930513b68a567f782d4122ce3645354c97a22`。就是已通过两次无extras桌面启动的交付包，没有另用诊断APK冒充。
- 本地Shader链`cf83dfece8e7`，12b8 Turnip，原BC资源和完整native资源表，共享guest heap/lazy windows开启。covered-clear使用交付默认ON，未改游戏CPU模块。
- 上游源版本`b811b90172f4eb40ec1bc89d903308e0c5ab88d0`，所测隔离载体APK `b8d1fcd10edcc70c2f1f98758efeb396fafdb62ce6c209b3afd4934464313592`。其中`libdm.so`的SHA `e847e70be1a28b756e9058d8028f8302c2ff6f998e58f9916e7744516ddd6d6e`与官方发布APK逐字节相同。
- 上游载体的Java、包名及可调试清单有适配，**不能称整个APK与官方签名release相同**。实际进程使用系统Adreno驱动，没有自带Turnip或抓帧层。
- 上游原生库直接从APK的`ZIP_STORED`成员映射。身份核验覆盖实际安装APK SHA、成员SHA、ZIP数据偏移、官方ELF程序头、真实RX/RO映射和相同load bias，没有假定必须存在解压后的独立`.so`。

早先f8驱动控制包重建了引擎，使用NDK28/Clang19并增加Volk loader；官方引擎使用NDK29/Clang21。本轮不再用f8作正式参照，从而消除这项编译器/loader差异。也不把所测冻结版本泛称所有后续上游或其它fork。

## 真正对齐的条件

同一SM8635/Adreno735设备，两端原生ARM64执行；游戏线程内NVN翻译、实际owner由原日志TID和进程Tgid绑定。实际原日志均证明：

| 项目 | 两端实际值 |
|---|---|
| game output | 1968×900 |
| VkSwapchain | 1968×900 |
| 主3D目标 | 1673×765 |
| 固定3D比例 | 0.85 |
| 受控角色/营地 | Lv3，区域1，5225/6289 |
| 资源编码 | 原BC资源，没有ASTC替换包 |

两端使用相同22文件受控存档。上游外部存储不能正常处理原引擎的xattr枚举，因此正式窗口临时将**唯一DM_SAVE配置行**指向本包私有目录；其它环境字节、原生库和游戏资源均未改。这同时避免把外部文件系统失败当成性能问题。结束后原环境完整恢复，私有测试存档保留为独有审计副本。

系统截图画布为2358×1080，不能据此反推VkSwapchain尺寸。两张正式窗前截图已人工查看：同人物、建筑、墙体和传送点完整，未见明显黑块或几何损坏；触控UI、FPS/HUD层不同已明确，不声称逐像素一致、全程无闪动或全场景验收。

## 正式官方GPU窗

原SDK九项指标和同窗Perfetto，均通过原始完整采集与健康检查。数据按各自完整游戏Surface间隔归约，MB=10^6字节；全局GPU计数包含合成器，不冒称NVN独占流量。

| 同窗指标 | 上游官方引擎 | 当前交付版 |
|---|---:|---:|
| Surface FPS | 43.4193 | 50.1907 |
| Read/Surface | 106.286MB | 105.263MB |
| Write/Surface | 45.732MB | 44.194MB |
| Fragment instructions/Surface | 2.39283G | 1.84517G |
| GPU clock-count率 | 499.144M/s | 499.627M/s |
| Shaders Busy | 80.062% | 71.761% |
| Shaders Stalled | 16.965% | 16.856% |
| System Memory Stall | 0.356% | 0.405% |
| Texture Fetch Stall | 12.172% | 12.087% |
| Wave Context Occupancy | 58.006% | 57.844% |
| owner Running/Surface | 18.5419ms | 16.3693ms |
| owner运行期CPU频率 | 1746.86MHz | 2064.03MHz |
| 全应用Running/Surface | 37.1694ms | 30.5548ms |

GPU读少0.96%、写少3.36%、片元指令计数少22.89%，FPS高15.60%。每秒读写因为出帧更多而可能上升，不能把每秒流量和每帧工作混为一谈。`Clocks / Second`定义是GPU时钟计数率；本轮没有独立物理OPP轨迹，不把计数率接近写成严格锁频对照。

owner Running含guest/HLE和后端，不是纯NVN翻译函数时间。CPU频率同时高18.16%，没有同窗PMU时，不能声称CPU指令也少11.72%，更不能把所有变化单独归给共享。

各应用GPU硬件区间并集分别99.7430%和99.8546%，未知首尾边缘明确保留；这些是执行包络，不等于ALU利用率。当前可认证main批次约19.718ms、出图copy约0.191ms。上游只有HAL提交根，不能可靠把批次映射为同样main/HDR角色，因此保留unknown，没有用最长批次猜Shader热点。结合独立GPU-only改动的收益，当前主要限制更符合GPU工作吞吐，而非大量游戏锁等待或GPU长期断粮；仍不能断言纯ALU或纯DRAM单一瓶颈。

## 哪个改动有因果证据

独立同APK对照已证明，两条VS的精确整数地址规范化使普通FPS约43.25→49.86，GPU main包络22.915→19.818ms。真实驱动中一个精确匹配的VS从349条native指令/48条LDC变为142/12，读取分量不变；规则保留全部32位溢出与进位语义。

再对两条HDR FS的base/spec分别应用规则，普通窗49.88→50.45，独立GPU窗主批19.771→19.611ms，属于约1%的增量。原Uniform选择字段/SpecId保持，188个SSA改写结果通过全32位Z3等价证明。最终跨后端的22.89%片元计数差含此前其它实现差异，**不能全部归给这次FS规范化**。

见[VS因果对照](ANDROID_SHADER_ADDRESS_GAME_RESULTS_2026-10-28.zh-CN.md)与[VS+FS及桌面交付](ANDROID_SHADER_ADDRESS_COMBINED_RESULTS_2026-10-28.zh-CN.md)。这次主要新收益没有依赖继续降低分辨率，也没有减少玩法或直接改游戏状态。

## 共享路径目前能证明什么

当前无条件累加的旧pool同步full/used/protected范围计数、GPU→CPU readback字节及所列CPU同步等待计数，在正式稳态快照中未增长。源码中shared分支会在旧镜像同步入口之前返回；当前native资源确实采用共享路径。这不等于所有CPU memcpy为零，也不等于资源准备消失。它们不覆盖所有共享访问同步及WSI/呈现背压，不能把镜像通道计数为0写成CPU不再读取GPU结果、或所有CPU/GPU等待都已消失。

`pool_copy_accounting`本轮未开，可选拷贝明细是**未采集**；兼容接口`mtl_upload_bytes()`直接返回0，不能作证据。已有快照仍观察到渲染证明等查询活动。上游没有同构的这些工作计数，不补0；两端不同driver/实现且CPU频率不同，尚不能独占归因共享CPU收益。

现协议在launcher、JNI、native资源安装和image绑定处均要求共享及真实memfd别名，无法在保留同6217 native包时合法强关共享。删guard或把native标记当普通NVN布局解释会改变语义。下一步复用已跑通的官方simpleperf指令/周期计数与20秒Perfetto分母，先核净owner CPU工作；不为了取得“独立收益”捏造多拷贝负载或扩大不合法回退。

## 数据质量与恢复

普通20秒上游窗口的原严格检查因25次`systrace_parse_failure`失败，原result保持失败，未悄悄改成成功。独立同版官方parser源码审查逐条解释全部25条为lmkd普通文本；其它error/data_loss及所有drop/overrun为0。866条info级frame parser统计也与866个合法PresentFence端点及源码路径闭合，帧号连续。

另存的scope-qualified审查只准许该normal窗口的Surface/调度/频率分析：原始数据、SQL、source和审查均按SHA绑定；约65.296ms前缘调度未知保留，不补0。该窗口43.449→50.477 FPS是辅助证据，不冒称同窗SDP。主要GPU对照两端均通过原始健康检查，不依赖这项审查。没有为避开memory-pressure日志反复采到碰巧安静才选样。

本轮两个用户数据入口均已收尾：当前c09用户存档按**本次入口**完整22文件归还，SharedPreferences和桌面绑定不变；上游原22文件字节、b8 APK及原env恢复，私有测试副本保留。上游外部目录tar恢复时间戳曾报权限错误，失败原样保留；后续逐文件SHA确认22字节内容完整，不声称目录mtime完全相同。

两游戏及本轮SDK GUI、Xvfb、13个collector均已退出；Mali和平板未参与。本轮测试结束没有再次启动用户存档。日常设置仍为2358×1080/0.75，本文50帧数据针对统一1968×900/0.85，不直接套用于不同像素负载。

主要本地证据均在`.local/logs/final-c09-upstream-parity-v1/`：

- `gpu-tools/comparison.json`、两端原SDK session/完整收条与`hardware-analysis-agent/`：九指标、原始trace、动态context和硬件时基。
- `metadata-bridge-agent/gpu-green-with-sdp-v1/`：完整同窗CPU/GPU对照；`normal-scope-pair-v1/`另列普通窗。
- `health-analysis-agent/PRIVATE-N2/`：限定审查及原失败状态，未修改原trace或gate。
- `final-restoration.json`、`completion-audit.json`和`final-audit-plan.json`：本轮对照/恢复范围的58个证据绑定；不代表共享独立CPU因果或全场景60帧已证明。

尚未证明全场景60帧、长期热稳定性、能耗下降，以及共享单独的CPU净贡献。不能用本轮已领先替这些未完成事项结项。
