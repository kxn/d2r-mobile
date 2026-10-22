#include <unistd.h>
volatile int observed;
__attribute__((noinline)) void stop_here(int value) { observed = value; }
__attribute__((noinline)) void unrelated(void) { observed = -1; }
int main(int argc,char **argv) {
 if (argc>1 && argv[1][0]=='d') sleep(2);
 for (int i=0;i<200;i++) stop_here(i);
 if (argc>1) unrelated();
 stop_here(200);
 sleep(1);
 return 0;
}
