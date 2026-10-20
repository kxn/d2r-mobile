# 系统 Vulkan 的非 CPU 贴图输入预检

基线为 `1a5280a`。其实际 Mali 原游戏已经进入营地，并采用 serial19 的
42 层 ASTC 数组。本次仅减少这个已验证配置的翻译线程工作，不扩大编码适用范围。

原调用每次从纹理注册表复制完整 `tex_t`，随后在 `has_layout == 0` 时退出
CPU 源预检。现在仅在固定 shared OFF、defer OFF 路径，沿同一注册表锁和
valid/descriptor limit 检查，先判断 `has_layout && mem && L.size`。不满足的
注册项不复制整个描述符，也不调用空的 CPU preflight。未缓存这个结果，
每次都观察当前注册内容；共享和 defer 路径继续使用原代码。

有 packaged CPU 输入的贴图仍按原值快照调用完整预检，包括已初始化且看起来
干净时的同宿主页 pending CPU effect 检查。采用 ASTC 的动态数组本来没有
packaged layout，原 outer preflight 也不会消费它的 CPU 字节；其 image_get、
view、upload 内的编码、CPU 脏页、原始 Buffer alias 和 writer 拒绝均未改变。
不改变 dirty、内存保护、上传、查询、提交或完成策略。

官方 99Hz 样本把前端预检/别名组列为 20/519 self、42/519 retained-stack
命中；这个组包含本次没有修改的函数，不能把组占比当作候选收益。本候选
减少无意义快照和空调用，仍保留每绑定注册表锁/元数据检查。整帧收益待后续
同源实际对照；没有新增计时器、采样器、热路径计数器或开关。

必要回归使用原 NVN GPU 测试：同一 id 从非 packaged、NULL mem、空 size、
超出 descriptor limit 逐一改回 packaged；前几种不应等待 queued CPU effect，
恢复后必须发布它。原真实 draw/dispatch、首次上传、initialized clean + pending、
同页邻字节、非重叠页不等待及 GPU 像素断言继续执行。旧 ASTC 编码/alias/生命周期
负例保持。Linux LVP 不支持实际 ASTC 的场合明确报 unsupported，不能算 Mali 验收。

本阶段只交付源码、宿主官方 VVL/SYNC 回归和 NDK API29/O3 编译；不制作新 APK、
不操作设备或游戏资源。统一 APK/NSO 由根代理组合其它候选后重新生成。
