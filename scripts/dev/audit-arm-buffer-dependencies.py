#!/usr/bin/env python3
"""Read a GFXR capture and trace declared compute-buffer ranges through copies.

This is a conservative resource audit, not a GPU timing or CPU-read detector.
Descriptor overlap is a possible dependency, not proof every byte is accessed.
Unsupported descriptor encodings fail closed instead of guessing offsets.
"""
import argparse
from collections import Counter, defaultdict
import ctypes
import hashlib
import json
from pathlib import Path
import struct


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def get(self, fmt):
        values = struct.unpack_from('<' + fmt, self.data, self.pos)
        self.pos += struct.calcsize('<' + fmt)
        return values[0] if len(values) == 1 else values

    def array(self, fmt=None):
        flags = self.get('I')
        if flags & 1:
            return [] if fmt else 0
        if flags & 64:
            self.get('Q')  # capture pointer address, not a resource handle
        count = self.get('Q')
        if count > 100000:
            raise ValueError('unreasonable array count')
        if not flags & 128:
            return [] if fmt else 0
        return [self.get(fmt) for _ in range(count)] if fmt else count

    def end(self):
        if self.pos != len(self.data):
            raise ValueError(f'unconsumed encoded bytes: {len(self.data)-self.pos}')


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def blocks(path, wanted=None):
    lz = ctypes.CDLL('liblz4.so.1')
    lz.LZ4_decompress_safe.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    if wanted is None:
        wanted = {0x1104f, 0x1105d, 0x11067, 0x11068, 0x11069, 0x1106a, 0x1106b,
                  0x1106c, 0x1106d, 0x1106e, 0x11070}
    initial = False
    with path.open('rb') as stream:
        header = stream.read(16)
        if len(header) != 16 or header[:4] != b'GFXR':
            raise ValueError('invalid GFXR header')
        options = struct.unpack_from('<I', header, 12)[0]
        if options > 1024:
            raise ValueError('unreasonable GFXR option count')
        if len(stream.read(options * 8)) != options * 8:
            raise ValueError('truncated GFXR options')
        index = 0
        while header := stream.read(12):
            index += 1
            length, kind = struct.unpack('<QI', header)
            if length > 1 << 30:
                raise ValueError('unreasonable capture block')
            payload = stream.read(length)
            if len(payload) != length:
                raise ValueError('truncated capture block')
            if kind == 2:
                initial = struct.unpack_from('<I', payload)[0] == 1
            if kind & 0x7fffffff != 4:
                continue
            call = struct.unpack_from('<I', payload)[0]
            if call not in wanted:
                continue
            data = payload[12:]
            if kind & 0x80000000:
                size = struct.unpack_from('<Q', data)[0]
                if size > 64 << 20:
                    raise ValueError('unreasonable API payload')
                out = ctypes.create_string_buffer(size)
                compressed = data[8:]
                if lz.LZ4_decompress_safe(compressed, out, len(compressed), size) != size:
                    raise ValueError('API decompression failed')
                data = out.raw
            yield index, call, Reader(data), initial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--shader-audit', type=Path, required=True,
                        help='dispatch block/pipeline/module/reflection mapping from this capture')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        parser.error('report must be new')
    audit = json.loads(args.shader_audit.read_text())
    known = {item['block']: item for item in audit['dispatches']}
    sets, bound, vertex, indices, pipelines = {}, defaultdict(dict), defaultdict(dict), {}, {}
    events, dispatches, counts = [], [], Counter()
    for block, call, r, initial in blocks(args.input):
        counts[hex(call)] += 1
        if call == 0x1104f:  # vkUpdateDescriptorSets
            r.get('Q')
            count = r.get('I')
            if r.array() != count:
                raise ValueError('descriptor-write count differs')
            for _ in range(count):
                if r.get('I') != 35 or r.get('I') != 35:
                    raise ValueError('unsupported descriptor sType/pNext')
                descriptor, binding, element, n, kind = r.get('QIIII')
                images = r.array('QQI')
                buffers = r.array('QQQ')
                texels = r.array('Q')
                values = images if kind in (0, 1, 2, 3, 10) else buffers if kind in (6, 7, 8, 9) else texels
                if len(values) != n:
                    raise ValueError('unsupported/omitted descriptor data')
                for j, value in enumerate(values):
                    sets.setdefault(descriptor, {})[binding, element+j] = (kind, value)
            if r.get('I') or r.array():
                raise ValueError('descriptor copies not supported')
        elif call == 0x1105d:
            cmd, point, pipeline = r.get('QIQ')
            pipelines[cmd, point] = pipeline
        elif call == 0x11067:
            cmd, point, _layout, first, count = r.get('QIQII')
            values = r.array('Q')
            if len(values) != count or r.get('I') or r.array('I'):
                raise ValueError('unsupported descriptor/dynamic-offset count')
            for j, value in enumerate(values):
                bound[cmd, point][first+j] = value
        elif call == 0x11069:
            cmd, first, count = r.get('QII')
            buffers, offsets = r.array('Q'), r.array('Q')
            if len(buffers) != count or len(offsets) != count:
                raise ValueError('vertex binding count differs')
            for j, value in enumerate(zip(buffers, offsets)):
                vertex[cmd][first+j] = value
        elif call == 0x11068:
            cmd, buffer, offset, kind = r.get('QQQI')
            indices[cmd] = (buffer, offset, kind)
        elif call == 0x11070:
            cmd, src, dst, count = r.get('QQQI')
            regions = r.array('QQQ')
            if len(regions) != count:
                raise ValueError('copy-region count differs')
            if not initial:
                for so, do, size in regions:
                    events.append(dict(block=block, kind='copy', cmd=cmd, src=src, src_offset=so,
                                       dst=dst, dst_offset=do, size=size))
        else:
            cmd = r.get('Q')
            compute = call == 0x1106e
            item = dict(block=block, kind='compute' if compute else 'draw', cmd=cmd, buffers=[])
            for slot, descriptor in bound[cmd, int(compute)].items():
                # This backend assigns CS=2, VS=0, FS=1, HS=3, DS=4, GS=5.
                if (compute and slot != 2) or (not compute and slot == 2):
                    continue
                for (binding, element), (kind, value) in sets.get(descriptor, {}).items():
                    if kind in (6, 7):
                        buffer, offset, size = value
                        item['buffers'].append(dict(set=slot, binding=binding, element=element,
                                                    type=kind, buffer=buffer, offset=offset, size=size))
            if compute:
                item['groups'] = r.get('III')
                if not initial:
                    source = known.get(block)
                    if (not source or source['cmd'] != cmd or list(item['groups']) != source['groups']
                            or source['pipeline'] != pipelines.get((cmd, 1))):
                        raise ValueError('shader audit does not match dispatch')
                    item['shader'] = source
                    writable = {b['binding'] for b in source.get('storage_buffers', []) if b['flags'] & 1}
                    item['writes'] = [b for b in item['buffers'] if b['binding'] in writable and b['type'] == 7]
                    dispatches.append(item)
            else:
                # Draw payloads are kept opaque; do not invent vertex byte ranges.
                item['vertex_bindings'] = list(vertex[cmd].values())
                item['index_binding'] = indices.get(cmd)
                r.pos = len(r.data)
            if not initial:
                events.append(item)
        r.end()
    if len(dispatches) != audit['dispatch_count']:
        raise ValueError('dispatch count differs from shader audit')
    results = []
    for dispatch in dispatches:
        outputs = []
        for write in dispatch['writes']:
            # Propagate overlapping subranges through actual GPU copies. Keep all
            # origins conservatively: later writes may overwrite an origin.
            spans = {(write['buffer'], write['offset'], write['offset']+write['size'])}
            uses = []
            for event in events:
                if event['block'] <= dispatch['block']:
                    continue
                if event['kind'] == 'copy':
                    for buffer, lo, hi in list(spans):
                        a, b = max(lo, event['src_offset']), min(hi, event['src_offset']+event['size'])
                        if buffer == event['src'] and a < b:
                            delta = event['dst_offset']-event['src_offset']
                            spans.add((event['dst'], a+delta, b+delta))
                            uses.append(dict(block=event['block'], kind='GPU copy', source_range=[a,b],
                                             dst_buffer=event['dst'], dst_range=[a+delta,b+delta]))
                else:
                    for read in event['buffers']:
                        overlaps = [[max(lo, read['offset']), min(hi, read['offset']+read['size'])]
                                    for buffer, lo, hi in spans if buffer == read['buffer']
                                    and max(lo, read['offset']) < min(hi, read['offset']+read['size'])]
                        if overlaps:
                            uses.append(dict(block=event['block'], kind=event['kind']+' declared descriptor overlap',
                                             stage={0:'vertex',1:'fragment',2:'compute',3:'tess-control',4:'tess-eval',5:'geometry'}[read['set']],
                                             descriptor=read, overlaps=overlaps))
            outputs.append(dict(declared_write=write, downstream_uses=uses,
                                propagated_ranges=sorted(spans)))
        results.append(dict(block=dispatch['block'], pipeline=dispatch['shader']['pipeline'],
                            source=dispatch['shader'].get('source'), groups=dispatch['groups'],
                            buffers=dispatch['buffers'], outputs=outputs,
                            reflected_storage_images=dispatch['shader'].get('storage_images'),
                            write_reflection_available='storage_buffers' in dispatch['shader']))
    report = dict(input=str(args.input), input_sha256=sha(args.input),
                  shader_audit_sha256=sha(args.shader_audit), script_sha256=sha(Path(__file__)),
                  api_counts=counts, dispatches=results,
                  limitations=['Declared descriptor overlap, not proof of executed accesses or byte liveness.',
                               'Copy propagation retains overwritten origins conservatively.',
                               'Ordinary VS outputs are not buffer writes in this audit.',
                               'Image alias/subresource and vertex/index accessed ranges are not resolved.',
                               'Capture contains no guest CPU loads; CPU read necessity is not proven.'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(dispatches=len(results), reflected_writable_buffers=sum(len(d['outputs']) for d in results))))


if __name__ == '__main__':
    main()
