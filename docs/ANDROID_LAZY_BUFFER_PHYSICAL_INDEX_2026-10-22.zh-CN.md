# Lazy Buffer 物理区间解析

基底为cb297f8；依据是现有f75官方PMU与三个30s FP窗口。resolver SELF约2.07–3.12%NvN，pool_address父包络约9%；本改动仅减少resolver的链扫描，不承诺FPS或整个父包络收益。

同一shared_buffer_index节点增加typed kind，利用64B节点尾padding，不新增分配/根/缓存；静态断言要求node64B、kind offset60、pool_backing_span152B。range query只选择一个完整包含请求的Buffer：cover最高generation（原链newest-first）优先，随后唯一canonical。覆盖联合、native image import或两个相邻Buffer都不能冒充单Buffer。

原operation geometry/recording/locked hints先执行；只对qualified lazy非空recording miss和原resources锁内fallback调用新查询。zero/tail、maintenance、nonlazy和泛用unlocked reader保持原线性路径。所有初始化、CPU/GPU页/阶段/visibility/alias/ref/epoch与import预算/OOM仍在原位置。

生产lazy本来要求dedicated NVN owner。该owner在resources内成功import后发布索引，completion只完成CPUeffects和释放refs，不旋转Buffer树；同owner FIFO PoolFinalize finish/关闭image并等待原refs排空后才销毁节点。仅“有引用”不足以给任意线程不加锁读AVL，故未全局替换generic resolver。

不加入rank aggregate；复杂度O(logN+相关候选)，宽重叠最坏仍O(N)。实际回归复用未改线性resolver比较exact backing/offset/size、合法旧covervs新canonical反例、size0/tail/overflow/hole/crossnode、旧retained handle、原OOM状态；再由原GPU alias fixture验证字节、等待与Pool close。所有新增测试helper/body/调用均!DM_ANDROID_APP，不执行真实游戏模块。
