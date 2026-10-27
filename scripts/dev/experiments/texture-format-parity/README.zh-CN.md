# 复用现成编码的普通 NVN 纹理覆盖层（离线实验）

目的：为上游 Android 后端准备与我们当前资源**同一套 ASTC4/ASTC6 压缩块**，去掉我们独有的 DMN/驱动物理布局包装。它不重新编码，不修改游戏 binary，也不覆盖原 S2X。这里不是运行时实现或性能结果。

## 构建与验证

输入配置是本机 `.local/logs/texture-format-parity-20261027/build-inputs.json`，含完整路径和 SHA。依赖原游戏 RomFS、原上游 UI overlay、现成 pre-native `source.texture`/声明、完整 cache、array closure；这些私有资源不随脚本分发。Python 环境需要原工具的 numpy/Pillow，原 NVN layout 库必须已构建。

```sh
.local/venv/bin/python3 -X cpu_count=2 scripts/dev/experiments/texture-format-parity/build.py \
  --config .local/logs/texture-format-parity-20261027/build-inputs.json \
  --output .local/logs/texture-format-parity-20261027/candidate-v2
.local/venv/bin/python3 -m unittest discover \
  -s scripts/dev/experiments/texture-format-parity -p 'test_*.py'
```

输出必须是新目录；失败产物不会当作缓存命中继续。脚本复用 `dm/tools/casc_overlay.py`、`astc_repack.py`、`astc_compose.py`、`astc_array_groups.py` 与原 NVN deswizzle，只验证已有块；不调用纹理编码器。

验证覆盖完整 cache 的名字、顺序、描述符 schema/格式/尺寸/header/全部 mip、有用块 SHA、缩略图与数组闭包。选中资源之外，cache 必须与原 BC cache 完全相等；原 CFT 证明只改选中纹理和完整 cache，root/config 只改索引引用。全部 CASC 替换资源通过原 reader 精确读回。原上游 UI 清单保持，仅 `.build.config` 与新档案有意变化；重新生成并验证原 manifest。`composition.json` 记录输入、工具和每个非链接产物 SHA，`composition.sha256.json` 锚定该记录。

本次 `candidate-v1/candidate-overlay` 实际含 110 个文件，259,627,238 字节（含 manifest）。6217 条选中资源：ASTC4 3379、ASTC6 2838；其余 24855 条完整 fallback 描述符及原资源不变。不是把全游戏都换成 ASTC。没有 DMN 表或驱动物理 payload。

## 上游冷选择入口（尚未部署）

上游 b811b90 的 JNI 会无条件将 `DM_ROMFS_OVERLAY` 设为原 `dm/overlay`，直接改 env 中该键不能选择新资源。`upstream-overlay-select.patch` 是独立诊断补丁，只改该 JNI 启动入口：

- 适用 b811b90，也可叠在 `scripts/dev/experiments/upstream-driver-control-v1.patch` 后；后者不改 JNI。不要重复应用同一补丁。
- 未设置 `DM_DIAG_TEXTURE_OVERLAY`，保留原 overlay/default BC 资源。
- 明确设置时，仅接受真实路径位于 **app 私有 files/texture-format-parity/** 里的子目录。空值、越界、逃逸 symlink、无效/过长路径均启动前失败。
- 仍执行原 `dm_overlay_validate` 和 `dm_overlay_require_touch_art`，不绕过源指纹/全清单/UI检查。
- 环境文件依然由原 loader 在冷启动读取；同一诊断 APK 两臂只增删这一行。候选例：`DM_DIAG_TEXTURE_OVERLAY=/data/user/0/<pkg>/files/texture-format-parity/candidate-v1`。

后续部署应复制整个 `candidate-overlay` 到全新私有目录，核每个 SHA，保留原 UI overlay 和 S2X；只在游戏已安全存档停止时以原 env 字节 SHA 做 CAS 更改。两臂固定同 APK、游戏输出/main/swap extent、driver/策略、角色/场景和其余 env；结束按修改后 SHA 比较并恢复原 env 字节。不要只替换 descriptor cache，漏掉 archive/config 会导致错误资源。这里没有执行 ADB、部署或改动现有 env。

## 运行准入缺口

上游 `xbg_game.cpp`/`nvn_tex.c` 的普通 ASTC 格式和布局接受全族，**但动态 ASTC→decoded BC 纹理 copy 特例当前只接受 4×4**（`xbg_game.cpp:3376`、`xbg_bc_policy.h`）。若这套 ASTC6 资源进入该路径，可能被原错误检查拒绝；不能把离线通过当成可玩或同编码 GPU 准入。必须先以实际格式、copy 日志与画面验证；未验证前不报告其 FPS。

原始 BC3 与 ASTC4 都是每 4×4 块16字节，不能仅凭 ASTC4 名称声称压缩字节减半；ASTC6 才改变块覆盖面积。实际 GPU 流量还受采样、内部布局、copy/解码策略及画面负载影响。该实验只补足编码因素对照，不直接归因共享内存收益。
