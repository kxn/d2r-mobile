# Vulkan 驱动常量表的全路径稀疏比较键

`c0` / `c2` 仍使用原来的完整 GPU ABI 和 64 KiB Uniform descriptor。
本优化只减少宿主每次构造、散列和比较 CPU 缓存键的工作，不修改 SPIR-V、游戏材质、
控制流、绘制次序或游戏 binary。没有以捕获到的材质值推断未来分支。

原完整键包含 c2 的 264 个可变 u32（1056 B），以及 c0 每阶段的 48 个 u32
（CS 同时包含 SSBO、UBO 两个 bank，96 个 u32）。离线 `dmsh_driver_keys.py`
使用官方 SPIRV-Tools 验证、反汇编最终模块，遍历所有函数及所有控制流分支。
它只接受地址完全确定、类型/Offset/ArrayStride 可核查的 Uniform 读取；向量加载包含全部分量。
动态或特化索引、指针逃逸、未知布局或限定符、无法验证的模块，保守回退原完整键。
不依赖运行时分支命中率，不借用 specialization 的局部分支依赖评分当作完整证明。

可选 `.dkey`（DDK1）记录 c0/c2 的精确可变字索引，绑定 metadata、base SPV、完整
`.spec`、`.pred`、`.tssbo` 五份产物的 SHA256 组合。读集合覆盖 base/spec/pred 及由
原 `unpack_pair` 校验后的 alternate/spec-alternate；未知模块不能继承已有证明。
末尾 checksum 和运行时边界检查检测损坏，缺失、指纹失配和非法记录均回退完整键。
部署工具还从实际模块重新计算完整证明并比较产物，不仅接受 sidecar 自报的摘要。
该 sidecar 不递归加入自己的输入指纹；它不改变 GPU 模块、PSO 语义或原 cache ABI。

运行时每次从当前真实 `S` 读取所有可能被着色器观察的字，保留地址高低 32 位、size 和
句柄截断语义；固定为零的 ABI padding/tail 不进入可变键。直接恢复 `S` 同样会重新读取。
只在字数至多 32 且小于完整键一半时使用稀疏键，避免稠密 gather 替代既有 NEON 路径。
last/history cache 同时比较独立 shader/slot schema 身份，散列碰撞仍完整比较全部稀疏字。
alternate 拥有独立 schema 对象，包含晚启用 alternate 的情况。

命中可复用旧完整表中的未被任何可能分支读取的字。MISS 则调用现有
`driver_data_reference` 完整构造原 0xa00 字节，并使用原不可变 Uniform arena；
不会把稀疏键解释为 GPU 表。其余资源定位、descriptor 内容、GPU owner、dirty/upload、
image layout/hazard 和真实完成后回收全部照旧。原 `driver_reference` 保留独立全表路径。

新增成本包括加载时的小 sidecar IO/校验、每个 shader 两个指针、有效 schema 的少量
索引及每个 immutable driver entry 的一个身份指针。MISS 额外构造一次短键后再构造全表；
命中减少的 CPU 字节不能当作 GPU 带宽或 FPS 收益。schema 与现 shader/module 一样具有
device/process 寿命；未来增加统一 shader teardown 时须在不再使用后释放 schema。

当前原 3b0e 链三份 HDR 原始 base/spec 的 c2 全路径只读取 `S.tex[FS][30]` 和
`S.samp[FS][1]` 的低 32 位，比较键可由 1056 B 降至 8 B；其中一份 c0 只读取
`S.ssbo[FS][5].addr` 的两个字，192 B 降至 8 B。此事实是通用证明的结果，不是规则内
的 shader key 白名单；对应原 SPV/meta/spec 字节保持不变。不能据此预报整帧收益。

功能验收复用原完整 64 KiB c0 VS/FS/CS、c2 独立 GPU oracle，并新增 owned compute
用例：相反分支、相同 stage/slot 的不相交 shader schema、观测/非观测字变化、A/B/A、
直接 S 恢复、CS 双 bank、动态索引完整回退、零尾及缺失/损坏 sidecar。相同 GPU 结果还
对照完整 reference 和 template/plain descriptor writes；缓存机制用现成 MISS/hit 状态
断言验证，没有新增生产热计数器、计时器、采样器或线程。

本轮独立 lavapipe 主机 suite（真实执行 Vulkan GPU 操作，不执行游戏 CPU）结果：
异步提交 + Khronos synchronization validation **21,752,159 checks PASS**，
串行 reference + 同校验 **21,654,310 PASS**，无 VUID/SYNC-HAZARD。保留 llvmpipe
既有 texel-buffer SPIR-V warning，没有将其写成新错误或隐去。原厂 Android 真机结果需
在统一 APK 和 canonical shader 重建、哈希部署后另列，主机用例不能代替原游戏验收。

## Android 真机整合结果

正式链 `81b00a6ea49d` 完整重建6931模块；相对已验证原链，6931份SPV、6931份meta、3739份spec逐字节相同。部署工具从实际模块重新计算证明并核对，手机31458份文件验哈希后发布。原生ELF `9c2d816e…` 的Mali严格GPU测试19396672 checks通过；新增proof漏洞修正不改该ELF及GPU用例字节。独审发现的合法后向pointer Phi现已保守回退，原未修链未部署。

原游戏固定3D0.65、温控30的两段对照中，新键将每draw的常量表MISS减少83.8%左右（原78416/1358472draw，新12696/1357368draw），候选窗口没有新建arena chunk。现有descriptor内容MISS每draw也约减少3%。这些是CPU工作量和分配请求变化，不是DDR计数器。实际Perfetto回放均值CPU28.517→28.066ms、加权频率1476.67→1460.43MHz；出图均值33.476→33.488ms，未显示整帧涨帧。只有一对窗口，不能据此承诺净CPU百分比或散热效果。两段均预热288/288/0，采集窗口未新建PSO，保存两次后停止。对照、原始profile和所有边界见 `.local/logs/driver-sparse-key-v2-mali-work-comparison.json`。
