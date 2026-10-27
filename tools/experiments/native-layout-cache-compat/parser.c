/* Exercise the real production parser; identity is supplied explicitly. */
#include "nvn_native_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *read_file(const char *path,size_t *length) {
  FILE *f=fopen(path,"rb");if(!f)return NULL;
  if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);if(n<=0){fclose(f);return NULL;}rewind(f);
  void *p=malloc((size_t)n);if(!p){fclose(f);return NULL;}
  if(fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}fclose(f);*length=(size_t)n;return p;
}
int main(int argc,char **argv) {
  if(argc!=5)return 2;
  size_t bytes=0,identity_bytes=0,pack_bytes=0;
  void *table=read_file(argv[1],&bytes),*identity=read_file(argv[2],&identity_bytes),*pack=read_file(argv[3],&pack_bytes);
  if(!table||!identity||!pack||identity_bytes!=sizeof(nvn_native_identity)||pack_bytes!=32)return 2;
  int actual=nvn_native_table_install(table,bytes,pack,identity),expected=atoi(argv[4]);
  printf("{\"production_parser_accept\":%d,\"expected\":%d,\"table_bytes\":%zu,\"GPU_executed\":false}\n",actual,expected,bytes);
  free(table);free(identity);free(pack);return actual==expected?0:1;
}
