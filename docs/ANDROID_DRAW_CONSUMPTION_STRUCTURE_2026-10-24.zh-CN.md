# Android 绘制消费结构：整包复用与连续附件访问

目标仍是将高通翻译线程降至可支撑60FPS的运行时间。前轮组合复用已证明用户指令减少18%–19%、用户周期减少8%–10%，但Running仍约26.8ms；没有完成目标。本轮依据完整命令身份数据检查是否还能绕过整段准备，不把已完成的UBO、普通图像组、附件表示及PSO缓存再次认领。

## 整个上次draw并不是高覆盖复用单位

输入为同一次成功停止捕获的完整原命令、入口状态、shader/schema及资源身份：native-image-coverage-snapshot-v3，SHA `5bcbd37463289643d5c1e79fac98ca748bb1df36d9c6fe3bd8c7c18ca893ff6b`、ELF `1720393e8c4f316530ba795a6a65b0723be3647a`。45,257条OP、2,445个draw、21个handle，487页验证及完整19,800字节入口S。它是历史同暂停的逻辑身份，不是当前运行时命中率或同步合法性证明。

保守边界内2,387对相邻draw，固定部分相同、只允许只读UBO地址变化的仅144对（约6.0%）；没有固定部分及UBO也完全相同的相邻对。2,243对含其它变化，其中2,241对有几何变化。最长连续整包组11个draw；不相邻的旧包复访约500次，不能据此直接引入多项缓存：物理owner、writer、CPU效果、shader变体、录制代数仍须兑现。

另外1,365对仅几何与允许的UBO地址变化，其余部分相同；加上144对，除几何与只读UBO地址之外稳定的共1,509/2,387（63.22%）。几何分类存在重叠：使用中的vertex地址变化1,845次、range size变化875次、index地址变化1,411次，vertex格式和stream约百余次；索引类型、primitive、basevertex与instance参数同段相邻变化均为0，允许basevertex作为动态参数也不改变144的结果。这说明应区分固定产物与每次实际变化的几何，而不是让一项地址变动拖累整包重建；同时也说明不能简单跳过每次真实Buffer消费。

最终审计清理了bitfield未定义高位，RT按捕获serial和有效view比较，并以VS实际输入槽再次计算，三种口径的相邻结果一致。23个实际使用RT中3个未完整捕获，其保守指针身份保留，SET_RT均切硬段；不以这些未知对象证明物理复用。数据还包含675个已有specialization模块和32个pixel-local候选draw，当前逻辑覆盖不是动态变体选择资格。

原代码已经让UBO计划与普通FS图像组共用prepared_values并合并skip mask，附件借原job数组，PSO命中跳完整key/hash。只把四者包成一个新结构不会再省这些工作。完整解析、逐draw状态差分及边界见 `.local/logs/draw-packet-coverage-v1/`，各后续审计以目录最终冻结报告为准，不能用本段静态次数预测毫秒。

## 选择连续附件访问作为更深的一层实验

现有附件复用仅借用表示，热draw仍执行attachment_access的owner/range恢复、规范化请求构造，然后做依赖查询及访问历史记录。相同实际Render Pass中的固定功能访问，现有dependency代码本来就有raster-pass豁免；寻找这个豁免本身仍有逐项工作。

受限候选让原job持有固定的规范化附件请求，首次真实draw完整执行依赖及记录，后续相同角色的连续访问作为一个run消费。进一步尝试按原事件位置推进精确逻辑clock与generation，把成员字段的重复散写延迟到观察前完成；不是忽略写历史或只保存第一draw。

开始实现前的合同要求：普通非native、不同物理附件owner、同实际pass与固定range/角色；storage writer、query/ghost/局部渲染及未知边界保守排除。原history包括预清与最终record的generation递增、每image use与每cell write时钟、reader清理、layout visibility、zero、readproof、shared alias发布，均须保持等价。

闭包不能只放在image_get/dependency_source：layout whole/range入口会更早读cell，mip admission直接查bucket和alias clock，pixel-local与readproof有直接观察，render_end须在ghost及STORE之前物化。相反，不相交UBO的shared-linear访问不应无条件关闭run，否则会使高频动态几何/常量重新破坏整段收益。具体入口、所有权及字段审计见 `.local/logs/draw-packet-contract-v1/`。

独立工作树 `.local/worktrees/attachment-raster-run-v1`，基线 `c30662c`，默认关闭，尚在实现；没有Android部署或性能结论。验收须包含完整CPU可见image/cell/clock/alias/zero状态的ON/OFF差分，以及中途采样、清屏、barrier、mip/readproof、pixel-local、ghost和角色变化，不只比较最终像素。若完整闭包不能成立，则保留待修实验，不能以少同步换帧率。

## 已排除的周期猜测

额外检查了旧ADPF：同一metadata区间reports增加250、guest submissions也增加250；ON trace中replay238、present239，两轮OFF为239/239。physical fence编号的步长不能当guest submission数量，因此“三个小任务各按一整帧预算汇报”不是该营地的已证错误。没有按这个猜测改ADPF或重新采集，证据为 `.local/logs/adpf-work-period-audit-v1/`。一般多guest/frame的接口契约仍可改进，但不能拿它解释本场景。

本轮未操作暂停的Mali或平板，也没有为静态覆盖检查启动手机。主线没有加入整包缓存；新附件run仅作为隔离实验推进。直到真机同窗CPU、指令/周期和实际出图得到验证之前，不声称已提升帧率或达到60FPS。
