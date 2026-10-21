# Reuse immutable logical source identities

依据 actual ebe0 官方 Simpleperf（exact ELF b0c8f99c / BuildID04a0f003，621个NvN用户态样本、0lost）：`vk_texture_source_resolve` self16/621=2.576%，其直接caller全部为CPU source producer；其中8样本在ownerlogical slot epoch命中、3在lookup慢路附近。不是native admission的epoch重验已省掉，也不能将inclusive与self相加预报CPU毫秒。

旧producer每operation均把已经分配的entry.source.id清为-1，导致下一原CPU source消费必定再作logical lookup和144B tex_t副本，即使实际ID与全局descriptor epoch没变。此候选仅在scratch增长时初始化新增尾部id=-1，保留已拥有的值；每次CPU source/admission依旧从实际S读取handle，再经原`vk_texture_source_resolve`校验ID+原全局descriptor epoch，变化时原lookup重建。没有新增表、hash、cache slot或生产内存。原200B logical cache、861184B最大source scratch均不变。

ID与epoch证明的只有注册值不变，不证明CPU/GPU数据ready。每次CPU preflight、dirty/page/native/alias/promotion、admission pin、prepare、view、hazard和GPUlast_use仍执行。storage/complex/nativeOFF/深度溢出/OOM条件不变。注册、limit变化和逻辑retirement仍由原受锁publisher推进checked不回绕epoch，旧value未经重验不会消费。相邻operation换shader/stage/schema/寄存器只改变新的actualhandle索引，不授予旧逻辑值权威。

原强制lookup路径即便ownerlogical缓存命中也要TLS/table/hash/ID/epoch判定及144B复制；新命中仅已有value的ID/epoch/valid判定（CPU producer增加原本短路掉的epoch读，但替代lookup内同epoch读和后续copy）。发生ID/epoch变化仍原路径，工作不减少；不以16个样本作为可全部回收的毫秒承诺。

验证为host软件GPU：normal与ASan/UBSan各4个Core/SYNC原功能用例，覆盖真实graphics/compute/ghost/promotion、FS/CS/TRANSFER、CPU同值/noFlush及nativeImage→Buffer GPU payload，ordinaryOFF原路；新功能oracle核相邻相同值不refresh、count缩小/增长/新增尾部、换shader/stage/id、register/limit invalidation。refresh观测仅EMBED&&!APP，不是APP性能counter。原f830 table1216B/payload94208B经原Builder输出非空collector metadata仍验证，未冒称LVP ASTC执行。两必要变异：恢复每op清id须被新重复工作oracle拒绝；去epoch须被注册失效oracle拒绝。未访问设备、未声称Android性能提高。

一次host编排在ASan二进制尚未完成链接时提前进入该组，产生FileNotFoundError；原4个normal成功记录保留。随后等待同一ASan构建完成，只补其4组，不重复normal。该工具错误不是运行时失败，最终汇总引用两份真实输出与各自binary SHA。
