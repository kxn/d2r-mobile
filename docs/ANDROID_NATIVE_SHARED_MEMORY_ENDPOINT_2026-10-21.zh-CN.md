# 当前共享路径的内存端点

精确8480a53在Q735/f830/native6217、固定0.60/1MiB窗口完成原游戏路线后，使用手机现成 `/system/bin/showmap -a -v -o json` 取一次普通应用UID的映射快照，随后存档退出并恢复原包/数据。调用在全部性能窗口之后，没有APP采样器、调试器或自写smaps读取器。原始输出和独立汇总在 `.local/logs/native-candidate-source-reuse-8480-Q735-v1/native/official-showmap.json` 与同名 `-analysis/native/official-showmap-memory-summary-root.json`。

系统输出含5057条映射和一条TOTAL。分析排除TOTAL后求和，并逐字段与TOTAL核对，不能把TOTAL再加一次。以下仅为该端点的映射PSS，不是峰值或整个GPU分配量。

| 项目 | PSS MiB |
| --- | ---: |
| 全部进程映射 | 1800.17 |
| 游戏共享memfd堆（两个视图合计） | 1411.82 |
| scudo primary | 97.85 |
| scudo secondary | 61.55 |
| kgsl映射 | 92.95 |
| 其它匿名映射 | 89.14 |

共享堆占映射PSS约78.4%。游戏视图PSS1182.32MiB，导入别名视图229.51MiB；两者对应同一个memfd。两段4GiB虚拟地址不是8GiB物理内容，RSS也不能直接当独立分配相加。PSS与文件/匿名/共享映射的含义参考[内核proc文档](https://www.kernel.org/doc/html/v6.17/filesystems/proc.html)；showmap是[AOSP现有工具](https://android.googlesource.com/platform/system/memory/libmeminfo/+/refs/heads/android-s-qpr3-beta-1/tools/showmap.cpp)。dumpsys的GPU memtrack分类与映射PSS并非同一个总量，驱动allocation counter也不能再直接加到PSS上。

该共享堆包含游戏CPU数据和原生图像payload，不能把它全部称为贴图、空闲内存或可回收缓存。当前原有Pool计数中的扫描、镜像、shadow、上传、写回搬运均为零；私有图像计数仍单独保留。既有较早showmap/ASTC6结果没有丢弃，本次不是与其匹配的内存收益对照。也不能用这个单一营地端点证明6GB设备持续游玩安全。

下一项可验证的资源方向是把已有质量合格、排除法线/数组/UI的ASTC6资源接入现有native加工链。当前6217原生资源与既有合格ASTC6组合交集为2838；源NVN padded量160880128→117306880字节，ASTC有效块量154657728→71590512字节。这些是已有源配方的字节量，真实f830 VkImage排布、分配量、采样带宽与游戏视觉还需独立资格，不能预报44%整机内存或FPS收益。
