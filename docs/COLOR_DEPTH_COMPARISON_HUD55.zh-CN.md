# HUD55 通用颜色比较采样迁移（源码候选）

以55a1112为基，迁移c613662已验证的NVN比较采样核心。历史官方VVL观察到R16_UNORM颜色图被Dref采样、缺DEPTH_COMPARISON能力（06479）。本候选修正该API语义；历史c613修后0VUID仍发生CSF失败，不能称为公开RC3挂起修复。本轮没有Android canonical、部署或真实游戏结果。

原深度格式继续用原view/布局/所有权通路。API1.1显式枚举并启用KHR_format_feature_flags2，在device初始化完成后只查询R16/D16两格式一次。R16若真实支持DEPTH_COMPARISON，保留原颜色view，不生成副本。扩展不可用绝不假定R16支持；fallback要求可查询的D16比较能力及采样/线性过滤/transfer/深度附件格式能力。当前范围只R16_UNORM、单样本2D/array/cube；其它颜色编码没有新实现。

不支持颜色比较时，GPU先按全部mip/layer将R16拷到临时buffer，再保留每个16bit写入D16。原R16仍是唯一NVN canonical，普通采样、CPU读回、B2T/T2T和Storage保持原图；D16仅给Shadow元数据bit10的描述符。实际writer通过原image_record递增版本，CPUdirty仍先走原上传/preflight，只有版本匹配才复用D16内容。rawhandle曾逃逸的原图每次准备都刷新，不能用旧快照预测其内容。现有共享CPUalias/dirty观察/native acquire/release检查没有删除或绕过；native depth继续原normal binding。

D16由logical image唯一持有，不进logical list/hash/alias registry；生成的实际GPU请求仍走原image依赖记录。所有physical clone共用image_replacement_detach，将child从旧storage副本脱开，避免completion回调双重释放。parent每次lookup仍更新last_use，parent完成/原八guest宽限后才退休child；旧framebuffer/view使用原退休回调。比较路径不写普通texture-binding recipe，shader descriptor history仍比较实际view/layout。

parent与child的真实VMA allocation都计入private total；仅当parent CPU-rebuildable时child同时计入rebuildable预算。GPU写撤销二者重建证明。guest-only预算淘汰parent时同步释放child，配额/released字节计两者；不改变512MiB候选预算、32扫描/4删除或256chunk年龄。native导入本身不计私有字节，未给native图新增普通Buffer materialization。

成本：image结构904→928B（DWARF，+24B）；设备增加三个int和初始化两次format query。首次比较/真实写后刷新增加一个D16分配（首次）与GPU image→buffer→image完整拷贝，保留真实hazard；命中只比较版本。历史128²×6/mip1为192KiB D16 texel payload，实际allocation按VMA记录；不是减少镜像或RAM收益。

有限测试复用原NVN fixture、GLSL/官方spirv-val及LVP（LP_NUM_THREADS=2），无新renderer/timer/profiler。原2D/nativeD16/cube值和GPU写刷新；新增3mip×3layer全像素、局部GPU写只改一cell、原R16 CPU读回、真实storage promotion、全CPU重写刷新、原8guest退休及guest预算冷淘汰后重建。新增淘汰测试只用真实完成的257 chunks变老，不改时间/完成水位。缺writer版本的独立owned fault必须被同GPU字节oracle拒绝。

首次新增退休测试误将PoolFinalize当作立即删除，失败保留；改用原八guest宽限后通过。第一次fault编排把Ninja shell `:` wrapper当作可执行程序，未运行GPU，保留错误；修正仅host link argv后得到预期GPU断言失败。LVP仅证明自有fixture，不能代替Mali/publicRC3真实原游戏，当前R16原生比较支持路径仅源码guard核验（LVP实际不支持）。

规范依据：[VkFormatProperties3](https://docs.vulkan.org/refpages/latest/refpages/source/VkFormatProperties3.html)、[Depth Comparison Format Support](https://docs.vulkan.org/spec/latest/chapters/formats.html#formats-required-format-support-depth-comparison)。不把depth格式与R16可兼容view混淆，不通过关闭comparison、dummy纹理或CPU回读规避NVN语义。
