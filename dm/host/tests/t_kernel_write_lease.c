// Exercise the original ReadFile and dm_data_pread with real kernel writes.
// Only syscall outcomes/short-read boundaries are controlled; no game CPU runs.
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
static ssize_t lease_pread(int,void *,size_t,off_t);
#define pread lease_pread
#ifndef HLE_FS_SOURCE
#define HLE_FS_SOURCE "../hle_fs.c"
#endif
#include HLE_FS_SOURCE
#undef pread
struct dm_flags g_flag;
uint64_t g_t0_ns;
uint64_t now_ns(void) { return 0; }
void dm_prof_add(int i,uint64_t n,uint64_t b) {(void)i;(void)n;(void)b;}
int dm_custom_lang_explicit(void) { return -1; }
static unsigned checks,prepares,begins,ends,active,calls;
static int registered,relevant=1,mode;
static void *expected_address;
static size_t expected_bytes;
static void *protected_page;
static size_t page_bytes;
#define CHECK(c) do {checks++;if(!(c))die("kernel lease check line %u: %s",__LINE__,#c);}while(0)
void dm_guest_write_prepare(void *p,size_t n) {
    prepares++;CHECK(p==expected_address && n==expected_bytes);
    if(registered && relevant && n)CHECK(active==1);
    if(protected_page && n)CHECK(!mprotect(protected_page,page_bytes,PROT_READ|PROT_WRITE));
}
#include "../guest_write_lease.inc"
static unsigned cookie;
static void *lease_begin(void *p,size_t n) {
    begins++;CHECK(p==expected_address && n==expected_bytes);
    if(!relevant || !n)return NULL;
    CHECK(!active);active=1;return &cookie;
}
static void lease_end(void *p) {
    CHECK(p==&cookie && active==1);active=0;ends++;
    errno=ENOENT; // Must not change the actual pread error/result mapping.
}
static const dm_guest_write_lease_hooks hooks={lease_begin,lease_end};
static const dm_guest_write_lease_hooks other_hooks={lease_begin,lease_end};
static ssize_t lease_pread(int fd,void *p,size_t n,off_t off) {
    calls++;if(registered && relevant && expected_bytes)CHECK(active==1);
    if(mode==1 && calls==1){errno=EINTR;return -1;}
    if(mode==1 && n>2)n=2;
    if(mode==2 && calls==1 && n>3)n=3;
    else if(mode==2 && calls>1){errno=EINVAL;return -1;}
    return pread(fd,p,n,off);
}
static Result read_case(int fd,int64_t off,void *buffer,size_t n,size_t *got,int selected_mode) {
    expected_address=buffer;expected_bytes=n;mode=selected_mode;calls=0;
    unsigned before_prepares=prepares,before_ends=ends;
    Result result=h_fs_ReadFile(got,(uint64_t)fd,off,buffer,n);
    CHECK(prepares==before_prepares+1);CHECK(active==0);
    CHECK(ends==before_ends+(registered && relevant && n?1:0));return result;
}
int main(void) {
    struct rlimit no_core={0,0};CHECK(!setrlimit(RLIMIT_CORE,&no_core));
    char name[]="/tmp/dm-kernel-write-lease.XXXXXX";int fd=mkstemp(name);CHECK(fd>0);CHECK(!unlink(name));
    CHECK(write(fd,"abcdef",6)==6);uint8_t buffer[32];size_t got=99;
    // No hook: old prepare still runs and a real syscall returns exact bytes.
    memset(buffer,0xa5,sizeof buffer);CHECK(read_case(fd,0,buffer,6,&got,0)==RES_OK);
    CHECK(got==6 && !memcmp(buffer,"abcdef",6) && begins==0 && ends==0);
    dm_guest_write_lease_register(&hooks);dm_guest_write_lease_register(&hooks);registered=1;
    got=99;memset(buffer,0xa5,sizeof buffer);
    CHECK(read_case(fd,0,buffer,6,&got,1)==RES_OK);CHECK(calls==4 && got==6 && !memcmp(buffer,"abcdef",6));
    // EOF after a partial read releases the full requested destination lease.
    got=99;memset(buffer,0xa5,sizeof buffer);CHECK(read_case(fd,0,buffer,16,&got,0)==RES_OK);
    CHECK(got==6 && calls==2 && !memcmp(buffer,"abcdef",6));CHECK(buffer[6]==0xa5);
    got=99;CHECK(read_case(fd,999,buffer,16,&got,0)==RES_OK);CHECK(got==0 && calls==1);
    // An error after partial data preserves original output/errno semantics.
    got=99;memset(buffer,0xa5,sizeof buffer);CHECK(read_case(fd,0,buffer,16,&got,2)==RES_FAIL);
    CHECK(got==99 && calls==2 && errno==EINVAL && !memcmp(buffer,"abc",3) && buffer[3]==0xa5);
    got=99;CHECK(read_case(-1,0,buffer,16,&got,0)==RES_FAIL);CHECK(got==99 && errno==EBADF);
    got=99;CHECK(read_case(fd,0,buffer,0,&got,0)==RES_OK);CHECK(got==0 && calls==0);
    relevant=0;got=99;CHECK(read_case(fd,0,buffer,6,&got,0)==RES_OK);CHECK(got==6 && !memcmp(buffer,"abcdef",6));relevant=1;
    // Actual RO page destination: lease is acquired before original prepare
    // makes it writable; without prepare the kernel would return EFAULT.
    page_bytes=(size_t)sysconf(_SC_PAGESIZE);
    protected_page=mmap(NULL,page_bytes,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);CHECK(protected_page!=MAP_FAILED);
    CHECK(!mprotect(protected_page,page_bytes,PROT_READ));got=99;
    CHECK(read_case(fd,0,protected_page,6,&got,0)==RES_OK);CHECK(got==6 && !memcmp(protected_page,"abcdef",6));
    CHECK(!munmap(protected_page,page_bytes));protected_page=NULL;
    // Empty/ended leases are harmless; an acquired opaque cookie is released
    // once even if the caller's common error cleanup invokes end again.
    expected_address=buffer;expected_bytes=6;dm_guest_write_lease lease=dm_guest_write_begin(buffer,6);
    CHECK(active==1);dm_guest_write_end(&lease);unsigned once=ends;dm_guest_write_end(&lease);CHECK(ends==once && active==0);
    // A second independent hook pair must not replace an in-flight contract.
    pid_t child=fork();CHECK(child>=0);
    if(!child){dm_guest_write_lease_register(&other_hooks);_exit(3);}
    int status;CHECK(waitpid(child,&status,0)==child);CHECK(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
    unsigned old_begins=begins;got=99;CHECK(h_fs_ReadFile(&got,0,0,buffer,16)==RES_FAIL);CHECK(got==0 && begins==old_begins && active==0);
    CHECK(!close(fd));printf("kernel-write-lease checks=%u passed; no game modules/devices\n",checks);return 0;
}
