# 共享图像首次 GPU 定义的 CPU 源观察

此候选从冻结 03cbdba 隔离建立，默认共享模式仍关闭。实际 Mali 在已确认共享 heap/capability 后触发 partial-freshness guard；原日志没有逐图身份，不能直接认定是 CPU 真修改，也不能断言本修复就是该次触发的唯一原因。

源上存在一个确定可执行缺口：全幅单 mip/layer GPU clear 会省略初始 CPU upload，写后 initialized=true/cpu_rebuildable=false，但原 dirty scope/stamp 仅在 CPU upload 建立。随后采样把未跟踪当 dirty，因而误触 guard。

修正将 CPU 源观察与 CPU upload 分离：新物理 image 在实际 GPU 写之前，在原 outer preflight 已完成整 Pool import/pending-CPU publication 后，通过只读无提交检查确认 source，再建立原 image-only scope、先取 stamp 后 protect。原完整上传仍更新自己的 stamp；从未 CPU 上传的 GPU 定义不授予 cpu_rebuildable。没有 CPU 数据复制、snapshot、hash 或新 Pool shadow。

同时补齐 c8 OFF gate 与共享模式的整合：普通 draw/dispatch 的 before-borrow CPU-source preflight 以 defer || actual_shared 进入。actual getter 只读已冻结 shared_active，不读环境、不初始化 Vulkan。optional ghost 仍不强 prepare。编译关闭共享时保留原 defer 判断。

不能用 baseline 字节相等证明没有 CPU 写：CPU 初值红255、GPU 清绿0、CPU 又写255，source 可以与旧 baseline 完全相等，但存在合法后写意图。没有 GPU→原始排布 canonical 发布/局部 retile 前，所有 GPU-owned 图像的源页后写仍 fail closed；同页邻居写也保持保守拒绝，不以 memcmp 忽略。

真实 GPU fixture 覆盖首次全 GPU clear→采样、完全不增加 CPU upload/reupload、别页写后仍保留 GPU 绿，以及同页邻写/CPU 同值写/真实值变三个 guard 负例。普通 draw/dispatch 分别绑定全新未 import Pool 的 IMAGE，在 shared/defer0 下由 outer preflight 导入，原 shader 输出精确 cyan/magenta。没有新游戏 shader、计时器或采样器。

新增成本仅冷 image 初始化时的现有 scope/token/page保护（此前 CPU-upload 图本来就会有；额外覆盖从未 CPU-upload 的 GPU 定义图），与两普通 operation 的共享模式只读 gate。scope 为原不可变1MiB bucket/page元数据，不是整 Pool 影子或逐次逐字节扫描。实际 RAM、DRAM、帧率与散热净收益未验收。错误上下文只在原 fail-closed 分支记录 serial/格式/目标/source/span/scope/stamp/generation/external，无每 draw 日志。
