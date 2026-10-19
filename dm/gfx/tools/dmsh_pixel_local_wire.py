"""Deployable DPL1 input-attachment alternatives, bound to each actual module.

Complete same-UV group plus each nonempty single-omission subset; never a powerset.
Runtime still proves geometry, sampler, layout, attachment and pass conditions.
All integers use little endian; there are no native pointers or JSON in DPL1.
"""
import hashlib
from pathlib import Path
import struct
from dmsh_pixel_local import analyze, input_variant

MAGIC = 0x314c5044
VERSION = 2
SUFFIXES = ('spv', 'meta', 'spec', 'pred', 'tssbo')
MODULES = {'base': 1, 'spec': 2, 'pred': 3, 'tssbo': 4, 'tssbo_spec': 5}
HEADER_BYTES = 192
MAX_BYTES = 32 << 20


def sha(data): return hashlib.sha256(data).digest()


def actual_modules(blobs, reflection):
    result = {'base': blobs['spv']}
    if 'spec' in blobs:
        from dmsh_uniform_predicates import _old_recipe
        _old_recipe(blobs['spv'], blobs['spec'])
        result['spec'] = blobs['spec'][60:]
    if 'pred' in blobs:
        from dmsh_uniform_predicates import unpack
        result['pred'] = unpack(blobs['pred'], blobs['spv'], blobs['spec'])[0]
    if 'tssbo' in blobs:
        from dmsh_texel_ssbo import unpack_pair
        proof = reflection.get('texel_storage_alternative')
        if not proof: raise ValueError('missing paired-module proof')
        alt, spec, _ = unpack_pair(blobs['tssbo'], blobs['spv'], blobs.get('spec'), proof)
        result['tssbo'] = alt
        if spec: result['tssbo_spec'] = spec
    return result


def _record(kind, source, omit=None):
    proof = analyze(source)
    if 'rejected' in proof: return None
    bindings = []; recipes = []; variant = b''; location = 0; position = 0; mask = 0; group_count = 0
    if proof['stage'] == 'Fragment':
        groups = {}
        for binding in proof['bindings']:
            if not binding['eligible'] or binding['set'] >= 6 or binding['binding'] >= 128: continue
            locations = {s['varying_location'] for s in binding['samples']}
            if len(locations) == 1 and next(iter(locations)) < 16:
                groups.setdefault(next(iter(locations)), []).append(binding)
        # A single complete group. Oversized groups cannot be truncated: doing
        # so would make the declared attachment union differ from the module.
        groups = {k:v for k,v in groups.items() if 0 < len(v) <= 8}
        if not groups: return None
        location = min(groups, key=lambda k:(-len(groups[k]), k))
        selected = sorted(groups[location], key=lambda b:(b['set'], b['binding']))
        if len({b['set'] for b in selected}) != 1: return None
        group_count = len(selected)
        mask = (1 << group_count)-1
        if omit is not None:
            if not 0 <= omit < len(selected) or len(selected)==1: return None
            mask &= ~(1 << omit)
            selected = [b for i,b in enumerate(selected) if i != omit]
        mapping = {(b['set'], b['binding']):i for i,b in enumerate(selected)}
        variant, _ = input_variant(source, mapping)
        for i,b in enumerate(selected):
            lod = (1 if any(s['lod']=='implicit-zero-required' for s in b['samples']) else 0) | (2 if any(s['lod']=='explicit-zero' for s in b['samples']) else 0)
            bindings.append(struct.pack('<4I', b['set'], b['binding'], i, lod))
    elif proof['stage'] == 'Vertex':
        if omit is not None: return None
        position = proof['position']['attribute']
        if not 0 <= position < 16: return None
        for r in proof['uv_recipes']:
            if not 0 <= r['location'] < 16 or not 0 <= r['attribute'] < 16: continue
            refs = [0]*12
            if r['mode'] == 'fma':
                refs = [x for v in (*r['scale'], *r['bias']) for x in v]
                if any(not (0 <= v[0] < 6 and 0 <= v[1] < 128 and 0 <= v[2] <= 65532 and v[2]%4 == 0) for v in (*r['scale'], *r['bias'])): continue
            recipes.append(struct.pack('<16I', r['location'], r['attribute'], int(r['mode']=='fma'), 0, *refs))
        if not recipes or len(recipes)>16: return None
    else: return None
    body = b''.join(bindings+recipes)+variant
    return (struct.pack('<8I', kind, 96+len(body), len(bindings), len(recipes), location,
                        position, len(variant)//4, mask | (group_count << 8))+sha(source)+
            (sha(variant) if variant else bytes(32))+body)


def build(blobs, reflection=None):
    reflection = reflection or {}
    modules = actual_modules(blobs, reflection)
    base = analyze(blobs['spv'])
    stage = {'Vertex':0, 'Fragment':1}.get(base.get('stage'), 5)
    # Metadata is an independent member of the fingerprint closure.
    descriptor_set = reflection.get('descriptor_set', 0)
    if not 0 <= descriptor_set < 6: raise ValueError('pixel-local descriptor set')
    records = []
    for name,code in modules.items():
        full = _record(MODULES[name], code)
        if full is None: continue
        records.append(full)
        count = struct.unpack_from('<I',full,8)[0]
        if count > 1:
            records.extend(_record(MODULES[name], code,omit) for omit in range(count))
    body = b''.join(records)
    size = HEADER_BYTES+len(body)
    if size > MAX_BYTES: raise ValueError('pixel-local artifact size limit')
    data = (struct.pack('<8I', MAGIC, VERSION, size, stage, descriptor_set,
                        len(records), HEADER_BYTES, 0)+
            b''.join(sha(blobs[s]) if s in blobs else bytes(32) for s in SUFFIXES)+body)
    return data, {'schema':VERSION, 'sha256':sha(data).hex(), 'records':len(records),
                  'source_modules':{k:sha(v).hex() for k,v in modules.items()}}


def emit(root, key, reflection):
    root = Path(root)
    blobs = {s:(root/(key+'.'+s)).read_bytes() for s in SUFFIXES if (root/(key+'.'+s)).is_file()}
    data, proof = build(blobs, reflection)
    (root/(key+'.plocal')).write_bytes(data)
    return proof
