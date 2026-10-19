# CASC 数据档文件名

原始 Switch 3.3.94012 `ExeFS/main`（SHA-256
`55fd909bb7230b002357e5b0d8499c11f06888f345c49e69eb4734df44a27d0e`）
只读解压后的 RO 虚拟地址 `0x1d48e72` 包含格式字符串：

```text
%s/%02lu-%08lx.data
```

因此 chunk 用十进制、archive 用小写十六进制。例如数值 chunk=10、archive=11
对应 `10-0000000b.data`；chunk=17 仍写为 `17`。不能将两个编号都改成十六进制。
旧数据档 archive 0..9 的文件名和字节均不受影响。

`casc_overlay.py` 原有读取、`pack` 输出、TVFS 重写输出均将 archive 写成十进制；
`available_location()` 也按十进制解析已有文件。这个错误来自上游提交
`afad65c895386a96dfb863f2317e41d61222a5a9` 中的工具，在编号超过 9 后才暴露。
现在三个命名入口共用 `data_filename()`，占用位置扫描按 decimal chunk / hex archive
解析；扫描也覆盖 chunk 100..255。EKey 的 key-layout `8 8 40` 中 chunk 和 archive
各只有一字节，分配器在 archive 255 后转到下一 chunk，而不是生成无法编码的 256。
archive 0 仍按原分配策略保留，不自动分配。

`pack` 命令的数字参数继续是十进制，包括前导零。`0-10` 和 `00-00000010` 均表示
数值 archive=10，实际输出 `00-0000000a.data`。也可显式输入 `0x0-0x10` 表示
archive=16；文件名扫描中的 `00-00000010.data` 则始终表示十六进制 archive=16。
不猜测或自动重命名历史错误文件，应从声明的输入重新构建覆盖层。

同次检查修正 `dmx.py`、`tact_bootlist.py`、`touch_layout.py` 两个读取入口的档号
格式；`verify-game.py` 的已知档号均小于 10，其输出保持不变。各工具仍复用原有
BLTE、TVFS、清单和校验实现，没有游戏代码或资源内容补丁。

验证命令（只生成自有数据，无 GPU、密钥、游戏模块或设备）：

```sh
python3 scripts/dev/test_casc_archive_names.py -v
```

测试用原编码器生成 BLTE 和最小 TVFS，实际执行命令入口 `pack`、读取条目表、
通过原 `read_data_file()` / DMX / 两个 touch 读取入口 / bootlist 回读；另通过
原 `make_overlay()` / `rewrite_tvfs()` 构建跨 9 的档，核对新 VFS、root 引用与
多块 BLTE 内容。覆盖旧档名、数字参数兼容、十六进制位置占用、255 边界和编号空间
耗尽。它证明工具链命名及自有格式往返，不代替真实游戏启动验证。

ASTC 小样保留在独立的 `f7b3ada` 源工作树，未重编码，也未改变小样产物或其
`tool_inputs` 指纹。本修复只影响之后重新生成 CASC 档时的存储定位。
