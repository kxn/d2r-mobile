# SGSR1探针首次安装编排失败（2026-10-28）

**首次 `ACTUAL-COMPILE1` 在安装确认阶段超时，没有进入shader编译。原因是我复用安装器时漏核了实际应用标题。** 此项失败不能解释为SGSR1、原FS、12b驱动或pipeline创建失败。

独立APK包名为 `org.kxn.sgsrcompile`，SHA `1d3b8eb03b10e09bbaae1bce48eff186a59655bb1c8962235c4e3f95a213025f`。官方 `aapt2` 从实际APK导出的manifest标题，以及root保存的手机安装器XML，均为 **`SGSR compile-only fixture`**。复用的 `scripts/dev/experiments/native-bc3/native_batch_installer.py` 在处理安装按钮之前，硬性要求当前安装器窗口包含 **`D2R Mobile`**；标题不同便返回空动作。

原安装收条明确记录actions为空、100秒pending install超时；原控制器没有取得fixture PID，也没有启动它，因此没有两PSO的创建结果、IR3输出或QCOM查询结果。控制器的主游戏安装/driver/profile/resource锚点前后保持相同，原失败与图片/XML保留。

此前41项主机检查核了输入、签名、打包、driver和零绘制范围，但**没有覆盖复用工具的安装UI标题合同**；dry-run也没有执行安装UI。它们只能称主机输入检查通过，不能称完整安装路径已覆盖。这个遗漏属于准备工作不完整，应明确承认，不能拿“已有工具曾成功”替代核查新包实际标题。

root正在同一个已显示的安装会话内，基于当前明确标题使用现成UI工具恢复确认。本agent没有操作设备、重发第二安装请求、重建/改名APK或修改控制器。原失败收条不覆盖成成功；后续若root取得恢复后的真实编译结果，应使用独立的恢复资格记录，分别说明普通UID、实际APK/driver/shader身份及清理，不能据后来的成功否认本次编排错误。

后续复用安装工具时，必须在请求安装之前核对实际APK manifest标题与工具的预期标题。若做成显式参数，仍应匹配已验证的准确标题和当前安装器包/资源ID，保留单请求、唯一包身份与清理约束；不能通过放宽成任意安装窗口来绕过。

只读分类证据在 `.local/logs/sgsr1-compile-qualification-v1/install-failure-audit-v1/`，包含当时原控制器与安装器收条的独立拷贝、root已保存UI XML、官方APK manifest导出及源码/原产物SHA。此前frozen、host-check、APK与编译输入均未改写。
