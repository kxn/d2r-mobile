#!/usr/bin/env python3
"""Exercise the production fixture stdio redirect; no GPU or device work."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PROGRAM = r'''
#include "fixture_output.h"
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
static void *write_stream(void *stream) {
  int marker=stream==stdout?'S':'E';
  for(unsigned i=0;i<4096;i++) if(fputc(marker,stream)==EOF) return (void *)1;
  return NULL;
}
int main(int argc,char **argv) {
  if(argc!=3) return 2;
  int error=dm_fixture_output_open(argv[2]);
  if(error) return error;
  if(!(fcntl(fileno(stdout),F_GETFL)&O_APPEND) ||
     !(fcntl(fileno(stderr),F_GETFL)&O_APPEND)) return 92;
  printf("{\"configuration\":\"host-stdio-only\"}\n");
  if(!strcmp(argv[1],"concurrent")) {
    pthread_t a,b;void *ra,*rb;
    if(pthread_create(&a,NULL,write_stream,stdout) ||
       pthread_create(&b,NULL,write_stream,stderr)) return 93;
    pthread_join(a,&ra);pthread_join(b,&rb);
    if(ra || rb) return 94;
    fputc('\n',stdout);
  } else {
    fprintf(stderr,"renderer log after configuration\n");
    fprintf(stdout,"stdout record\n");
    fprintf(stderr,"stderr record\n");
  }
  printf("{\"result\":\"stdio-passed\"}\n");
  return 0;
}
'''


class FixtureOutputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='d2r-fixture-output-')
        cls.base = Path(cls.temp.name)
        cc = shutil.which('cc')
        if not cc:
            raise unittest.SkipTest('native host C compiler required')
        source = cls.base / 'output.c'; source.write_text(PROGRAM)
        cls.executable = cls.base / 'output'
        subprocess.run([cc, '-I', str(ROOT / 'dm/host/android'), str(source),
                        '-pthread', '-o', str(cls.executable)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_interleaved_streams_preserve_first_and_final_json(self):
        path = self.base / 'sequential.log'
        path.write_text('stale prior run\n')
        subprocess.run([self.executable, 'sequential', path], check=True)
        lines = path.read_text().splitlines()
        self.assertEqual(json.loads(lines[0]), {'configuration': 'host-stdio-only'})
        self.assertEqual(lines[1:-1], ['renderer log after configuration',
                                     'stdout record', 'stderr record'])
        self.assertEqual(json.loads(lines[-1]), {'result': 'stdio-passed'})

    def test_concurrent_streams_keep_all_bytes_between_json_records(self):
        path = self.base / 'concurrent.log'
        subprocess.run([self.executable, 'concurrent', path], check=True)
        lines = path.read_text().splitlines()
        self.assertEqual(json.loads(lines[0]), {'configuration': 'host-stdio-only'})
        self.assertEqual(len(lines[1]), 8192)
        self.assertEqual(lines[1].count('S'), 4096)
        self.assertEqual(lines[1].count('E'), 4096)
        self.assertEqual(json.loads(lines[2]), {'result': 'stdio-passed'})

    def test_open_failures_preserve_error_code(self):
        for path in (self.base, self.base / 'missing-parent/file.log'):
            with self.subTest(path=path):
                result = subprocess.run([self.executable, 'sequential', path])
                self.assertEqual(result.returncode, 90)


if __name__ == '__main__':
    unittest.main()
