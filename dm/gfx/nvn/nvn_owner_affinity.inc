// Startup-only diagnostic for the existing translation owner. No draw hook.
#include <sched.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int nvn_owner_cpu = -1;
static int nvn_owner_cpu_parse(const char *text, int *cpu) {
    if (!text || !strcmp(text,"-1")) { *cpu=-1; return 0; }
    if (!*text) return -1;
    unsigned value=0;
    for (const char *p=text; *p; ++p) {
        if (*p<'0' || *p>'9' || value>=1024) return -1;
        value=value*10+(unsigned)(*p-'0');
    }
    if (value>=1024 || value>=CPU_SETSIZE) return -1;
    *cpu=(int)value; return 0;
}
static void nvn_owner_mask_text(const cpu_set_t *set, char out[CPU_SETSIZE/4+1]) {
    static const char hex[]="0123456789abcdef";
    for (unsigned n=0;n<CPU_SETSIZE/4;n++) {
        unsigned bits=0,first=CPU_SETSIZE-4*(n+1);
        for(unsigned b=0;b<4;b++) if(CPU_ISSET(first+b,set)) bits|=1u<<b;
        out[n]=hex[bits];
    }
    out[CPU_SETSIZE/4]=0;
}
// Return an explicit failure, never continue an alleged pinned run on failure.
// Requested CPU must be in this thread's original allowed mask. The scheduler
// may subsequently change policy; this is startup readback, not a lasting claim.
static const char *nvn_owner_affinity_apply(int cpu, cpu_set_t *before,
                                         cpu_set_t *after, int *error) {
    *error=0;
    if(cpu==-1) return NULL; // default: no sched_get/setaffinity calls
    if(cpu < -1 || cpu>=1024 || cpu>=CPU_SETSIZE) return "invalid-cpu";
    if(sched_getaffinity(0,sizeof(*before),before)) { *error=errno;return "get-original"; }
    if(!CPU_ISSET(cpu,before)) return "outside-original-mask";
    cpu_set_t selected;CPU_ZERO(&selected);CPU_SET(cpu,&selected);
    if(sched_setaffinity(0,sizeof(selected),&selected)) { *error=errno;return "set-self"; }
    if(sched_getaffinity(0,sizeof(*after),after)) { *error=errno;return "get-readback"; }
    if(!CPU_EQUAL(&selected,after)) return "readback-mismatch";
    return NULL;
}
