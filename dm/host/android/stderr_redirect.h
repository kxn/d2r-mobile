#pragma once
// Cold Android logging only. Standard stderr borrows descriptor2 and Bionic
// deliberately gives the standard FILE no fdsan owner tag. Never close2 before
// replacing it: another thread may open/reuse2 in that gap.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#ifdef __BIONIC__
#include <android/fdsan.h>
#endif

static inline int dm_android_stderr_available(FILE *stream) {
  if(stream!=stderr || fileno(stream)!=STDERR_FILENO)return EBADF;
  if(fcntl(STDERR_FILENO,F_GETFD)<0)return errno;
#ifdef __BIONIC__
  if(android_fdsan_get_owner_tag(STDERR_FILENO)!=0)return EBUSY;
#endif
  return 0;
}

// Returns0 or an errno value, without logging through a rejected descriptor.
// The FILE lock serializes stdio operations, not another component deliberately
// closing/re-tagging/dup'ing the process-wide descriptor outside that lock.
// Public fdsan has no atomic tag-check-plus-dup operation; do not claim one.
static inline int dm_android_redirect_stderr(const char *path) {
  if(!path || !*path)return EINVAL;
  FILE *stream=stderr;
  flockfile(stream);
  int error=dm_android_stderr_available(stream);
  if(error) {funlockfile(stream);return error;}
  // Match freopen("w"): flush the old stream, truncate our new log, and keep
  // the standard FILE object. Only the new descriptor belongs to this helper.
  (void)fflush(stream);
  int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
  if(fd<0) {error=errno;funlockfile(stream);return error;}
  if(fd==STDERR_FILENO) {
    // An outside component closed2 despite the entry contract. Keep this
    // newly opened2 alive rather than introducing another close2 window; the
    // caller must reject startup. No unknown tag is exchanged or cleared.
    funlockfile(stream);return EBADF;
  }
  error=dm_android_stderr_available(stream);
  if(!error) {
    int result;
    do {result=dup2(fd,STDERR_FILENO);}while(result<0 && errno==EINTR);
    if(result<0)error=errno;
  }
  // close() is not retried after EINTR: the kernel may already have released
  // this descriptor number. This closes only our independent open(), never2.
  (void)close(fd);
  if(!error) {
    clearerr(stream); // freopen also clears pre-existing EOF/error indicators.
    (void)setvbuf(stream,NULL,_IONBF,0);
  }
  funlockfile(stream);
  return error;
}
