#pragma once
#include <stdio.h>

static inline int dm_fixture_output_open(const char *path) {
  FILE *initial=fopen(path,"w");
  if(!initial) return 90;
  if(fclose(initial)) return 90;
  // Both FILE objects need append mode: disabling buffering alone cannot
  // prevent stderr's independent offset from overwriting stdout's records.
  if(!freopen(path,"a",stderr)) return 90;
  if(!freopen(path,"a",stdout)) return 91;
  setvbuf(stderr,NULL,_IONBF,0);
  setvbuf(stdout,NULL,_IONBF,0);
  return 0;
}
