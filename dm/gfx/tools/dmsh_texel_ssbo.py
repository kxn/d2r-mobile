"""Emit an optional, immutable readonly float-buffer shader alternative.

SPIRV-Tools has no texel-buffer-to-SSBO descriptor representation pass. This
small adapter supplies that missing representation; assembly, CFG inlining,
dead-resource removal and validation remain official SPIRV-Tools operations.

An OpTypeImage float Buffer does NOT establish its runtime VkBufferView format.
The alternative therefore carries a mandatory runtime RGBA32F/stride16/range
contract. Its caller must retain the original module and descriptor schema and
select this alternative only after inspecting the actual bound resource. No
shader name, game key, material value, or captured buffer content enters policy.
"""
from pathlib import Path
import hashlib
import struct
import subprocess
import tempfile

REQUIRED_FORMAT = 'VK_FORMAT_R32G32B32A32_SFLOAT'
PAIR_MAGIC = 0x31535444  # DTS1: little-endian, immutable paired alternatives.
IDENTITY_MODEL = 'identity'
QUIET_NAN_MODEL = 'rgba32f_quiet_nan_v1'
LOAD_MODELS = (IDENTITY_MODEL, QUIET_NAN_MODEL)


def transform(code, eligible_bindings, descriptor_set, load_model=IDENTITY_MODEL):
    """Return (alternative SPIR-V, proof), or (None, skipped proof).

    eligible_bindings comes from the existing readonly texel-buffer reflection,
    not from guessed formats in SPIR-V. All proven eligible scalar descriptors
    are converted together; the runtime must check every converted binding.
    Array descriptors, non-float32 images, image queries/other opaque uses, and
    writable images retain the original path. Bounds are explicit: negative or
    >= descriptor-range/16 indices produce float4(0). Restrict is never added.
    """
    if load_model not in LOAD_MODELS:
        raise ValueError('unsupported RGBA32F typed-load model')
    eligible_bindings = {b for b in eligible_bindings if 0<=b<128}
    if not eligible_bindings:
        return None, {'skipped': 'no readonly texel-buffer reflection'}
    with tempfile.TemporaryDirectory(prefix='dmsh_texel_ssbo_') as temp:
        root = Path(temp)
        src = root / 'source.spv'
        src.write_bytes(code)
        lines = subprocess.check_output(['spirv-dis', '--raw-id', str(src)], text=True).splitlines()
        defs, decorations, rows = {}, {}, []
        for index, line in enumerate(lines):
            tokens = line.split(';', 1)[0].split()
            if not tokens:
                continue
            rid = None
            if len(tokens) > 2 and tokens[1] == '=':
                rid, tokens = tokens[0], tokens[2:]
            op, args = tokens[0], tokens[1:]
            rows.append((rid, op, args, index))
            if rid:
                defs[rid] = (op, args)
            if op == 'OpDecorate' and len(args) == 3:
                decorations[(args[0], args[1])] = args[2]
        # ImageMemory and UniformMemory have different SPIR-V barrier bits.
        # This bounded representation adapter does not rewrite shader memory
        # semantics or discard coherent/volatile/non-private qualifications.
        if any(op in ('OpMemoryBarrier','OpControlBarrier') for _,op,_,_ in rows):
            return None, {'skipped': 'shader memory/control barrier requires semantic lowering'}
        forbidden = {'Coherent','Volatile','VolatileTexel','VolatileTexelKHR',
                     'NonPrivateTexel','NonPrivateTexelKHR','NonPrivatePointer','NonPrivatePointerKHR'}
        if any(any(part in forbidden for a in args for part in a.split('|'))
               for _,op,args,_ in rows):
            return None, {'skipped': 'coherent/volatile/non-private memory qualification'}
        if any(op=='OpMemoryModel' and args!=['Logical','GLSL450'] for _,op,args,_ in rows):
            return None, {'skipped': 'memory model requires semantic lowering'}
        candidates = {}
        for rid, (op, args) in defs.items():
            if op != 'OpVariable' or args[1] != 'UniformConstant':
                continue
            binding = int(decorations.get((rid, 'Binding'), '-1'))
            if binding not in eligible_bindings or decorations.get((rid, 'DescriptorSet')) != str(descriptor_set):
                continue
            ptr = defs.get(args[0])
            if not ptr or ptr[0] != 'OpTypePointer':
                continue
            image = defs.get(ptr[1][1])
            if image and image[0] == 'OpTypeSampledImage':
                image = defs.get(image[1][0])
            if not image or image[0] != 'OpTypeImage':
                continue
            a = image[1]
            scalar = defs.get(a[0])
            if scalar != ('OpTypeFloat', ['32']) or a[1:] != ['Buffer', '0', '0', '0', '1', 'Unknown']:
                continue
            candidates[rid] = {'binding': binding, 'scalar': a[0]}
        if not candidates:
            return None, {'skipped': 'no scalar readonly float32 Buffer descriptor'}

        def opaque_root(value, seen=()):
            if value in seen:
                return None
            if value in candidates:
                return value
            op, a = defs.get(value, ('', []))
            if op in ('OpLoad', 'OpImage', 'OpCopyObject'):
                return opaque_root(a[1], (*seen, value))
            return None

        fetches, rejected = {}, set()
        for rid, op, a, index in rows:
            if op.startswith('OpImage') and op != 'OpImage':
                resource = opaque_root(a[1]) if rid and len(a) > 1 else None
                if not resource:
                    continue
                result = defs.get(a[0])
                expected = ('OpTypeVector', [candidates[resource]['scalar'], '4'])
                coord = defs.get(a[2]) if op == 'OpImageFetch' and len(a) >= 3 else None
                coord_type = defs.get(coord[1][0]) if coord and coord[1] else None
                if op != 'OpImageFetch' or a[3:] not in ([], ['None']) or result != expected or not coord_type or coord_type[0] != 'OpTypeInt' or coord_type[1][0] != '32':
                    rejected.add(resource)
                    continue
                fetches[rid] = (resource, a[0], a[2], coord[1][0], index)
            # Opaque values passed to functions/phi/select/etc are not proven.
            if op not in ('OpLoad', 'OpImage', 'OpCopyObject', 'OpImageFetch', 'OpName', 'OpDecorate'):
                for operand in a[1:] if rid else a:
                    resource = opaque_root(operand)
                    if resource:
                        rejected.add(resource)
        fetches = {rid: f for rid, f in fetches.items() if f[0] not in rejected}
        used = {f[0] for f in fetches.values()}
        if not used:
            return None, {'skipped': 'buffer image has unsupported use or no vec4 fetch'}
        candidates = {rid: value for rid, value in candidates.items() if rid in used}
        uint = next((rid for rid, d in defs.items() if d == ('OpTypeInt', ['32', '0'])), '%tss_uint')
        boolean = next((rid for rid, d in defs.items() if d == ('OpTypeBool', [])), '%tss_bool')
        # Helpers take the original coordinate type, preserving signed index bits.
        annotations, declarations, helpers, contracts = [], [], [], []
        if uint == '%tss_uint':
            declarations.append(f'{uint} = OpTypeInt 32 0')
        if boolean == '%tss_bool':
            declarations.append(f'{boolean} = OpTypeBool')
        declarations.append(f'%tss_zero_index = OpConstant {uint} 0')
        if load_model == QUIET_NAN_MODEL:
            # Explicit format-conversion hypothesis, not relaxed arithmetic.
            # Input is loaded as integers; all non-NaN bits remain unchanged.
            # This does not authorize any device: driver/context qualification
            # belongs to the bounded experiment, never to a shader/game key.
            declarations += [f'%tss_uint4 = OpTypeVector {uint} 4',
                             f'%tss_bool4 = OpTypeVector {boolean} 4',
                             f'%tss_exp = OpConstant {uint} 2139095040',
                             f'%tss_mantissa = OpConstant {uint} 8388607',
                             f'%tss_quiet = OpConstant {uint} 4194304',
                             '%tss_uint4_zero = OpConstantNull %tss_uint4',
                             '%tss_exp4 = OpConstantComposite %tss_uint4 %tss_exp %tss_exp %tss_exp %tss_exp',
                             '%tss_mantissa4 = OpConstantComposite %tss_uint4 %tss_mantissa %tss_mantissa %tss_mantissa %tss_mantissa',
                             '%tss_quiet4 = OpConstantComposite %tss_uint4 %tss_quiet %tss_quiet %tss_quiet %tss_quiet']
        for resource in sorted(candidates, key=lambda rid: candidates[rid]['binding']):
            c = candidates[resource]
            binding = c['binding']
            selected = [f for f in fetches.values() if f[0] == resource]
            vector = selected[0][1]
            load_vector = '%tss_uint4' if load_model == QUIET_NAN_MODEL else vector
            tag = f'tss_b{binding}'
            annotations += [f'OpDecorate %{tag}_array ArrayStride 16', f'OpDecorate %{tag}_block Block',
                            f'OpMemberDecorate %{tag}_block 0 Offset 0', f'OpMemberDecorate %{tag}_block 0 NonWritable',
                            f'OpDecorate %{tag}_buffer DescriptorSet {descriptor_set}', f'OpDecorate %{tag}_buffer Binding {binding}']
            declarations += [f'%{tag}_array = OpTypeRuntimeArray {load_vector}', f'%{tag}_block = OpTypeStruct %{tag}_array',
                             f'%{tag}_ptrblock = OpTypePointer StorageBuffer %{tag}_block', f'%{tag}_ptrelem = OpTypePointer StorageBuffer {load_vector}',
                             f'%{tag}_buffer = OpVariable %{tag}_ptrblock StorageBuffer', f'%{tag}_zero = OpConstantNull {vector}']
            for coord_type in sorted({f[3] for f in selected}):
                name = tag + '_i' + coord_type[1:]
                declarations.append(f'%{name}_type = OpTypeFunction {vector} {coord_type}')
                helpers += [f'%{name}_read = OpFunction {vector} None %{name}_type', f'%{name}_index = OpFunctionParameter {coord_type}',
                            f'%{name}_entry = OpLabel', f'%{name}_unsigned = OpBitcast {uint} %{name}_index',
                            f'%{name}_length = OpArrayLength {uint} %{tag}_buffer 0', f'%{name}_valid = OpULessThan {boolean} %{name}_unsigned %{name}_length',
                            f'OpSelectionMerge %{name}_merge None', f'OpBranchConditional %{name}_valid %{name}_load %{name}_empty',
                            f'%{name}_load = OpLabel', f'%{name}_ptr = OpAccessChain %{tag}_ptrelem %{tag}_buffer %tss_zero_index %{name}_unsigned']
                if load_model == QUIET_NAN_MODEL:
                    helpers += [f'%{name}_raw = OpLoad %tss_uint4 %{name}_ptr',
                                f'%{name}_exp = OpBitwiseAnd %tss_uint4 %{name}_raw %tss_exp4',
                                f'%{name}_maxexp = OpIEqual %tss_bool4 %{name}_exp %tss_exp4',
                                f'%{name}_mantissa = OpBitwiseAnd %tss_uint4 %{name}_raw %tss_mantissa4',
                                f'%{name}_fraction = OpINotEqual %tss_bool4 %{name}_mantissa %tss_uint4_zero',
                                f'%{name}_nan = OpLogicalAnd %tss_bool4 %{name}_maxexp %{name}_fraction',
                                f'%{name}_mask = OpSelect %tss_uint4 %{name}_nan %tss_quiet4 %tss_uint4_zero',
                                f'%{name}_formatted = OpBitwiseOr %tss_uint4 %{name}_raw %{name}_mask',
                                f'%{name}_value = OpBitcast {vector} %{name}_formatted']
                else:
                    helpers.append(f'%{name}_value = OpLoad {vector} %{name}_ptr')
                helpers += [f'OpBranch %{name}_merge', f'%{name}_empty = OpLabel',
                            f'OpBranch %{name}_merge', f'%{name}_merge = OpLabel',
                            f'%{name}_result = OpPhi {vector} %{name}_value %{name}_load %{tag}_zero %{name}_empty',
                            f'OpReturnValue %{name}_result', 'OpFunctionEnd']
            contracts.append({'binding': binding, 'descriptor_set': descriptor_set, 'source_descriptor': 'uniform_texel_buffer',
                              'alternative_descriptor': 'readonly_storage_buffer', 'required_runtime_format': REQUIRED_FORMAT,
                              'stride_bytes': 16, 'fetch_sites': len(selected), 'bounds': 'unsigned(index) < ArrayLength; else vec4(0)'})
            if load_model != IDENTITY_MODEL:
                contracts[-1]['load_model'] = load_model
        for rid, (resource, vector, coord, coord_type, index) in fetches.items():
            tag = f"tss_b{candidates[resource]['binding']}_i{coord_type[1:]}"
            lines[index] = f'{rid} = OpFunctionCall {vector} %{tag}_read {coord}'
        if load_model == QUIET_NAN_MODEL:
            # SPIR-V does not permit duplicate scalar/vector type declarations.
            # Reuse original packed-bit uint4/bool4 types when they exist.
            aliases = {}
            for symbol, scalar in (('%tss_uint4', uint), ('%tss_bool4', boolean)):
                existing = next((rid for rid, d in defs.items()
                                 if d == ('OpTypeVector', [scalar, '4'])), None)
                if existing:
                    aliases[symbol] = existing
                    declarations = [d for d in declarations if not d.startswith(symbol + ' = ')]
            def reuse_types(text):
                return ' '.join(aliases.get(token, token) for token in text.split())
            declarations = [reuse_types(d) for d in declarations]
            helpers = [reuse_types(h) for h in helpers]
        first_type = next(i for i, line in enumerate(lines) if '= OpType' in line)
        lines[first_type:first_type] = annotations
        first_function = next(i for i, line in enumerate(lines) if '= OpFunction ' in line)
        lines[first_function:first_function] = declarations
        asm, binary, result = root/'alternative.spvasm', root/'unoptimized.spv', root/'alternative.spv'
        asm.write_text('\n'.join([*lines, *helpers]) + '\n')
        subprocess.run(['spirv-as', '--target-env', 'vulkan1.1', str(asm), '-o', str(binary)], check=True, capture_output=True)
        subprocess.run(['spirv-opt', '--inline-entry-points-exhaustive', '--eliminate-dead-code-aggressive', '--trim-capabilities', str(binary), '-o', str(result)], check=True, capture_output=True)
        subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(result)], check=True, capture_output=True)
        alternate = result.read_bytes()
        proof = {'base_sha256': hashlib.sha256(code).hexdigest(), 'alternative_sha256': hashlib.sha256(alternate).hexdigest(),
                           'runtime_contracts': contracts, 'fetch_sites': len(fetches), 'adds_restrict': False,
                           'preserves_original_module': True, 'requires_runtime_format_selection': True}
        if load_model != IDENTITY_MODEL:
            proof['load_model'] = load_model
        return alternate, proof


def pair(base, specialized, eligible_bindings, descriptor_set, load_model=IDENTITY_MODEL):
    """Pair both immutable modules; retain specialization constants and recipes.

    specialized is the complete existing DSP2 recipe or None. Identity covers
    both input files; the runtime must not combine an old alternative with a
    different original base/spec file. Reflection binding contracts must match.
    This function does not edit the existing .spv, .meta or .spec files.
    """
    if specialized:
        if len(specialized) < 80 or len(specialized) % 4 or struct.unpack_from('<I', specialized)[0] not in (0x31505344, 0x32505344):
            raise ValueError('invalid Uniform specialization recipe')
        words = struct.unpack_from('<7I', specialized)
        if len(specialized) != 60 + words[6]*4 or specialized[28:60] != hashlib.sha256(base).digest():
            raise ValueError('Uniform specialization recipe does not match base')
    alternate, proof = transform(base, eligible_bindings, descriptor_set, load_model)
    if alternate is None:
        return None, proof
    spec_alt = b''
    if specialized:
        spec_alt, spec_proof = transform(specialized[60:], eligible_bindings, descriptor_set, load_model)
        if spec_alt is None or spec_proof['runtime_contracts'] != proof['runtime_contracts']:
            return None, {'skipped': 'base/specialized alternative contracts differ'}
        proof['specialized_alternative_sha256'] = hashlib.sha256(spec_alt).hexdigest()
    contracts = proof['runtime_contracts']
    if len(contracts)>16:return None, {'skipped': 'bounded alternative exceeds 16 texel bindings'}
    header = struct.pack('<6I', PAIR_MAGIC, 1, descriptor_set, len(contracts), len(alternate)//4, len(spec_alt)//4)
    payload = (header + hashlib.sha256(base).digest() + (hashlib.sha256(specialized).digest() if specialized else bytes(32)) +
               b''.join(struct.pack('<I', c['binding']) for c in contracts) + alternate + spec_alt)
    if len(payload)>32<<20:return None, {'skipped': 'alternative pair exceeds bounded cache file size'}
    proof['pair_sha256'] = hashlib.sha256(payload).hexdigest()
    return payload, proof


def unpack_pair(payload, base, specialized, proof):
    """Verify persisted cache identity and every declared alternative payload."""
    if len(payload)<108 or len(payload)%4:
        raise ValueError('short texel alternative pair')
    magic,version,descriptor_set,count,base_words,spec_words=struct.unpack_from('<6I',payload)
    if magic!=PAIR_MAGIC or version!=1 or not 1<=count<=16 or base_words<5 or bool(spec_words)!=bool(specialized) or len(payload)!=88+4*(count+base_words+spec_words):
        raise ValueError('invalid texel alternative header')
    if payload[24:56]!=hashlib.sha256(base).digest() or payload[56:88]!=(hashlib.sha256(specialized).digest()if specialized else bytes(32)):
        raise ValueError('texel alternative input mismatch')
    bindings=list(struct.unpack_from('<%dI'%count,payload,88))
    contracts=proof['runtime_contracts']
    model=proof.get('load_model', IDENTITY_MODEL)
    if model not in LOAD_MODELS or any(c.get('load_model', IDENTITY_MODEL)!=model for c in contracts):
        raise ValueError('texel alternative load model mismatch')
    if len(set(bindings))!=count or bindings!=[c['binding']for c in contracts] or any(c['descriptor_set']!=descriptor_set or c['required_runtime_format']!=REQUIRED_FORMAT or c['stride_bytes']!=16 for c in contracts):
        raise ValueError('texel alternative reflection mismatch')
    start=88+4*count;alternate=payload[start:start+base_words*4];spec_alt=payload[start+base_words*4:]
    if hashlib.sha256(payload).hexdigest()!=proof['pair_sha256'] or hashlib.sha256(alternate).hexdigest()!=proof['alternative_sha256'] or (spec_alt and hashlib.sha256(spec_alt).hexdigest()!=proof['specialized_alternative_sha256']):
        raise ValueError('texel alternative payload hash mismatch')
    return alternate,spec_alt,bindings
