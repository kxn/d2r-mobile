# 完整 MineGraph 驱动单次真实游戏准入

只测试完整上游 `703db0ebb9afb2c97ae31f24dba5981c7c765fc1` 的 Android Bionic 驱动能否在 PHP110 上加载并启动原游戏。使用已经安装的原 `022d6254` APK、模块和 full ASTC4 资源；shared OFF、legacy ASTC copy OFF、固定 0.60、输出 2772×1240。没有旧 capture 重放、游戏 binary 修改或性能验收。

新驱动真实查询内存属性和 requirements 后由原后端分配。与旧驱动 image requirements/memory type 索引不同，所以不能沿用旧 capture 的 none-memorytranslation 假定。该上游驱动未包含我们任意 host pointer 导入实现，本次不证明共享内存。

工具默认离线。运行门禁逐项检查完整驱动源码/编译依赖、初版失败和最小平台适配的固定 source-ready 哈希，以及原022的 APK/ELF/35项资源模块。真实执行前重新读取当前 shader 全树、driver/temp/app cache 全目录和 GPU layer 设置，不覆盖历史基线。

原 driver/temp/cache 原目录以 stopped-app CAS 原子移入唯一备份；实验使用带 owner token 的新目录，旧 PSO 不被使用或修改。只启动一次。成功后先保存原 HUD/截图/maps 再断言；失败保留 native/crash 输出，包括 libhardware 命名空间装载错误。活动首 PID 严格归属当前事务；结束经原 Android 生命周期得到 fresh save2 后停止，再以 hash CAS 恢复目录。进程已退出时不为存档重启。未知 PID 或目录篡改时保留备份并明确失败。

既有 direct ICD 不组成 loader validation layers，因此这里没有 VVL 通过声明。原层设置不改，截图待人工验收；HUD 到达也不是整个游戏或性能通过。

`test_panvk_minegraph_game.py` 对实际 restore 函数检查目录存在/缺失、部分发布后恢复及幂等32组、篡改拒绝3组、错误驱动凭据拒绝1组；不执行 ADB。
