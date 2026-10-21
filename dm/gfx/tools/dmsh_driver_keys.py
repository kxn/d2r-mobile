"""Exact driver-table read schemas, over every function and control-flow arm.

This does not specialize a shader or infer a material value. SPIRV-Tools owns
validation/disassembly; every unsupported pointer use keeps the full CPU key.
DDK1 is optional and bound to all five immutable shader artifact hashes.
"""
from pathlib import Path
import hashlib
import struct
import subprocess
import tempfile
from dmsh_uniform_predicates import _parse

MAGIC = 0x314b4444
SPAN_MAGIC = 0x31505344  # DSP1; independent of the unchanged DDK1 wire/schema
SPAN_BYTES = 96
MAX_WORDS = 32
SUFFIXES = ('meta', 'spv', 'spec', 'pred', 'tssbo')


def fingerprint(blobs):
    return hashlib.sha256(b''.join(hashlib.sha256(blobs[s]).digest()
                                  if s in blobs else bytes(32) for s in SUFFIXES)).digest()


def metadata(blob):
    if len(blob) < 32 or len(blob) % 4:
        raise ValueError('short metadata')
    h = struct.unpack_from('<8I', blob)
    header = {0x314b5644: 8, 0x324b5644: 12, 0x334b5644: 16}.get(h[0])
    if header is None or h[1] >= 6 or h[3] > 128 or len(blob) != header*4+h[3]*32+h[4]:
        raise ValueError('metadata shape')
    return h[1], h[2], [struct.unpack_from('<8i', blob, header*4+i*32) for i in range(h[3])]


def compact_word(slot, stage, offset):
    """Map an aligned ABI u32 to its varying-key word, None for proven zero."""
    if offset < 0 or offset > 65532 or offset % 4:
        raise ValueError('unaligned/out-of-range read')
    if slot == 2:
        for start, count, base in ((0x168, 128, 0), (0x568, 128, 128), (0x120, 8, 256)):
            if start <= offset < start+count*8:
                return base+(offset-start)//8 if (offset-start) % 8 == 0 else None
    elif slot == 0:
        if stage not in (0, 1, 5):
            raise ValueError('unsupported c0 stage')
        banks = [({0:0x110, 1:0x510, 5:0x310}[stage], 0)]
        if stage == 5: banks.append((0x210,1))
        for start, bank in banks:
            if start <= offset < start+256:
                within = (offset-start) % 16
                return bank*48+(offset-start)//16*3+within//4 if within < 12 else None
    return None


def reads(code, descriptor_set, binding):
    """Return all potentially loaded u32 offsets, or reject the whole binding."""
    with tempfile.TemporaryDirectory(prefix='dmsh_driver_key_') as temporary:
        path = Path(temporary)/'module.spv'; path.write_bytes(code)
        subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(path)],
                       check=True, capture_output=True)
        text = subprocess.check_output(['spirv-dis', '--raw-id', str(path)], text=True)
    defs, rows, decs, members = _parse(text)
    # Phi operands can be defined on a later back edge, so discovering an
    # escape only while walking root-derived pointers in physical order is
    # insufficient. Close the pointer-result domain BEFORE that walk. Scalar
    # and vector Phi/Select remain fully supported and all their arms scanned.
    for rid,op,a,_ in rows:
        if op.startswith('OpType') and 'Pointer' in op and op != 'OpTypePointer':
            raise ValueError('unsupported pointer type '+op)
        if rid and a and defs.get(a[0],(None,None))[1] == 'OpTypePointer':
            # A typed Function-storage parameter cannot contain a Uniform
            # pointer: validated calls require the exact storage/type. Keep
            # these unrelated arithmetic helper parameters (used by HDR),
            # while Uniform parameters and every pointer Phi/Select/copy/
            # cast/load/return remain a whole-binding fallback.
            local_parameter = op == 'OpFunctionParameter' and defs[a[0]][2][0] == 'Function'
            if op not in ('OpVariable','OpAccessChain','OpInBoundsAccessChain') and not local_parameter:
                raise ValueError('pointer escape/unsupported result '+op)
    if any(op == 'OpCapability' and a[0] in ('VariablePointers','VariablePointersStorageBuffer')
           for _,op,a,_ in rows):
        raise ValueError('unsupported variable pointer capability')
    if any(op in ('OpDecorationGroup', 'OpGroupDecorate', 'OpGroupMemberDecorate',
                  'OpDecorateId', 'OpMemberDecorateString', 'OpDecorateString') for _,op,_,_ in rows):
        raise ValueError('unsupported decorations')
    constants = {rid:int(a[1]) for rid,op,a,_ in rows if op == 'OpConstant'
                 and defs[a[0]][1] == 'OpTypeInt'}
    for (rid,decoration),value in decs.items():
        if decoration == 'Binding' and value == [str(binding)] and decs.get((rid,'DescriptorSet')) == [str(descriptor_set)]:
            if defs.get(rid,(None,None))[1] != 'OpVariable':
                raise ValueError('unsupported descriptor root')
    roots = []
    for rid,op,a,_ in rows:
        if op != 'OpVariable':
            continue
        if decs.get((rid,'DescriptorSet')) == [str(descriptor_set)] and decs.get((rid,'Binding')) == [str(binding)]:
            typ = defs[a[0]]
            if a[1] != 'Uniform' or typ[1] != 'OpTypePointer' or typ[2][0] != 'Uniform' or (typ[2][1],'Block') not in decs:
                raise ValueError('unsupported root')
            roots.append((rid, typ[2][1]))
    if not roots:
        return set()  # Removed from this complete module; another variant may read it.
    if len(roots) != 1:
        raise ValueError('duplicate roots')
    root, typ = roots[0]
    if any((rid,q) in decs for rid in (root,typ) for q in ('Volatile','Coherent','Aliased','RestrictPointer','AliasedPointer')):
        raise ValueError('qualified root')
    pointers = {root:(0,typ)}; offsets = set()
    def size(t):
        op,a = defs[t][1:3]
        if op in ('OpTypeFloat','OpTypeInt') and a[0] in ('32','64'):
            return int(a[0])//8
        if op == 'OpTypeVector':
            return size(a[0])*int(a[1])
        raise ValueError('aggregate/non-word load')
    # With every other pointer-producing instruction already rejected,
    # validation's dominance rule places access-chain definitions before uses.
    for rid,op,a,_ in rows:
        if op in ('OpAccessChain','OpInBoundsAccessChain') and a[1] in pointers:
            offset, typ = pointers[a[1]]
            for index in a[2:]:
                if index not in constants or constants[index] < 0:
                    raise ValueError('dynamic/negative index')
                n = constants[index]; kind,args = defs[typ][1:3]
                if kind == 'OpTypeStruct':
                    if n >= len(args) or (typ,str(n),'Offset') not in members:
                        raise ValueError('struct layout')
                    if any((typ,str(n),q) in members for q in ('Volatile','Coherent','Aliased','RowMajor','MatrixStride')):
                        raise ValueError('qualified member')
                    offset += int(members[typ,str(n),'Offset']); typ = args[n]
                elif kind == 'OpTypeArray':
                    if args[1] not in constants or n >= constants[args[1]] or (typ,'ArrayStride') not in decs:
                        raise ValueError('array layout/bounds')
                    offset += n*int(decs[typ,'ArrayStride'][0]); typ = args[0]
                elif kind == 'OpTypeVector':
                    if n >= int(args[1]):
                        raise ValueError('vector bounds')
                    offset += n*size(args[0]); typ = args[0]
                else:
                    raise ValueError('unsupported indexed type')
            if defs[a[0]][1:3] != ('OpTypePointer', ['Uniform',typ]):
                raise ValueError('pointer type mismatch')
            pointers[rid] = offset,typ
        tracked = [v for v in a if v in pointers]
        if not tracked or op in ('OpName','OpDecorate','OpEntryPoint'):
            continue
        if op in ('OpAccessChain','OpInBoundsAccessChain'):
            if a[1] not in pointers or any(v != a[1] for v in tracked):
                raise ValueError('pointer used as index')
            continue
        if op == 'OpLoad' and len(a) >= 2 and tracked == [a[1]]:
            if len(a) != 2:  # Optional memory operands remain a conservative fallback.
                raise ValueError('load memory operands')
            offset,typ = pointers[a[1]]
            if a[0] != typ:
                raise ValueError('load type mismatch')
            count = size(typ)
            if offset % 4 or offset < 0 or count % 4 or offset+count > 65536:
                raise ValueError('load bounds')
            offsets.update(range(offset, offset+count, 4))
        else:
            raise ValueError('pointer escape/unsupported use '+op)
    return offsets


def build_with_spans(blobs, alternate_proof=None):
    stage, descriptor_set, bindings = metadata(blobs['meta'])
    modules = [blobs['spv']]
    if 'spec' in blobs:
        from dmsh_uniform_predicates import _old_recipe
        _old_recipe(blobs['spv'], blobs['spec']); modules.append(blobs['spec'][60:])
    if 'pred' in blobs:
        from dmsh_uniform_predicates import unpack
        modules.append(unpack(blobs['pred'], blobs['spv'], blobs['spec'])[0])
    module_error = None
    if 'tssbo' in blobs:
        from dmsh_texel_ssbo import unpack_pair
        try:
            if alternate_proof is None: raise ValueError('missing paired-module proof')
            alt, spec_alt, _ = unpack_pair(blobs['tssbo'],blobs['spv'],blobs.get('spec'),alternate_proof)
            modules.append(alt)
            if spec_alt: modules.append(spec_alt)
        except ValueError as error:
            module_error = str(error)
    result = bytearray(struct.pack('<4I',MAGIC,1,stage,descriptor_set)+fingerprint(blobs))
    span = bytearray(struct.pack('<4I',SPAN_MAGIC,1,stage,descriptor_set)+fingerprint(blobs))
    report = {}
    span_report = {}
    for slot in (0,2):
        matches = [b for b in bindings if b[0] == 1 and b[2] == slot]
        words = None; reason = 'absent/ambiguous driver binding'
        end = 65536; span_reason = reason
        binding = matches[0][1] if len(matches) == 1 else 0xffffffff
        span_valid = False
        if len(matches) == 1 and matches[0][5] == 1 and not module_error:
            try:
                if not 0 <= binding < 128: raise ValueError('binding range')
                offsets = set().union(*(reads(code,descriptor_set,binding) for code in modules))
                # Full load width, including compact_word(None) zero-tail
                # words. Dense varying keys do not invalidate a range proof.
                end = max(offsets, default=-4)+4
                span_reason = 'all modules/all functions/all branches exact byte end'
                span_valid = True
                words = sorted({word for offset in offsets if (word := compact_word(slot,stage,offset)) is not None})
                full_words = 264 if slot == 2 else 96 if stage == 5 else 48
                if len(words) > MAX_WORDS or len(words)*2 >= full_words:
                    raise ValueError('dense key')
                reason = 'all modules/all functions/all branches exact'
            except (ValueError, subprocess.CalledProcessError) as error:
                reason = str(error); words = None
                if not span_valid:
                    end = 65536; span_reason = reason
        elif module_error:
            reason = module_error
            span_reason = module_error
        result += struct.pack('<2I',binding,0xffffffff if words is None else len(words))
        if words is not None:
            result += struct.pack('<%dH'%len(words),*words)
            if len(words) % 2: result += bytes(2)
        report[str(slot)] = {'binding':binding,'words':words,'reason':reason}
        span += struct.pack('<2I',binding,end)
        span_report[str(slot)] = {'binding':binding,'max_read_end':end,'reason':span_reason}
    return (bytes(result)+hashlib.sha256(result).digest(), report,
            bytes(span)+hashlib.sha256(span).digest(), span_report)


def build(blobs, alternate_proof=None):
    """Keep the original DDK1 API and exact bytes, including full-key fallback."""
    key, proof, _, _ = build_with_spans(blobs, alternate_proof)
    return key, proof


def emit(root, key, reflection=None):
    root = Path(root)
    blobs = {suffix:(root/(key+'.'+suffix)).read_bytes() for suffix in SUFFIXES
             if (root/(key+'.'+suffix)).is_file()}
    if reflection is None and (root/(key+'.json')).is_file():
        import json
        reflection = json.loads((root/(key+'.json')).read_text())
    data, proof = build(blobs,(reflection or {}).get('texel_storage_alternative'))
    (root/(key+'.dkey')).write_bytes(data)
    return {'sha256':hashlib.sha256(data).hexdigest(), 'proof':proof}


def emit_with_spans(root, key, reflection=None):
    """Optional companion: analyze once and emit old DDK1 plus fixed DSP1."""
    root = Path(root)
    blobs = {suffix:(root/(key+'.'+suffix)).read_bytes() for suffix in SUFFIXES
             if (root/(key+'.'+suffix)).is_file()}
    if reflection is None and (root/(key+'.json')).is_file():
        import json
        reflection = json.loads((root/(key+'.json')).read_text())
    key_data, key_proof, span_data, span_proof = build_with_spans(
        blobs,(reflection or {}).get('texel_storage_alternative'))
    (root/(key+'.dkey')).write_bytes(key_data)
    (root/(key+'.dspan')).write_bytes(span_data)
    return {'driver_keys':{'sha256':hashlib.sha256(key_data).hexdigest(),'proof':key_proof},
            'driver_spans':{'sha256':hashlib.sha256(span_data).hexdigest(),'proof':span_proof}}


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('keys',nargs='*')
    p.add_argument('--spans',action='store_true',help='emit optional DSP1 and update existing reflection; preserve DDK1 bytes')
    a = p.parse_args()
    keys=a.keys or sorted(v.stem for v in a.root.glob('*.spv')
                          if not a.spans or (a.root/(v.stem+'.json')).is_file())
    for key in keys:
        if a.spans:
            import json
            info=a.root/(key+'.json'); reflection=json.loads(info.read_text())
            proof=emit_with_spans(a.root,key,reflection);reflection.update(proof)
            info.write_text(json.dumps(reflection,ensure_ascii=False,indent=1))
            print(key,proof)
        else:
            print(key, emit(a.root,key))
