// Real kernel descriptor concurrency; Android builds additionally use the real
// public Bionic fdsan implementation. No mock fdsan, profiler or game modules.
#include "../stderr_redirect.h"
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define REQUIRE(x) do {if(!(x)) {dprintf(STDOUT_FILENO,"FAIL line=%d %s errno=%d\n",__LINE__,#x,errno);_exit(1);}}while(0)
static char root[4096];
static void path(char out[4096],const char *leaf) {
  REQUIRE(snprintf(out,4096,"%s/%s",root,leaf)<4096);
}
static void initial_stderr(void) {
  char name[4096];path(name,"initial");
  int fd=open(name,O_RDWR|O_CREAT|O_TRUNC|O_CLOEXEC,0600);REQUIRE(fd>=0);
  REQUIRE(dup2(fd,STDERR_FILENO)==STDERR_FILENO);if(fd!=STDERR_FILENO)REQUIRE(!close(fd));
}
static void exact_file(const char *name,const char *expected) {
  int fd=open(name,O_RDONLY|O_CLOEXEC);REQUIRE(fd>=0);char bytes[256];
  ssize_t n=read(fd,bytes,sizeof bytes);REQUIRE(n==(ssize_t)strlen(expected));
  REQUIRE(!memcmp(bytes,expected,(size_t)n));REQUIRE(!close(fd));
}
static int descriptor_count(void) {
  DIR *dir=opendir("/proc/self/fd");REQUIRE(dir);int count=0;
  for(struct dirent *e;(e=readdir(dir));)if(e->d_name[0]!='.')count++;
  REQUIRE(!closedir(dir));return count;
}
static void same_target(const struct stat *before) {
  struct stat after;REQUIRE(!fstat(STDERR_FILENO,&after));
  REQUIRE(before->st_dev==after.st_dev && before->st_ino==after.st_ino);
}
static void success(void) {
  initial_stderr();char name[4096];path(name,"native.log");
  int fd=open(name,O_WRONLY|O_CREAT|O_TRUNC,0600);REQUIRE(fd>=0);
  REQUIRE(write(fd,"stale suffix",12)==12);REQUIRE(!close(fd));
  int count=descriptor_count();REQUIRE(!dm_android_redirect_stderr(name));
  REQUIRE(fileno(stderr)==STDERR_FILENO && descriptor_count()==count);
  REQUIRE(fputs("new",stderr)>=0);exact_file(name,"new"); // No fflush: unbuffered.
  REQUIRE(!dm_android_redirect_stderr(name));REQUIRE(fputs("next",stderr)>=0);
  exact_file(name,"next");REQUIRE(descriptor_count()==count);
}
static void failure_preserves_descriptor(void) {
  initial_stderr();struct stat before;REQUIRE(!fstat(STDERR_FILENO,&before));
  char bad[4096];path(bad,"absent-parent/native.log");int count=descriptor_count();
  REQUIRE(dm_android_redirect_stderr(bad)==ENOENT);same_target(&before);
  REQUIRE(descriptor_count()==count);
  REQUIRE(dm_android_redirect_stderr(NULL)==EINVAL);same_target(&before);
}
static void previous_stdio_error_is_cleared(void) {
  initial_stderr();char initial[4096];path(initial,"initial");
  int fd=open(initial,O_RDONLY|O_CLOEXEC);REQUIRE(fd>=0);
  REQUIRE(dup2(fd,STDERR_FILENO)==STDERR_FILENO);REQUIRE(!close(fd));
  REQUIRE(!setvbuf(stderr,NULL,_IONBF,0));
  REQUIRE(fputc('x',stderr)==EOF && ferror(stderr));
  char name[4096];path(name,"after-error.log");
  REQUIRE(!dm_android_redirect_stderr(name));REQUIRE(!ferror(stderr));
  REQUIRE(fputs("recovered",stderr)>=0);exact_file(name,"recovered");
}
static void closed_descriptor_is_refused(void) {
  initial_stderr();REQUIRE(!close(STDERR_FILENO));char name[4096];path(name,"must-not-create");
  REQUIRE(dm_android_redirect_stderr(name)==EBADF);REQUIRE(access(name,F_OK)<0 && errno==ENOENT);
}
static void different_FILE_is_refused(void) {
  initial_stderr();FILE *saved=stderr;char name[4096];path(name,"other-FILE");
  FILE *other=fopen(name,"w");REQUIRE(other && fileno(other)!=STDERR_FILENO);
  stderr=other;char target[4096];path(target,"must-not-create-other");
  REQUIRE(dm_android_redirect_stderr(target)==EBADF);
  stderr=saved;REQUIRE(!fclose(other));REQUIRE(access(target,F_OK)<0 && errno==ENOENT);
}
#ifdef __BIONIC__
static void real_owner_is_preserved(void) {
  initial_stderr();struct stat before;REQUIRE(!fstat(STDERR_FILENO,&before));
  int owner;uint64_t tag=android_fdsan_create_owner_tag(ANDROID_FDSAN_OWNER_TYPE_UNIQUE_FD,(uintptr_t)&owner);
  // This test becomes the real fdsan owner, then legitimately releases its own
  // exact tag after the helper refuses it. The helper never changes any tag.
  REQUIRE(!android_fdsan_get_owner_tag(STDERR_FILENO));
  android_fdsan_exchange_owner_tag(STDERR_FILENO,0,tag);
  char name[4096];path(name,"must-not-create-owned");
  REQUIRE(dm_android_redirect_stderr(name)==EBUSY);
  REQUIRE(android_fdsan_get_owner_tag(STDERR_FILENO)==tag);same_target(&before);
  REQUIRE(access(name,F_OK)<0 && errno==ENOENT);
  android_fdsan_exchange_owner_tag(STDERR_FILENO,tag,0); // Known test-owner release.
}
#endif
static _Atomic int stop_workers,acquired2,worker_error,worker_opens;
static void *descriptor_worker(void *unused) {
  (void)unused;
#ifdef __BIONIC__
  int owner;uint64_t tag=android_fdsan_create_owner_tag(ANDROID_FDSAN_OWNER_TYPE_UNIQUE_FD,(uintptr_t)&owner);
#endif
  while(!atomic_load(&stop_workers)) {
    int fd=open("/dev/null",O_RDWR|O_CLOEXEC);
    if(fd<0) {atomic_store(&worker_error,errno);break;}
    atomic_fetch_add(&worker_opens,1);
    if(fd==STDERR_FILENO)atomic_fetch_add(&acquired2,1);
#ifdef __BIONIC__
    android_fdsan_exchange_owner_tag(fd,0,tag);
    sched_yield();
    if(android_fdsan_close_with_tag(fd,tag))atomic_store(&worker_error,errno);
#else
    sched_yield();if(close(fd))atomic_store(&worker_error,errno);
#endif
  }
  return NULL;
}
static void descriptor_race(void) {
  initial_stderr();int count=descriptor_count();pthread_t workers[4];
  for(unsigned i=0;i<4;i++)REQUIRE(!pthread_create(&workers[i],NULL,descriptor_worker,NULL));
  char name[4096];path(name,"race-native.log");
  for(unsigned i=0;i<4000;i++)REQUIRE(!dm_android_redirect_stderr(name));
  atomic_store(&stop_workers,1);
  for(unsigned i=0;i<4;i++)REQUIRE(!pthread_join(workers[i],NULL));
  REQUIRE(!atomic_load(&worker_error) && !atomic_load(&acquired2));
  REQUIRE(atomic_load(&worker_opens)>0);
  REQUIRE(descriptor_count()==count);REQUIRE(fputs("race-end",stderr)>=0);exact_file(name,"race-end");
}
int main(int argc,char **argv) {
  const char *temporary=argc==2?argv[1]:getenv("TMPDIR");if(!temporary)temporary="/tmp";
  int length=snprintf(root,sizeof root,"%s/dm-stderr-XXXXXX",temporary);
  REQUIRE(length>0 && (size_t)length<sizeof root);REQUIRE(mkdtemp(root));
  struct {const char *name;void(*fn)(void);}cases[]={
    {"truncate-unbuffered-no-leak",success},{"open-failure-preserves-old",failure_preserves_descriptor},
    {"previous-stdio-error-cleared",previous_stdio_error_is_cleared},
    {"closed-stderr-refused",closed_descriptor_is_refused},{"different-FILE-refused",different_FILE_is_refused},
#ifdef __BIONIC__
    {"real-fdsan-owner-preserved",real_owner_is_preserved},
#endif
    {"4000-redirect-four-openers",descriptor_race}};
#ifndef __BIONIC__
  puts("SKIP real-fdsan-owner-preserved: host libc is not Bionic");fflush(stdout);
#endif
  for(unsigned i=0;i<sizeof cases/sizeof *cases;i++) {
    pid_t child=fork();REQUIRE(child>=0);if(!child) {cases[i].fn();_exit(0);}
    int status;REQUIRE(waitpid(child,&status,0)==child);REQUIRE(WIFEXITED(status) && !WEXITSTATUS(status));
    printf("PASS %s\n",cases[i].name);fflush(stdout);
  }
  printf("RESULT game_modules_executed=false actual_Bionic_fdsan=%s no_fdsan_level_or_unknown_tag_override=true temp=%s\n",
#ifdef __BIONIC__
    "true",
#else
    "false",
#endif
    root);
  return 0;
}
