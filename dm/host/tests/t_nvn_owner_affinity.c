#define _GNU_SOURCE
#include <sched.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int gets,sets,mode;
static cpu_set_t original;
static int fake_get(pid_t who,size_t size,cpu_set_t *out) {
 assert(who==0 && size==sizeof(*out));gets++;
 if((mode==1 && gets==1)||(mode==3 && gets==2)){errno=EPERM;return -1;}
 if(gets==1) *out=original;
 else {CPU_ZERO(out);CPU_SET(mode==4?4:5,out);}
 return 0;
}
static int fake_set(pid_t who,size_t size,const cpu_set_t *in) {
 assert(who==0 && size==sizeof(*in));sets++;assert(CPU_COUNT(in)==1 && CPU_ISSET(5,in));
 if(mode==2){errno=EPERM;return -1;}return 0;
}
#define sched_getaffinity fake_get
#define sched_setaffinity fake_set
#include "../../gfx/nvn/nvn_owner_affinity.inc"
int main(void) {
 const char *bad[]={""," ","-2","+5","5x","1024","999999999999999999999999999"};
 assert(!nvn_owner_cpu_parse(NULL,&nvn_owner_cpu)&&nvn_owner_cpu==-1);
 assert(!nvn_owner_cpu_parse("-1",&nvn_owner_cpu)&&nvn_owner_cpu==-1);
 assert(!nvn_owner_cpu_parse("5",&nvn_owner_cpu)&&nvn_owner_cpu==5);
 assert(!nvn_owner_cpu_parse("1023",&nvn_owner_cpu)&&nvn_owner_cpu==1023);
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) assert(nvn_owner_cpu_parse(bad[i],&nvn_owner_cpu));
 cpu_set_t before,after;int error;
 assert(!nvn_owner_affinity_apply(-1,&before,&after,&error)&&!gets&&!sets);
 assert(!strcmp(nvn_owner_affinity_apply(1024,&before,&after,&error),"invalid-cpu")&&!gets&&!sets);
 CPU_ZERO(&original);CPU_SET(5,&original);CPU_SET(7,&original);
 char text[CPU_SETSIZE/4+1];nvn_owner_mask_text(&original,text);
 assert(!strcmp(text+CPU_SETSIZE/4-2,"a0"));
 for(int m=0;m<5;m++) {
  gets=sets=0;mode=m;
  const char *failure=nvn_owner_affinity_apply(5,&before,&after,&error);
  const char *expected[]={NULL,"get-original","set-self","get-readback","readback-mismatch"};
  if(expected[m])assert(failure&&!strcmp(failure,expected[m]));else assert(!failure);
  assert(gets==(m==1||m==2?1:2)&&sets==(m==1?0:1));
  assert(error==((m==1||m==2||m==3)?EPERM:0));
 }
 gets=sets=0;mode=0;CPU_CLR(5,&original);
 assert(!strcmp(nvn_owner_affinity_apply(5,&before,&after,&error),"outside-original-mask")&&gets==1&&!sets);
 puts("PASS: parser, unchanged zero calls, self-only original-mask gate, set/readback failures; mocked host APIs, NOT Android qualification");
}
