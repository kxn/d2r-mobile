// 把原 APFS 快照边界接到 Linux：优先 reflink，否则复制；提交只用内核原子交换。
// Adapt the original APFS snapshot boundary to Linux: prefer reflink, otherwise
// copy; commit uses kernel atomic exchange.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>

static int copy_node(int source, int destination, const char *name,
                     unsigned depth) {
  if (depth > 64) {
    errno = ELOOP;
    return -1;
  }
  struct stat st;
  if (fstatat(source, name, &st, AT_SYMLINK_NOFOLLOW))
    return -1;
  if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
    errno = EINVAL;
    return -1;
  }
  int in = openat(source, name,
                  O_RDONLY | O_CLOEXEC | O_NOFOLLOW |
                      (S_ISDIR(st.st_mode) ? O_DIRECTORY : 0));
  if (in < 0)
    return -1;
  int out = -1, rc = -1;
  char *buffer = NULL;
  if (S_ISDIR(st.st_mode)) {
    if (mkdirat(destination, name, 0700))
      goto done;
    out = openat(destination, name,
                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (out < 0)
      goto done;
    int duplicate = dup(in);
    if (duplicate < 0)
      goto done;
    DIR *directory = fdopendir(duplicate);
    if (!directory) {
      close(duplicate);
      goto done;
    }
    struct dirent *entry;
    rc = 0;
    errno = 0;
    while ((entry = readdir(directory))) {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
        continue;
      if (copy_node(in, out, entry->d_name, depth + 1)) {
        rc = -1;
        break;
      }
      errno = 0;
    }
    if (!entry && errno)
      rc = -1;
    int saved = errno;
    closedir(directory);
    errno = saved;
    if (rc)
      goto done;
  } else {
    out = openat(destination, name,
                 O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (out < 0)
      goto done;
    if (ioctl(out, FICLONE, in)) {
      buffer = malloc(65536);
      if (!buffer) {
        errno = ENOMEM;
        goto done;
      }
      ssize_t n;
      for (;;) {
        do
          n = read(in, buffer, 65536);
        while (n < 0 && errno == EINTR);
        if (n < 0)
          goto done;
        if (!n)
          break;
        for (ssize_t at = 0; at < n;) {
          ssize_t written = write(out, buffer + at, (size_t)(n - at));
          if (written < 0 && errno == EINTR)
            continue;
          if (written <= 0) {
            if (!written)
              errno = EIO;
            goto done;
          }
          at += written;
        }
      }
    }
    // 保留原实现的客体名字元数据；其余平台元数据不参与游戏存档。
    // Preserve original guest-name metadata; other platform metadata is not
    // part of game saves.
    ssize_t n = fgetxattr(in, "user.org.d2rmobile.guest-name", NULL, 0);
    if (n >= 0) {
      void *guest_name = malloc(n ? n : 1);
      if (!guest_name) {
        errno = ENOMEM;
        goto done;
      }
      int error = 0;
      if (fgetxattr(in, "user.org.d2rmobile.guest-name", guest_name,
                    (size_t)n) != n ||
          fsetxattr(out, "user.org.d2rmobile.guest-name", guest_name, (size_t)n,
                    0))
        error = errno;
      free(guest_name);
      if (error) {
        errno = error;
        goto done;
      }
    } else if (errno != ENODATA && errno != ENOTSUP && errno != EOPNOTSUPP)
      goto done;
  }
  rc = -1;
  {
    struct timespec times[2] = {st.st_atim, st.st_mtim};
    if (fchmod(out, st.st_mode & 0777) || futimens(out, times) || fsync(out))
      goto done;
  }
  rc = 0;
done: {
  int saved = errno;
  if (out >= 0)
    close(out);
  free(buffer);
  close(in);
  errno = saved;
}
  return rc;
}
static int parent(const char *path, char *name, size_t capacity) {
  size_t n = strlen(path);
  if (!n || n >= 4096 || path[n - 1] == '/') {
    errno = EINVAL;
    return -1;
  }
  char copy[4096];
  memcpy(copy, path, n + 1);
  char *slash = strrchr(copy, '/');
  const char *leaf = slash ? slash + 1 : copy;
  if (strlen(leaf) >= capacity || !strcmp(leaf, ".") || !strcmp(leaf, "..")) {
    errno = EINVAL;
    return -1;
  }
  strcpy(name, leaf);
  if (slash) {
    if (slash == copy)
      slash[1] = 0;
    else
      *slash = 0;
  }
  return open(slash ? copy : ".",
              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}
int dm_clonefile(const char *source, const char *destination, int flags) {
  if (flags) {
    errno = EINVAL;
    return -1;
  }
  char source_name[256], dest_name[256];
  int src = parent(source, source_name, sizeof(source_name));
  if (src < 0)
    return -1;
  int dst = parent(destination, dest_name, sizeof(dest_name));
  if (dst < 0) {
    int e = errno;
    close(src);
    errno = e;
    return -1;
  }
  // 顶层可改名，递归层维持原名字。 | The root may be renamed; descendants
  // retain their names.
  struct stat st;
  int rc = -1;
  if (fstatat(src, source_name, &st, AT_SYMLINK_NOFOLLOW))
    goto done;
  if (!S_ISDIR(st.st_mode)) {
    errno = EINVAL;
    goto done;
  }
  if (mkdirat(dst, dest_name, 0700))
    goto done;
  int in =
      openat(src, source_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  int out =
      openat(dst, dest_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (in < 0 || out < 0) {
    if (in >= 0)
      close(in);
    if (out >= 0)
      close(out);
    goto done;
  }
  DIR *directory = fdopendir(in);
  if (!directory) {
    close(in);
    close(out);
    goto done;
  }
  struct dirent *entry;
  rc = 0;
  errno = 0;
  while ((entry = readdir(directory))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    if (copy_node(dirfd(directory), out, entry->d_name, 0)) {
      rc = -1;
      break;
    }
    errno = 0;
  }
  if (!entry && errno)
    rc = -1;
  if (!rc && fsync(out))
    rc = -1;
  {
    int saved = errno;
    closedir(directory);
    close(out);
    errno = saved;
  }
done: {
  int saved = errno;
  close(src);
  close(dst);
  errno = saved;
}
  return rc;
}
int dm_rename_exchange(const char *source, const char *destination,
                       unsigned flags) {
  if (flags != 2) {
    errno = EINVAL;
    return -1;
  }
  return (int)syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD, destination,
                      RENAME_EXCHANGE);
}
static const char *attribute(const char *name, char *buffer, size_t n) {
  if (strncmp(name, "org.d2rmobile.", 14))
    return name;
  if (snprintf(buffer, n, "user.%s", name) >= (int)n) {
    errno = ERANGE;
    return NULL;
  }
  return buffer;
}
ssize_t dm_getxattr(const char *path, const char *name, void *value,
                    size_t size, uint32_t position, int options) {
  if (position || (options & ~1)) {
    errno = EINVAL;
    return -1;
  }
  char buffer[256];
  name = attribute(name, buffer, sizeof(buffer));
  if (!name)
    return -1;
  return options ? lgetxattr(path, name, value, size)
                 : getxattr(path, name, value, size);
}
int dm_setxattr(const char *path, const char *name, const void *value,
                size_t size, uint32_t position, int options) {
  if (position || (options & ~1)) {
    errno = EINVAL;
    return -1;
  }
  char buffer[256];
  name = attribute(name, buffer, sizeof(buffer));
  if (!name)
    return -1;
  return options ? lsetxattr(path, name, value, size, 0)
                 : setxattr(path, name, value, size, 0);
}
int dm_removexattr(const char *path, const char *name, int options) {
  if (options & ~1) {
    errno = EINVAL;
    return -1;
  }
  char buffer[256];
  name = attribute(name, buffer, sizeof(buffer));
  if (!name)
    return -1;
  return options ? lremovexattr(path, name) : removexattr(path, name);
}
