# 当前真实提交：资源准备复用覆盖与预算

2026-10-28。一个完整营地提交证明了大量状态重复，但不能把命中百分比直接
乘整个资源准备预算。当前优先实现的是现有 Render Job 的兼容证明前移；
Store 整列表缓存的适用工作量较小，暂不新增其管理层。

## 原始资格

真实 ARM64 游戏，Q735，APK f3576a49…、host26a104a8…、源码6283988，
cf83 Shader／12b8 Turnip／原BC3资源，game与swapchain1968×900、3D1673×765。
原工具为官方NDK LLDB，只做一次完整计划的只读元数据快照，不是性能采样。

首次函数断点虽命中正确owner，却处于replay20721，已严格拒绝。根据精确ELF
官方反汇编，后续使用x_run内file0x3895dc的单次停点：两个规划返回路径在
这里汇合，root-loop回边只回3895e0。实际replay0、valid/store_valid/index均
有效、descriptor epoch一致，x24/w23的首handle与plan[0]相同，22266条
展开后的原命令完整。没有修改目标变量、执行表达式或放宽断言。

读取时保存的4KiB页经SHA核准，后来证明完整覆盖全部命令payload；复用
这些原字节与精确ELF DWARF，不为新增问题再附加或补读设备。

## Store准备：大量空列表命中不能冒充大量省工

本提交2421 draw、10 dispatch，4852个有效Shader阶段访问，12023次
描述符查找。2432个Program设置中2017个同pointer/serial；2442个TEX/IMG
设置写36606槽，仅4079槽值改变，1240个设置记录完全不变。

| 离线规则 | 阶段命中 | 其中空列表 | 可避免描述符查找 |
| --- | ---: | ---: | ---: |
| 每次Program/绑定setter即失效 | 0 | 0 | 0 |
| 同Program保留，只在实际读取槽改变时失效 | 3512 | 2468 | 3940 |
| 再按相同读取schema跨Shader复用 | 3938 | 2893 | 3941 |

最后一种真实省查找仅32.78%，setter还要4011次相关槽值比较。它不能删除
每seq的domain判定、事件/未知需求和别名关系。当前官方PMU的2.3101M
Store解释预算包含这些保留工作，不能把整个预算乘阶段命中率。

另一枚更窄的逐槽bucket缓存可避10080次已有hash-cache HIT，新增3665次
setter比较及结果/valid维护；但现有相应SELF仅约0.270M指令、11个样本。
首次footprint解析约0.484M、20个样本仍需。它可作小候选，不当大突破。
VS的2421次访问均无图像读取；多出的阶段复用大量只是空列表控制流。
上述规则的8项主机边界测试验证的是离线整理，不是生产缓存验收。

## Render Job：值得前移的兼容证明

实际RT、最终DS、区域和plan角色在2357/2421个相邻draw逻辑稳定。保守
沿用完整pipeline epoch，并把任何RT/viewport setter也当失效，仍有
2000/2421（82.61%）满足必要条件。这不是物理job命中率，资源准备、query
和回调仍可能结束或切换job。

当前每draw在现有last-hit检查前重建RT匹配收条、最终DS及272B PSO
context。已有官方PC/DWARF给相关毛包络约1.546M指令；全删也仅owner
约2.18%，实际只可删其中重复部分，不据次数直接折算毫秒或FPS。

候选复用既有prepared_pipeline/context与live job，无第二个大缓存：setter、
restore和job生命周期提供失效，资源/hazard/query继续执行，其后重新确认
job、recording、representation和角色。必须处理vk_state_restore不推进旧
pipeline epoch、viewport独立变化、无版本runtime override、无setter的
plan need/writes变化、pixel-local subpass及image/query回调。未闭合路径
先排除。该候选在独立工作树实施，尚无净省工或整帧收益验收。

## 原始证据与状态

- `.local/logs/store-forward-read-reuse-capture-v2/ACTUAL-ONE-STOP1/`：真实快照、页、原工具输出和完整清理。
- 同目录上级`reuse-rule-analysis/`：逐setter/slot离线规则、原PMU预算、8项测试。
- `.local/logs/render-job-prefix-reuse-audit-v1/`：完整payload/DWARF覆盖、状态重复、失效与负控设计。
- `.local/logs/store-forward-read-reuse-device-v2/final-restoration.json`：本次入口22个用户文件原位恢复，c09原APK/偏好/接受配置一致；未冷启动用户存档，游戏与工具已停止。

快照只代表本次营地提交，不代表全游戏分布。此前Store过滤的净省工与其他
线程变化另见[正式同APK对照](ANDROID_STORE_ATTACHMENT_DOMAIN_GAME_2026-10-28.zh-CN.md)。
