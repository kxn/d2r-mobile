# MineGraph 原生 Mesa pandecode 单次诊断

目标是补齐原022/原BC/full703db真实启动失败的内部CS证据。v5实际q0完成714，q1/q2只完成712；官方generated v10字段解码确认EXTRACT落在713的CALL，而不是最新714。尚无713完整被调用CS，不能据此判断具体shader/内部等待。

## 唯一driver增量

以已真机加载的df9静态unsupported-HAL版本为底，只恢复`decode_common.c`中原先注释的文件名/ctx/frame后缀、`fopen`和失败日志。stderr选项、失败fallback、关闭逻辑原样保留。原df9及失败记录不动。独立-j2完整Android Bionic构建，匹配703db真实host codegen，完整tracked-source和实际compiler-dependency哈希。原APK、game binary、BC、CSF、同步算法均不改。

## 单次事务

沿84f0f01：原022 APK/raw NSO、原BC且UI完全相同的四叶资源、sharedOFF/legacyOFF、请求固定.60/2772×1240；driver/temp/cache/overlay四目录完整owner+CAS备份恢复，原PSO和45314 shader保留。默认只做离线预检，执行须显式`--execute`。

只保存、修改、反读和恢复以下两个property的**原始值**（包括空串）：

- `debug.mesa.panvk.debug=startup,dump`
- `debug.mesa.pandecode.dump.file=/data/data/com.d2rmobile.nativeport/files/runtime/temp/pandecode/cs`

目标在本次完整owned temp中，原生Mesa自动加进程/ctx/frame后缀。不开TRACE/SYNC/kbase_diag，不串行化提交。DUMP会关闭WB cached/no-mmap模式，原decoder读取时也有mprotect，因此本次不能用于性能或未开dump的稳定性结论。原生mapping登记来自device_memory/priv_bo/ring buffer；原CS CFG decoder递归解CALL，未知映射/无法解析也须如实保留，不能将文本反汇编当GPU执行轨迹。

热阶段只读native.log最多64KiB尾部、一次4KiB首PID确认和dump文件size，不拉整份dump；原native.log与decoder大文本分开，save2工具仍读小日志。原launcher75秒、外层100秒，dump总逻辑size软上界256MiB。触顶退出原launch、原生命周期fresh save2后停止；若进程已经崩溃退出则不为save重启。软上界允许检查间隔及存档期间继续输出，不能承诺严格256MiB，也不能承诺在此上界前一定到713；截断是inconclusive，不自动重试。

停机后对全部dump文件做一次SHA清单、单次tar流拉取、逐成员SHA和远端清单重核；不制作热阶段回传/自造decoder。成员拒绝链接、路径穿越、未知名字和重复。若dump拉取失败，先恢复stock目录，禁止清除含未取回dump的owned staging，交root处理；其他失败照原stop/CAS恢复并留真实失败记录。fresh save2失败保留原fail-closed行为，不伪造成功继续清理。

## 离线检查

沿用88个恢复/拒绝检查和16个真实函数backup→publish→restore→cleanup联合检查，另7个property事务/停机后tar验证检查（空串和空格原值、写后传输报错、损坏、未知路径、symlink、进程未停）。直接提取实际官方open/close C函数检查正常文件/后缀/不重开/close、显式stderr及失败fallback。不是游戏或GPU通过证据。
