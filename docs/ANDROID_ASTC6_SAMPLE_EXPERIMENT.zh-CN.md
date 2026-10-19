# ASTC6×6 离线小样（不部署）

`dm/tools/astc_pack.py` 增加显式 `--block-size 6x6`。省略该参数仍走原4×4函数；`--threads`、`--normal-block-size`只影响6×6小样分支。新分支要求非空 `--only` 和 `--limit 1..16`，不提供全量转换或部署入口。

当前仅接受原始RomFS中的2D BC3：descriptor `type=2/depth=1/layoutType=3`，完整文件头、尺寸、层级、layoutData、各mip序列化大小都须与原缓存及NVN C布局一致。其他类型明确列入 `unsupported`；格式/元数据矛盾或损坏的mip表直接失败，报告保持 `complete=false`。输出目录不能复用，避免旧成功产物混入。

## 格式与布局

已核原版3.3.94012 main NSO SHA256 `55fd909bb7230b002357e5b0d8499c11f06888f345c49e69eb4734df44a27d0e` 的只读格式表VA `0x1df0c24`：游戏码95/81对应NVN ASTC4×4 `0x79/0x87`，99/85对应ASTC6×6 `0x7d/0x8b`。工具使用这组声明，不执行或修改NSO；不能据此保证其他构建的格式表相同。

6×6不能沿用4×4按块原地替换的做法。新分支逐mip执行：

1. 通过 `romfs_art.py` ctypes加载原 `nvn_tex.c`、`nvn_deswizzle.c`，按原descriptor解开BC3块线性布局。
2. Pillow现成DDS/BC3解码器得到实际宽高RGBA；不改变通道、不使用会重排法线的 `astcenc -normal`。
3. 真实astcenc编码器以 `-j 1/2` 压缩，检查ASTC头的块尺寸、原宽高、深度和有效块字节数；拒绝可能覆盖线程参数的脚本wrapper。
4. 复用原 `nvn_swizzle` 写入新ASTC布局，重新生成完整mip表的size和相对offset；保留尺寸、levels、layoutData及其他文件头字节。
5. 再解排布，比较ASTC块逐字节相同；现成astcenc再解码验证RGBA一致。

完整 `.texture` 数据按NVN padding排布；缓存中的thumbnail是紧凑线性块序列，必须区分。当前小样还要求thumbnail从 `thumbnailMip` 起的实际尺寸、级数和原始块内容与完整贴图对应mip相同，按新块网格完全重建其表与数据。不能把完整文件每mip的512字节等padding直接套给thumbnail。

## 使用

Linux示例（传实际astcenc二进制，动态库路径按本地安装设置）：

```sh
export LD_LIBRARY_PATH=/path/to/astcenc/lib
python3 dm/tools/astc_pack.py /path/to/original/RomFS /tmp/firepit-mixed \
  --astcenc /path/to/astcenc --block-size 6x6 --threads 2 \
  --only act1_town_camp_firepit01_ --limit 6 --preset thorough
```

该配方默认 `_nrm.texture` 仍用4×4格式，其余已支持样本用6×6。法线的4×4小样也是逐mipRGBA重编码，不承诺与现有全量ASTC4包逐字节相同。显式 `--normal-block-size 6x6` 才生成全6×6质量对照。

原默认4×4路径的兼容验证则必须省略 `--block-size`，与原脚本使用相同encoder、preset和线程参数，比较完整texture、thumbnail/cache、quality.tsv的精确字节；不把“同格式”当作“相同编码文件”。

输出 `repack-report.json` 含全部原始texture/thumbnail、工具/布局源码/共享库、编码命令、各mip和输出文件SHA256。`quality/` 保留RGBA输入、实际ASTC文件、解码图、原工具日志。PSNR来自astcenc官方 `-t` 模式；该模式float EXR与实际编码文件的float解码逐字节相同才接受其指标。额外的alpha阈值覆盖和GA法线估算角误差用于补充工具不能直接覆盖的原通道约定，不冒充游戏Shader执行结果。

**小样缓存基于原RomFS，其他条目仍是原BC格式，不能直接覆盖已有全量ASTC4缓存。** 如后续组合实验overlay，应以已验证的全量ASTC4 pack/cache为基线，仅合入选中99/85条目与对应文件，其他条目逐entry不变；法线95/81应保留已有基线文件。这里不执行该组合、打包或部署。

## 验证与证据边界

自有BC3容器测试覆盖1×1、3×7、5×5、6×6、7×11、17×13、65×33、257×129、不同mip数、SRGB/linear、alpha变化、thumbnail、损坏offset/size以及不支持的类型；使用同一C布局和现成codec，无GPU。

```sh
ASTCENC_TEST=/path/to/astcenc python3 scripts/dev/test_astc_repack.py -v
```

报告中三类尺寸分开：

- `linear_sampled_block_bytes` / C `nvn_deswizzle_geom`：有效压缩块和理论上传内容字节。
- `C_NVN_storage_bytes` / `nvn_tex_layout`：NVN存储布局大小，对应原GetStorageSize；包含包内padding，可能只缩减一部分。
- 实际VkImage memory requirements、采样器缓存、DRAM读写、整帧收益：未使用GPU，全部未测。不能把离线字节比例直接乘原厂系统GPU聚合带宽，或当作帧率收益。

首批6张火坑图中，256²全链有效块87408→40096字节，但保留layoutData后NVN padding89600→64000；尺寸不变。全6×6的两张法线顶mip估算均值约3.6~4.2°、p95约9.4~12.3°，因此默认配方保留法线4×4；六张不透明/材质小样也不能证明所有透明裁剪、UI或其他场景可接受。
