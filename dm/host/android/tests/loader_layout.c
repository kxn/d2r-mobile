// Exercise the real loader's layout parser and immutable key-page installation.
// No guest module is loaded or executed. The only stubs are the native gate
// address and TLS descriptor address, whose values the loader must preserve.
#ifndef DM_LOADER_LAYOUT_SOURCE
#define DM_LOADER_LAYOUT_SOURCE "../../loader.c"
#endif
#include DM_LOADER_LAYOUT_SOURCE
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>

uintptr_t dm_tsd_descriptor(void) { return UINT64_C(0x1234567812345678); }
void dm_gate(void) {}

static char directory[] = "/data/local/tmp/dm-loader-layout-XXXXXX";
static unsigned checks;
static void expected_abort(int signal) { (void)signal; _exit(99); }
static void expected_readonly_fault(int signal) { (void)signal; _exit(100); }
static void require(int ok, const char *name) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", name); exit(1); }
}

static void layout(unsigned long abi, int gate, int bad_key) {
    char path[1024]; snprintf(path, sizeof path, "%s/fixture.fixups.tsv", directory);
    FILE *f = fopen(path, "w"); require(f != NULL, "create layout");
    fprintf(f, "# HOST_ABI\t%lx\n# PATCH_OFF\t8000\n# KEY_PAGE_OFF\t4000\n# KEY_PAGE_SIZE\t4000\n", abi);
    if (gate >= 0) fprintf(f, "# HOST_GATE_IDX\t%x\n", gate);
    const char *names[] = {"K18", "KTLS", "KTLR", "KGATE", "KRES0", "KRES1", "KASPECT", "KASPECTUI"};
    for (unsigned i = 0; i < 8; i++) {
        unsigned index = i;
        if (i == 7 && bad_key == 1) index = 0;
        if (i == 7 && bad_key == 2) index = 2048;
        if (i == 7 && bad_key == 3) index = 8;
        fprintf(f, "# KEY_%s_IDX\t%u\n", names[i], index);
    }
    require(fclose(f) == 0, "close layout");
}

static module_t parse(void) {
    module_t m = {0}; strcpy(m.name, "fixture"); read_layout(&m, directory); return m;
}

static void rejects(unsigned long abi, int gate, int bad_key, const char *name) {
    layout(abi, gate, bad_key);
    pid_t child = fork(); require(child >= 0, "fork reject");
    if (!child) { signal(SIGABRT, expected_abort); parse(); _exit(0); }
    int status; require(waitpid(child, &status, 0) == child, "wait reject");
    require(WIFEXITED(status) && WEXITSTATUS(status) == 99, name);
}

static void accepts(unsigned abi, int gate) {
    layout(abi, gate, 0); module_t m = parse();
    require(m.host_abi == abi, "ABI retained");
    m.size = 0xc000;
    m.base = mmap(NULL, m.size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    require(m.base != MAP_FAILED, "map key page fixture");
    install_key_page(&m);
    uint64_t *page = (void *)(m.base + m.key_page_off);
    for (unsigned i = 0; i < 8; i++) require(page[m.key_idx[i]] == dm_tsd_descriptor(), "TLS descriptor unchanged");
    require(page[8] == (abi == 3 ? (uintptr_t)dm_gate : 0), "separate process gate slot");
    pid_t child = fork(); require(child >= 0, "fork read-only check");
    if (!child) {
        signal(SIGSEGV, expected_readonly_fault);
        *(volatile uint64_t *)(page + 8) = 0; _exit(0);
    }
    int status; require(waitpid(child, &status, 0) == child, "wait read-only check");
    require(WIFEXITED(status) && WEXITSTATUS(status) == 100, "key page is read-only");
    require(munmap(m.base, m.size) == 0, "unmap key page");
}

int main(void) {
    struct rlimit core = {0, 0}; setrlimit(RLIMIT_CORE, &core);
#ifndef __ANDROID__
    strcpy(directory, "/tmp/dm-loader-layout-XXXXXX");
#endif
    require(mkdtemp(directory) != NULL, "temporary directory");
    accepts(2, -1);
#ifdef TEST_OLD_LOADER
    rejects(3, 8, 0, "old loader rejects ABI3 before execution");
#else
    accepts(3, 8);
    rejects(3, -1, 0, "ABI3 requires process gate metadata");
    rejects(2, 8, 0, "legacy ABI cannot claim process gate metadata");
    rejects(3, 9, 0, "gate slot must be declared ABI3 slot8");
    rejects(3, 8, 1, "duplicate TLS slots rejected");
    rejects(3, 8, 2, "out-of-range TLS slots rejected");
    rejects(3, 8, 3, "TLS cannot overwrite process gate");
    rejects(4, -1, 0, "unknown ABI rejected");
    rejects(UINT64_C(0x100000003), 8, 0, "ABI overflow rejected");
#endif
    char path[1024]; snprintf(path, sizeof path, "%s/fixture.fixups.tsv", directory);
    require(unlink(path) == 0 && rmdir(directory) == 0, "fixture cleanup");
    printf("PASS loader layout %u checks; no guest execution\n", checks);
    return 0;
}
