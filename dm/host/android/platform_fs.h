// Linux 文件快照、原子互换及 xattr 适配；原存档事务逻辑保持在 hle_fs.c。
// Linux file snapshots, atomic exchange and xattr adaptation; hle_fs.c retains
// the save transaction logic.
#pragma once
#include <stdint.h>
#include <sys/types.h>
#include <sys/xattr.h>
#define XATTR_NOFOLLOW 1
#define ENOATTR ENODATA
#define RENAME_SWAP 2
int dm_clonefile(const char *source, const char *destination, int flags);
int dm_rename_exchange(const char *source, const char *destination,
                       unsigned flags);
ssize_t dm_getxattr(const char *path, const char *name, void *value,
                    size_t size, uint32_t position, int options);
int dm_setxattr(const char *path, const char *name, const void *value,
                size_t size, uint32_t position, int options);
int dm_removexattr(const char *path, const char *name, int options);
#define clonefile dm_clonefile
#define renamex_np dm_rename_exchange
#define getxattr dm_getxattr
#define setxattr dm_setxattr
#define removexattr dm_removexattr
