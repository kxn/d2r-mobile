// Standalone actual-SDK oracle uses the APK's shared-library TLSDESC model.
// It never starts nnMain, a game world, graphics, or a device input loop.
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: native_opus_sdk_runner SHARED_ORACLE VERIFIED_MODULE_DIR [sdk|native]\n");
        return 2;
    }
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    int (*entry)(int, char **) = (int (*)(int, char **))dlsym(library, "dm_opus_sdk_fixture_main");
    if (!entry) { fprintf(stderr, "dlsym: %s\n", dlerror()); return 1; }
    char *args[] = {"native_opus_sdk_fixture", argv[2], argc==4?argv[3]:NULL, NULL};
    // The fixture owns loader/thread state for the process lifetime; do not
    // dlclose a runtime with registered SDK/TLS callbacks after a returned error.
    return entry(argc-1, args);
}
