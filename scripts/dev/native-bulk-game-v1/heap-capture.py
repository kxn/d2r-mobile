"""Own the official heapprofd helper; no allocator hook, sampler or GPU timer."""
from pathlib import Path
import csv
import hashlib
import io
import json
import os
import re
import selectors
import shlex
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
OFFICIAL = HERE / 'official'
R = Path('/home/dl/d2r-mobile')
PREFIX = 'DM_HEAP_COLLECTOR_OWNERSHIP '
READY = 'Profiling active. Press Ctrl+C to terminate.'


def proof(p, pinned, read, require):
    cfg = p['heap_profile']
    require(set(cfg) == {'tools_ready', 'runtime_symbols', 'driver_symbols', 'mode'}, 'heap fields differ')
    require(cfg['mode'] in ('control-only', 'pair'), 'heap mode differs')
    ready = read(pinned(cfg['tools_ready']))
    require(ready['status'] == 'OFFICIAL_TOOLS_CONFIG_READY_Q_FUNCTIONAL_PROFILE_NOT_YET_PROVED', 'heap tool preparation differs')
    tools = {Path(row['path']).name: str(pinned(row)) for row in ready['tools']}
    require({'heap_profile', 'trace_processor', 'traceconv-58042408e6cc861f'} <= tools.keys(), 'missing official tools')
    upstream = OFFICIAL / 'heap_profile.upstream.py'
    helper = OFFICIAL / 'heap_profile.ownership.py'
    provenance = read(OFFICIAL / 'provenance.json')
    require(hashlib.sha256(upstream.read_bytes()).hexdigest() == provenance['upstream_sha256'] == '07e79429d06f34fbabc01d7621a3718141f3e21c5c42e4d20ee00c8c5b614d22', 'upstream changed')
    require(hashlib.sha256(helper.read_bytes()).hexdigest() == provenance['ownership_helper_sha256'], 'ownership helper changed')
    line = '  print(\'DM_HEAP_COLLECTOR_OWNERSHIP {"pid":%d,"trace_path":"%s"}\' % (perfetto_pid, profile_device_path), flush=True)\n'
    require(helper.read_text().count(line) == 1 and helper.read_text().replace(line, '') == upstream.read_text(), 'more than metadata-only official change')
    runtime = pinned(cfg['runtime_symbols'])
    driver = pinned(cfg['driver_symbols'])
    require(cfg['runtime_symbols']['sha256'] == p['new_elf'] and cfg['driver_symbols']['sha256'] == p['driver']['sha256'], 'heap symbols differ from runtime/driver')
    cfg['helper'] = str(helper)
    cfg['traceconv'] = tools['traceconv-58042408e6cc861f']
    cfg['trace_processor'] = tools['trace_processor']
    cfg['symbol_paths'] = [str(runtime.parent), str(driver.parent)]
    command = capture_command(cfg, 'PRINT_ONLY_EMPTY_DIR')
    printed = subprocess.run(command[:-2] + ['--print-config'], check=True, capture_output=True, text=True).stdout
    require('no_running: true' in printed and 'block_client: true' not in printed and 'sampling_interval_bytes: 65536' in printed, 'official effective config changed')
    cfg['effective_config'] = printed
    return {str(q): hashlib.sha256(q.read_bytes()).hexdigest() for q in [Path(__file__), *OFFICIAL.iterdir(), *HERE.glob('heap-*.sql')] if q.is_file()}


def capture_command(cfg, output):
    return [sys.executable, cfg['helper'], 'android', '-n', 'com.d2rmobile.nativeport',
            '--no-running', '--no-block-client', '--shmem-size', '8388608', '-i', '65536',
            '-c', '5000', '-d', '180000', '--no-android-tree-symbolization',
            '--traceconv-binary', cfg['traceconv'], '-o', str(output)]


def read_csv(text):
    return list(csv.DictReader(io.StringIO(text)))


def assess(health, snapshots, textproto, pid):
    """Gate official output, not a replacement measurement or trace parser."""
    reasons = []
    for row in health:
        if row['value'] not in ('0', '[NULL]') and (row['severity'] == 'error' or row['name'] == 'heapprofd_client_disconnected'):
            reasons.append('official-health:' + row['name'] + '=' + row['value'])
    target = [r for r in snapshots if r['pid'] == str(pid) and r['heap_name'] == 'libc.malloc']
    if len({r['ts'] for r in target}) < 2:
        reasons.append('fewer-than-two-target-dumps')
    if {r['pid'] for r in snapshots} != {str(pid)}:
        reasons.append('empty-or-other-target-PID')
    # Indentation and field values belong to official convert-text output.
    # Only inspect small ProcessHeapSamples headers, not sampled payloads.
    headers = re.findall(r'(?m)^(\s*)process_dumps \{\n(.*?)^\1\}', textproto, re.S)
    matching = [body.split('      samples {', 1)[0] for _, body in headers
                if re.search(r'^\s*pid: ' + re.escape(str(pid)) + r'\s*$', body, re.M)]
    if not matching or not any(re.search(r'^\s*from_startup: true\s*$', b, re.M) for b in matching):
        reasons.append('startup-not-confirmed-by-official-proto')
    for body in matching:
        if re.search(r'^\s*(disconnected|buffer_overran|buffer_corrupted|hit_guardrail|rejected_concurrent): true\s*$', body, re.M):
            reasons.append('official-process-dump-error')
        errors = re.findall(r'^\s*client_error: (\S+)', body, re.M)
        if any(v not in ('CLIENT_ERROR_NONE', '0') for v in errors):
            reasons.append('official-client-error:' + ','.join(errors))
    return {'accepted': not reasons, 'reasons': sorted(set(reasons)), 'target_dump_count': len({r['ts'] for r in target})}


class Capture:
    def __init__(self, dev, out, cfg, journal):
        self.dev, self.out, self.cfg, self.journal = dev, Path(out), cfg, journal
        self.process = None
        self.collector = None
        self.identity = None
        self.selector = selectors.DefaultSelector()
        self.pending = ''
        self.ready = False
        self.state = {'accepted': False, 'stage': 'prepared', 'cleanup_confirmed': False, 'errors': []}
        self.finished = False

    def save(self):
        self.journal(dict(self.state))

    def _line(self, line):
        if line == READY:
            self.ready = True
        if line.startswith(PREFIX):
            if self.collector is not None:
                raise RuntimeError('duplicate official collector receipt')
            receipt = json.loads(line[len(PREFIX):])
            if set(receipt) != {'pid', 'trace_path'} or not isinstance(receipt['pid'], int) or receipt['pid'] <= 1 or not re.fullmatch('/data/misc/perfetto-traces/profile-[a-f0-9]{6}', receipt['trace_path']):
                raise RuntimeError('invalid official collector receipt')
            self.collector = receipt
            self.state['collector'] = receipt
            self.save()

    def _pump(self, timeout=.2):
        for key, _ in self.selector.select(timeout):
            data = os.read(key.fd, 65536)
            if not data:
                self.selector.unregister(key.fileobj)
                continue
            text = data.decode('utf-8', errors='replace')
            self.log.write(text); self.log.flush()
            self.pending += text
            while '\n' in self.pending:
                line, self.pending = self.pending.split('\n', 1)
                self._line(line.rstrip('\r'))

    def _identity(self):
        pid = str(self.collector['pid'])
        exists = self.dev.adb('shell', shlex.join(['sh', '-c', 'if test -d "$1"; then echo PRESENT; fi', 'sh', '/proc/' + pid]))
        if not exists:
            return None
        status = self.dev.adb('shell', 'cat', '/proc/' + pid + '/status')
        args = self.dev.adb('shell', 'cat', '/proc/' + pid + '/cmdline').rstrip('\x00').split('\x00')
        stat = self.dev.adb('shell', 'cat', '/proc/' + pid + '/stat')
        exe = self.dev.adb('shell', 'readlink', '/proc/' + pid + '/exe')
        uid = re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', status, re.M)
        expected = ['--txt', '-c', '-', '-o', self.collector['trace_path'], '-d']
        if not uid or uid.groups() != ('2000',) * 4 or not args or args[0] not in ('perfetto', '/system/bin/perfetto') or args[1:] != expected or exe != '/system/bin/perfetto':
            raise RuntimeError('unknown/replacement collector identity; no signal')
        return {'pid': int(pid), 'UID': 2000, 'cmdline': args, 'exe': exe,
                'starttime': stat.rsplit(')', 1)[1].split()[19]}

    def start(self):
        self.out.mkdir()
        self.output = self.out / 'official-profile'
        self.output.mkdir()
        self.log = (self.out / 'official-helper.log').open('w')
        (self.out / 'config.pbtxt').write_text(self.cfg['effective_config'])
        command = capture_command(self.cfg, self.output)
        env = os.environ.copy()
        env.update(ANDROID_SERIAL='10CG570J59001U1', PYTHONUNBUFFERED='1',
                   PERFETTO_BINARY_PATH=os.pathsep.join(self.cfg['symbol_paths']))
        env['PATH'] = str(R / '.local/android-sdk/platform-tools') + os.pathsep + env.get('PATH', '')
        self.state.update(stage='arming-official-helper', command=command,
                          environment={k: env[k] for k in ('ANDROID_SERIAL', 'PYTHONUNBUFFERED', 'PERFETTO_BINARY_PATH')})
        self.save()
        try:
            self.process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
            self.state['host_helper_PID'] = self.process.pid; self.save()
            self.selector.register(self.process.stdout, selectors.EVENT_READ)
            deadline = time.monotonic() + 35
            while not (self.ready and self.collector):
                self._pump()
                if self.process.poll() is not None:
                    raise RuntimeError('official helper exited before readiness')
                if time.monotonic() >= deadline:
                    raise RuntimeError('official readiness deadline')
            self.identity = self._identity()
            if self.identity is None:
                raise RuntimeError('collector absent before original launcher')
            self.state.update(stage='official-startup-ready', first_collector_identity=self.identity)
            self.save()
            return True
        except BaseException as error:
            self.state['errors'].append(repr(error)); self.save()
            self.finish(None)
            return False

    def _wait_helper(self, seconds):
        deadline = time.monotonic() + seconds
        while self.process.poll() is None and time.monotonic() < deadline:
            self._pump()
        self._pump(0)
        return self.process.poll() is not None

    def finish(self, pid):
        if self.finished and self.state['cleanup_confirmed']:
            return self.state
        self.finished = True
        try:
            if self.process is not None and self.process.poll() is None:
                self._pump(0)  # A late official receipt can complete earlier uncertain cleanup.
            if self.process is not None and self.process.poll() is None:
                # Original SIGINT handler owns remote stop, pull and cleanup.
                if self.collector is None:
                    raise RuntimeError('no collector receipt; do not interrupt unknown helper initialization')
                now = self._identity()
                if now is not None and self.identity is None:
                    self.identity = now
                    self.state['first_collector_identity'] = now
                    self.state['late_receipt_cleanup_only'] = True
                    self.save()
                if now is not None and now != self.identity:
                    raise RuntimeError('unknown/replacement collector; do not trigger official helper signal forwarding')
                if not self.state.get('helper_SIGINT_requested'):
                    self.process.send_signal(signal.SIGINT)
                    self.state['helper_SIGINT_requested'] = True
                    self.save()
                if not self._wait_helper(20 if not self.state.get('cleanup_bound_reached') else 1):
                    self.state['cleanup_bound_reached'] = True
                    self.state['errors'].append('official-helper-still-running-after-initial-cleanup-bound')
                    if self.collector:
                        now = self._identity()
                        if now is not None:
                            if self.identity is None or now != self.identity:
                                raise RuntimeError('unknown/replacement collector; no fallback signal')
                            # Exact UID/cmdline/path/starttime above; never killall.
                            if not self.state.get('exact_owned_collector_INT_fallback'):
                                self.dev.adb('shell', 'kill', '-INT', str(now['pid']))
                                self.state['exact_owned_collector_INT_fallback'] = True; self.save()
                    self._wait_helper(5)
            if self.process is not None:
                self.state['helper_returncode'] = self.process.poll()
            remote_gone = self.collector is not None and self._identity() is None
            self.state['collector_absent'] = remote_gone
            helper_done = self.process is None or self.process.poll() is not None
            if self.collector and helper_done:
                present = self.dev.adb('shell', shlex.join(['sh', '-c', 'if test -e "$1"; then echo PRESENT; fi', 'sh', self.collector['trace_path']]))
                self.state['remote_trace_absent'] = not present
            self.state['cleanup_confirmed'] = helper_done and remote_gone and self.state.get('remote_trace_absent', self.collector is None)
            if helper_done and pid and self.state['cleanup_confirmed'] and (self.output / 'raw-trace').is_file():
                if self.process.poll() != 0:
                    self.state['errors'].append('official-helper-returncode=' + str(self.process.poll()))
                self.analyze(pid)
            else:
                self.state['reasons'] = ['helper-or-collector-not-complete-or-no-owned-game-PID']
        except BaseException as error:
            self.state['errors'].append(repr(error))
        finally:
            self.state['stage'] = 'profile-accepted' if self.state.get('accepted') else 'profile-invalid-preserved'
            self.save()
            if self.process is None or self.process.poll() is not None:
                self.selector.close()
                if hasattr(self, 'log'): self.log.close()
        return self.state

    def analyze(self, pid):
        raw = self.output / 'raw-trace'
        if not raw.is_file():
            raise RuntimeError('official raw trace missing')
        self.state['raw_trace_sha256'] = hashlib.sha256(raw.read_bytes()).hexdigest()
        analysis_trace = self.output / 'symbolized-trace'
        if not analysis_trace.is_file():
            analysis_trace = raw
        self.state['analysis_trace'] = str(analysis_trace)
        self.state['analysis_trace_sha256'] = hashlib.sha256(analysis_trace.read_bytes()).hexdigest()
        rows = {}
        for name in ('health', 'snapshots', 'latest-callstacks', 'callchain'):
            command = [self.cfg['trace_processor'], 'query', '-f', str(HERE / ('heap-' + name + '.sql')), str(analysis_trace)]
            r = subprocess.run(command, capture_output=True, text=True, timeout=45)
            (self.out / (name + '.csv')).write_text(r.stdout)
            (self.out / (name + '.stderr')).write_text(r.stderr)
            if r.returncode:
                raise RuntimeError('official trace processor query failed: ' + name)
            rows[name] = read_csv(r.stdout)
        r = subprocess.run([self.cfg['trace_processor'], 'convert', 'text', str(raw)], capture_output=True, text=True, timeout=45)
        (self.out / 'official-textproto.txt').write_text(r.stdout)
        (self.out / 'official-textproto.stderr').write_text(r.stderr)
        if r.returncode:
            raise RuntimeError('official proto conversion failed')
        self.state.update(assess(rows['health'], rows['snapshots'], r.stdout, pid))
        if self.state['errors']:
            self.state['accepted'] = False
            self.state['reasons'].append('official-helper-control-errors')
        self.state['target_PID'] = int(pid)
