#!/usr/bin/env python3
# 原 dmsh_build 的 Vulkan 出口：保留翻译器 SPIR-V，分离各阶段描述符组并逐个验证。
# Vulkan output of the original dmsh_build: retain translator SPIR-V, separate stage descriptor sets, and validate every module.
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import subprocess
import tempfile
import concurrent.futures
from dmsh_storage_bias import bias_storage_offsets
from dmsh_constants import inline_c1


def compiler_options():
    """显式设备编译选项进入缓存指纹。 | Explicit device compilation options enter the cache fingerprint."""
    size = int(os.environ.get('DMSH_VK_SUBGROUP_SIZE', '32'))
    helper = os.environ.get('DMSH_VK_BALLOT_HELPER', '0')
    bias = os.environ.get('DMSH_VK_STORAGE_BIAS', '0')
    c1 = os.environ.get('DMSH_VK_INLINE_C1', '0')
    texels = os.environ.get('DMSH_VK_RELAX_SAMPLED_RESULTS', '0')
    functions = os.environ.get('DMSH_VK_INLINE_FUNCTIONS', '0')
    color_half = os.environ.get('DMSH_VK_PROTECTED_COLOR_HALF', '0')
    uniform_spec = os.environ.get('DMSH_VK_UNIFORM_SPECIALIZATION', '0')
    uniform_predicates = os.environ.get('DMSH_VK_UNIFORM_PREDICATES', '0')
    texel_storage = os.environ.get('DMSH_VK_TEXEL_STORAGE_ALTERNATIVES', '0')
    texel_model = os.environ.get('DMSH_VK_TEXEL_LOAD_MODEL', 'identity')
    if size not in (16, 32, 64, 128) or any(v not in ('0','1') for v in (helper,bias,c1,texels,functions,color_half,uniform_spec,uniform_predicates,texel_storage)):
        raise ValueError('expected subgroup size 16/32/64/128 and boolean compiler switches 0/1')
    if uniform_predicates == '1' and uniform_spec != '1':
        raise ValueError('additional Uniform predicates require old full-word specialization')
    if color_half == '1' and texels == '1':
        raise ValueError('protected color half requires full-precision sampled results')
    from dmsh_texel_ssbo import LOAD_MODELS, IDENTITY_MODEL
    if texel_model not in LOAD_MODELS or (texel_model != IDENTITY_MODEL and texel_storage != '1'):
        raise ValueError('non-default texel load model requires paired alternatives and a declared model')
    options = {'subgroup_size': size, 'ballot_helper': helper == '1', 'storage_bias': bias == '1', 'inline_c1': c1 == '1',
            'relaxed_sampled_results': texels == '1', 'inline_functions': functions == '1',
            'protected_color_half': color_half == '1','uniform_specialization':uniform_spec=='1','texel_storage_alternatives':texel_storage=='1'}
    # Preserve the legacy default dictionary for existing verified phone chains.
    # Non-default models are explicit, fingerprinted compiler inputs.
    if texel_model != IDENTITY_MODEL:
        options['texel_storage_load_model'] = texel_model
    if uniform_predicates == '1':
        options['uniform_predicates'] = True
    return options


def protected_color_half(source):
    """Opt-in explicit FP16 arithmetic; interfaces and non-color effects stay FP32.

    The backend must enable shaderFloat16. This is a device-specific experiment,
    with HDR/underflow visual risks; it is never enabled by default.
    """
    import dmsh_float_transfers
    import dmsh_fragment_precision
    with tempfile.TemporaryDirectory(prefix='dmsh_color_half_') as temporary:
        root=Path(temporary)
        canonical=root/'canonical.spv'
        transfer=dmsh_float_transfers.transform(source,canonical)
        if transfer.get('skipped'):
            return source.read_bytes(),transfer
        color=root/'color.spv'
        proof=dmsh_fragment_precision.transform(canonical,color)
        if proof.get('skipped') or not proof.get('eligible_arithmetic'):
            return source.read_bytes(),{k:v for k,v in proof.items() if k!='eligible_ids'}
        target=root/'half.spv'
        subprocess.run(['spirv-opt','--convert-relaxed-to-half','--simplify-instructions',
                        '--redundancy-elimination','--eliminate-dead-code-aggressive',
                        str(color),'-o',str(target)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(target)],check=True,capture_output=True)
        def capabilities(code):
            words=struct.unpack('<%dI'%(len(code)//4),code);at=5;result=set()
            while at<len(words):
                count=words[at]>>16;op=words[at]&65535
                if not count or at+count>len(words): raise ValueError('malformed FP16 output')
                if op==17: result.add(words[at+1])
                at+=count
            return result
        code=target.read_bytes()
        added=capabilities(code)-capabilities(source.read_bytes())
        if not added<={9}: raise ValueError('FP16 pass added non-arithmetic capabilities')
        proof={k:v for k,v in proof.items() if k!='eligible_ids'}
        proof.update(explicit_float16=True,added_capabilities=sorted(added),
                     transfer_counts=transfer['counts'])
        return code,proof


def relax_sampled_results(code):
    """实验画质选项：仅放宽浮点采样结果，不改坐标、常量、地址或算术精度。
    Experimental quality option: relax only floating sampled texels, retaining
    coordinate, uniform, address and arithmetic precision. Not an upstream FP16
    recovery. Floating data textures/HDR need scene validation before promotion.
    """
    words = list(struct.unpack('<%dI' % (len(code)//4), code))
    instructions = []
    index = 5
    while index < len(words):
        count, op = words[index] >> 16, words[index] & 0xffff
        if not count or index + count > len(words): raise ValueError('invalid SPIR-V instruction')
        instructions.append((op, words[index:index+count]))
        index += count
    floating = {w[1] for op,w in instructions if op == 22 and w[2] == 32}  # OpTypeFloat
    floating |= {w[1] for op,w in instructions if op == 23 and w[2] in floating}  # OpTypeVector
    relaxed = {w[1] for op,w in instructions if op == 71 and w[2] == 0}
    results = {w[2] for op,w in instructions if 87 <= op <= 97 and w[1] in floating} - relaxed
    if not results: return code, 0
    # Preserve all IDs and instructions; insert only OpDecorate RelaxedPrecision.
    output = words[:5]
    inserted = False
    for op,w in instructions:
        if not inserted and 19 <= op <= 39:
            for result in sorted(results): output += [(3 << 16) | 71, result, 0]
            inserted = True
        output += w
    if not inserted: raise ValueError('SPIR-V has no type declarations')
    return struct.pack('<%dI' % len(output), *output), len(results)


def ballot_helper(code):
    """在原调用位置调用 ballot 辅助函数，规避旧 Adreno 编译器 UGPR 分配失败。
    Call a ballot helper at each original invocation point to avoid old Adreno compiler UGPR allocation failures.
    """
    words = list(struct.unpack('<%dI' % (len(code) // 4), code))
    instructions = []
    i = 5
    while i < len(words):
        count, op = words[i] >> 16, words[i] & 0xffff
        if not count or i + count > len(words):
            raise ValueError('invalid SPIR-V instruction')
        instructions.append((op, words[i:i+count]))
        i += count
    ballots = [w for op, w in instructions if op == 339]  # OpGroupNonUniformBallot
    if not ballots:
        return code
    bool_type = next(w[1] for op, w in instructions if op == 20)
    # 每个 (结果类型、执行 scope) 保留原值；谓词按值传递，不移动控制流或添加同步。
    # Preserve each (result type, execution scope); pass predicates by value without moving control flow or adding synchronization.
    signatures = {}
    next_id = words[3]
    for w in ballots:
        signature = (w[1], w[3])
        if signature not in signatures:
            signatures[signature] = tuple(range(next_id, next_id+5))
            next_id += 5
    declarations, functions = [], []
    for (result_type, scope), (typ, fun, arg, label, result) in signatures.items():
        declarations += [(4 << 16) | 33, typ, result_type, bool_type]  # OpTypeFunction
        functions += [(5 << 16) | 54, result_type, fun, 2, typ,  # OpFunction DontInline
                      (3 << 16) | 55, bool_type, arg,
                      (2 << 16) | 248, label,
                      (5 << 16) | 339, result_type, result, scope, arg,
                      (2 << 16) | 254, result,
                      (1 << 16) | 56]
    output = words[:5]
    output[3] = next_id
    declared = False
    for op, w in instructions:
        if op == 54 and not declared:
            output += declarations
            declared = True
        if op == 339:
            fun = signatures[(w[1], w[3])][1]
            output += [(5 << 16) | 57, w[1], w[2], fun, w[4]]  # OpFunctionCall
        else:
            output += w
    output += functions
    return struct.pack('<%dI' % len(output), *output)


def version(base, translator):
    """固定源码、翻译器与 SPIR-V 工具指纹。 | Pin source, translator and SPIR-V tool fingerprints."""
    h = hashlib.sha256(base.encode())
    h.update(Path(__file__).read_bytes())
    h.update((Path(__file__).resolve().parents[1] / 'vulkan' / 'nvn_vk_metadata.h').read_bytes())
    h.update(Path(__file__).with_name('dmsh_build.py').read_bytes())
    h.update(Path(__file__).with_name('dmsh_storage_bias.py').read_bytes())
    h.update(Path(__file__).with_name('dmsh_constants.py').read_bytes())
    for name in ('dmsh_float_transfers.py','dmsh_fragment_precision.py','dmsh_uniform_specialization.py','dmsh_uniform_predicates.py','dmsh_texel_ssbo.py','dmsh_driver_keys.py'):
        h.update(Path(__file__).with_name(name).read_bytes())
    h.update((Path(__file__).resolve().parents[2] / 'third_party/ryujinx/src/Ryujinx.Graphics.Shader/StructuredIr/AstOptimizer.cs').read_bytes())
    cross = subprocess.run(['spirv-cross', '--version'], capture_output=True)
    h.update(cross.stdout + cross.stderr)
    h.update(Path(translator).with_name('Ryujinx.Graphics.Shader.dll').read_bytes())
    h.update(json.dumps(compiler_options(), sort_keys=True).encode())
    for command in ('spirv-opt', 'spirv-val', 'spirv-as', 'spirv-dis', 'glslangValidator'):
        h.update(subprocess.check_output([command, '--version']))
    return h.hexdigest()[:12]


def cache_inputs_match(cached, chain, source_sha256, c1):
    """Check current inputs, including immutable constants outside code/control.

    The shader key fingerprints its control record and source_sha256 covers
    instruction bytes. Neither covers the full c1 payload. Validating the old
    metadata's own checksum cannot prove it still matches the current input.
    """
    return (cached.get('chain') == chain and
            cached.get('source_sha256') == source_sha256 and
            cached.get('c1') == c1.hex())


def descriptor_set(code, value):
    """仅改 OpDecorate DescriptorSet，绑定号与资源反射保持原样。 | Change only OpDecorate DescriptorSet, preserving bindings and resource reflection."""
    words = list(struct.unpack('<%dI' % (len(code) // 4), code))
    if words[0] != 0x07230203:
        raise ValueError('invalid SPIR-V magic')
    i = 5
    while i < len(words):
        count, op = words[i] >> 16, words[i] & 0xffff
        if not count or i + count > len(words):
            raise ValueError('invalid SPIR-V instruction')
        if op == 71 and count == 4 and words[i + 2] == 34:
            words[i + 3] = value
        i += count
    # 原翻译器对无格式图像漏声明读写能力；按实际指令补齐并由 spirv-val 与运行时特性双重检查。
    # The original translator omits formatless-image capabilities; add them from instructions, checked by spirv-val and runtime device features.
    instructions = []
    i = 5
    while i < len(words):
        count, op = words[i] >> 16, words[i] & 0xffff
        instructions.append((op, words[i:i+count]))
        i += count
    unknown_image = any(op == 25 and len(w) >= 9 and w[8] == 0 for op, w in instructions)
    capabilities = {w[1] for op, w in instructions if op == 17}
    add = []
    if unknown_image:
        if any(op == 98 for op, w in instructions) and 55 not in capabilities: add += [(2 << 16) | 17, 55]
        if any(op == 99 for op, w in instructions) and 56 not in capabilities: add += [(2 << 16) | 17, 56]
    words[5:5] = add
    return struct.pack('<%dI' % len(words), *words)


def readonly_buffers(code, reflection):
    """按原反射的 Write 位声明只读存储资源。 | Declare read-only storage resources using original reflection's Write bit."""
    words = list(struct.unpack('<%dI' % (len(code)//4), code))
    readonly = {b['binding'] for b in reflection['sbufs'] if not (b['flags'] & 1)}
    bindings = {}
    i = 5
    first_type = None
    while i < len(words):
        count, op = words[i] >> 16, words[i] & 0xffff
        if not count or i+count > len(words): raise ValueError('malformed SPIR-V')
        if op == 71 and count == 4 and words[i+2] == 33:
            bindings[words[i+1]] = words[i+3]
        if 19 <= op <= 39 and first_type is None: first_type = i
        i += count
    additions = []
    for variable, binding in bindings.items():
        if binding in readonly: additions += [(3 << 16) | 71, variable, 24]
    if additions:
        if first_type is None: raise ValueError('SPIR-V has no type section')
        words[first_type:first_type] = additions
    return struct.pack('<%dI' % len(words), *words)


def build(g):
    out = Path(g['out'])
    out.mkdir(parents=True, exist_ok=True)
    translator = g['XLATE']
    options = compiler_options()
    source_sha = subprocess.check_output([translator, '--source-sha'], text=True).strip()
    if source_sha != g['dmsh_chain'].xlate_source_sha():
        raise RuntimeError('shader translator source differs; rebuild dmsh_xlate')
    keys = sorted(g['want'] or g['loc'])
    packs = {}
    results = []
    with tempfile.TemporaryDirectory(prefix='dmsh_vk_') as temporary:
        jobs, constants = {}, {}
        for key in keys:
            name, k = g['loc'][key]
            if name.startswith('main.rodata@'):
                if 'main' not in packs:
                    import sys
                    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'verify'))
                    import nsoimg
                    packs['main'] = nsoimg.load(g['MAIN'])
                record = g['main_rec'](packs['main'], k)
            else:
                if name not in packs:
                    packs[name] = g['records'](Path(g['dm_dir'], name).read_bytes())
                record = packs[name][k]
            ci = record.find(bytes.fromhex('34127698'))
            if ci < 0 or hashlib.sha256(record[ci:ci+0x880]).hexdigest()[:16] != key:
                raise RuntimeError('shader package control hash mismatch: ' + key)
            jobs[key], constants[key] = g['job_for'](key, name, record, temporary)
            jobs[key]['host_subgroup_size'] = options['subgroup_size']
        workers = int(os.environ.get('J', '4'))
        pending = []
        for key in keys:
            info = out / (key + '.json')
            blob = out / (key + '.spv')
            code_hash = hashlib.sha256(Path(jobs[key]['code']).read_bytes()).hexdigest()
            jobs[key]['source_sha256'] = code_hash
            if info.is_file() and blob.is_file() and (out / (key + '.meta')).is_file():
                cached = json.loads(info.read_text())
                spec=out/(key+'.spec');spec_hash=cached.get('uniform_specialization',{}).get('specialization_sha256')
                spec_valid=(spec.is_file() and hashlib.sha256(spec.read_bytes()).hexdigest()==spec_hash) if spec_hash else not spec.exists()
                alt=out/(key+'.tssbo');alt_hash=cached.get('texel_storage_alternative',{}).get('pair_sha256')
                alt_valid=(alt.is_file() and hashlib.sha256(alt.read_bytes()).hexdigest()==alt_hash) if alt_hash else not alt.exists()
                pred=out/(key+'.pred');pred_hash=cached.get('uniform_predicates',{}).get('predicate_sha256')
                pred_valid=(pred.is_file() and hashlib.sha256(pred.read_bytes()).hexdigest()==pred_hash) if pred_hash else not pred.exists()
                dkey=out/(key+'.dkey');dkey_hash=cached.get('driver_keys',{}).get('sha256')
                dkey_valid=dkey.is_file() and hashlib.sha256(dkey.read_bytes()).hexdigest()==dkey_hash
                if dkey_valid and pred_valid and alt_valid and spec_valid and cache_inputs_match(cached,g['CHAIN'],code_hash,constants[key]) and cached.get('spirv_sha256') == hashlib.sha256(blob.read_bytes()).hexdigest() and cached.get('metadata_sha256') == hashlib.sha256((out / (key + '.meta')).read_bytes()).hexdigest():
                    results.append({'key': key, 'st': 'ok', 'cached': True})
                    continue
            pending.append(key)
        chunks = [pending[i::workers] for i in range(workers)]
        def translate(chunk):
            path = Path(temporary, chunk[0] + '.jobs.json')
            path.write_text(json.dumps([jobs[key] for key in chunk]))
            return subprocess.run([translator, str(path), temporary], capture_output=True, text=True)
        with concurrent.futures.ThreadPoolExecutor(workers) as executor:
            translated = list(executor.map(translate, [chunk for chunk in chunks if chunk]))
        for result in translated:
            if result.stderr:
                print(result.stderr, end='', flush=True)
        def finish_key(key):
            source = Path(temporary, key + '.spv')
            if not source.is_file():
                return {'key': key, 'st': 'failed', 'err': 'no SPIR-V'}
            stage = jobs[key]['stage']
            set_index = {'vs': 0, 'ps': 1, 'cs': 2, 'hs': 3, 'ds': 4, 'gs': 5}[stage]
            destination = out / (key + '.spv')
            reflection = json.loads(Path(temporary, key + '.res.json').read_text())
            code = readonly_buffers(descriptor_set(source.read_bytes(), set_index), reflection)
            if options['ballot_helper']:
                code = ballot_helper(code)
            if options['storage_bias']:
                shader_stage={'vs':0,'ps':1,'gs':2,'hs':3,'ds':4,'cs':5}[stage]
                slots={b[1]:b[2]+(16 if b[0]==8 else 0) for b in storage_records(reflection,shader_stage)}
                code,mask,reason=bias_storage_offsets(code,slots,shader_stage)
                reflection['storage_bias']={'mask':mask,'reason':reason}
            folds = {'scalar_loads':0,'vector_loads':0}
            if options['inline_c1']:
                binding=next((b['binding'] for b in reflection['cbufs'] if b['slot']==1),None)
                code,folds=inline_c1(code,binding,constants[key],set_index)
                reflection['constant_folding']=folds
            if options['relaxed_sampled_results'] and stage == 'ps':
                code, count = relax_sampled_results(code)
                reflection['relaxed_sampled_result_count'] = count
            destination.write_bytes(code)
            passes=['--eliminate-dead-code-aggressive','--eliminate-dead-const','--trim-capabilities']
            if any(folds.values()):
                passes=['--ssa-rewrite','--ccp','--simplify-instructions','--eliminate-dead-branches',*passes]
            if options['inline_functions']:
                # Adreno compiler null-dereferences on a valid compute entry
                # calling the translated body. Standard SPIRV-Tools inlining
                # avoids that form without changing arithmetic or bindings.
                # Explicit DontInline ballot helpers retain their workaround.
                passes=['--inline-entry-points-exhaustive',*passes]
            subprocess.run(['spirv-opt', *passes, str(destination), '-o', str(destination)], check=True, capture_output=True)
            if options['protected_color_half'] and stage == 'ps':
                code,proof=protected_color_half(destination)
                destination.write_bytes(code)
                reflection['protected_color_half']=proof
            validated = subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(destination)], capture_output=True, text=True)
            if validated.returncode:
                destination.unlink()
                return {'key': key, 'st': 'failed', 'err': validated.stderr[:400]}
            spec=out/(key+'.spec');spec.unlink(missing_ok=True)
            if options['uniform_specialization'] and stage=='ps':
                from dmsh_uniform_specialization import transform
                eligible={b['binding']:b['slot']for b in reflection['cbufs']if 3<=b['slot']<19}
                specialized,proof=transform(destination.read_bytes(),eligible)
                reflection['uniform_specialization']=proof
                if specialized is not None:spec.write_bytes(specialized)
            predicates=out/(key+'.pred');predicates.unlink(missing_ok=True)
            if options.get('uniform_predicates') and stage=='ps' and spec.exists():
                from dmsh_uniform_predicates import transform as predicate_transform
                eligible={b['binding']:b['slot']for b in reflection['cbufs']if 3<=b['slot']<19}
                payload,proof=predicate_transform(destination.read_bytes(),spec.read_bytes(),eligible)
                reflection['uniform_predicates']=proof
                if payload is not None:predicates.write_bytes(payload)
            alternate=out/(key+'.tssbo');alternate.unlink(missing_ok=True)
            if options['texel_storage_alternatives'] and stage=='ps':
                from dmsh_texel_ssbo import pair
                eligible={b['binding'] for b in reflection['textures'] if b['type']&0xff==2 and b['array']<=1}
                payload,proof=pair(destination.read_bytes(),spec.read_bytes()if spec.exists()else None,eligible,set_index,
                                   options.get('texel_storage_load_model','identity'))
                reflection['texel_storage_alternative']=proof
                if payload is not None:alternate.write_bytes(payload)
            # Retain the transformation proof; do not reload/lose its ABI tag.
            # 只为静态证明导出原 MSL；不编译 Metal，不改变 Vulkan 指令。
            # Export original MSL only for static facts, without compiling Metal
            # or changing Vulkan instructions. Unprovable facts retain LOAD.
            facts = {}
            if stage in ('vs', 'ps'):
                msl = subprocess.run(['spirv-cross', *g['XC'], str(source)], capture_output=True, text=True)
                if msl.returncode:
                    reflection['raster_error'] = msl.stderr[:400]
                else:
                    facts = g['raster_facts'](stage, msl.stdout)
                    reflection['raster_msl_sha256'] = hashlib.sha256(msl.stdout.encode()).hexdigest()
            reflection.update(source_sha256=jobs[key]['source_sha256'], chain=g['CHAIN'], descriptor_set=set_index, c1=constants[key].hex(), local=jobs[key].get('local', [1, 1, 1]), source=g['loc'][key], spirv_sha256=hashlib.sha256(destination.read_bytes()).hexdigest())
            reflection['raster'] = facts
            reflection['compiler_options'] = options
            write_metadata(out / (key + '.meta'), reflection)
            reflection['metadata_sha256'] = hashlib.sha256((out / (key + '.meta')).read_bytes()).hexdigest()
            from dmsh_driver_keys import emit as emit_driver_keys
            reflection['driver_keys']=emit_driver_keys(out,key,reflection)
            (out / (key + '.json')).write_text(json.dumps(reflection, ensure_ascii=False, indent=1))
            return {'key': key, 'st': 'ok'}
        with concurrent.futures.ThreadPoolExecutor(workers) as executor:
            results.extend(executor.map(finish_key, pending))
    results.extend(build_builtins(out))
    (out / '_vk_build.json').write_text(json.dumps(results, ensure_ascii=False, indent=1))
    (out / '_vk_manifest.json').write_text(json.dumps({'chain':g['CHAIN'],'expected':len(keys)+4,'validated':sum(r['st']=='ok' for r in results),'target':'vulkan1.1','toolchain':'original dmsh_xlate + spirv-opt capability trimming','compiler_options':options},indent=1))
    (out / '.chain').write_text(g['CHAIN']+'\n')
    ok = sum(r['st'] == 'ok' for r in results)
    print(f'Vulkan shaders: {ok}/{len(results)} validated; output={out}', flush=True)
    for r in results:
        if r['st'] != 'ok':
            print(r['key'], r['err'])
    return int(ok != len(results))


def storage_records(reflection, stage):
    records=[]
    for b in reflection['sbufs']:
        offset=b['cb_offset']*4
        base={0:0x110,1:0x510,5:0x310}.get(stage,-1)
        ubo_pointer=stage==5 and b['cb_slot']==0 and 0x210<=offset<0x310 and (offset-0x210)%16==0
        reg=(offset-0x210)//16 if ubo_pointer else (offset-base)//16 if base>=0 and base<=offset<base+256 and (offset-base)%16==0 else -1
        if reg<0:raise RuntimeError('unknown storage buffer mapping: '+str(b))
        records.append([8 if ubo_pointer else 2,b['binding'],reg,0,b['flags'],1,b['cb_slot'],0])
    return records


def write_metadata(path, reflection):
    """固定小端 ABI 供 C 运行时读，不依赖 JSON 库。 | Fixed little-endian ABI for the C runtime, without a JSON library."""
    stage = {'vs': 0, 'ps': 1, 'gs': 2, 'hs': 3, 'ds': 4, 'cs': 5}[reflection['stage']]
    bindings = []
    bindings.append([0, 0, 0, 0, 0, 1, 0, 0])
    for b in reflection['cbufs']:
        bindings.append([1, b['binding'], b['slot'], 0, 0, 1, 0, 0])
    bindings.extend(storage_records(reflection,stage))
    for name in ('textures', 'images'):
        for b in reflection[name]:
            handle = b['handle']
            first, second, kind = handle & 0x3fff, (handle >> 14) & 0x3fff, handle >> 28
            offsets = [first, second] if kind else [first]
            cb = b['cbuf_slot']
            cb0 = (cb & 0xffff) if cb >= 0 else 2
            cb1 = ((cb >> 16) - 1) if cb >= 0 and cb >> 16 else cb0
            def slot(base, stop, bank):
                return next(((o * 4 - base) // 8 for o in offsets if bank == 2 and base <= o * 4 < stop and (o * 4 - base) % 8 == 0), -1)
            texture = slot(0x168, 0x568, cb0)
            sampler = slot(0x568, 0x968, cb1)
            if name == 'images':
                resource_kind, reg = 4, slot(0x120, 0x168, cb0)
            elif texture < 0 and sampler >= 0:
                resource_kind, reg = 5, sampler
            elif b['separate']:
                resource_kind, reg = 6, texture
            else:
                resource_kind, reg = 3, texture
            if b['type'] & 0xff == 2: resource_kind = 9 if name == 'images' else 7
            bindings.append([resource_kind,b['binding'],reg,sampler,b['flags'],max(1,b['array']),b['cbuf_slot'],b['type']])
    c1 = bytes.fromhex(reflection['c1'])
    local = reflection.get('local') or [1, 1, 1]
    facts = reflection.get('raster', {})
    flags = int(facts.get('pos_w') == 'one') | (int(facts.get('discard', 1)) << 1) | (int(facts.get('sample_mask', 1)) << 2)
    color_mask = sum(1 << i for i in facts.get('colors', []))
    header = [0x324b5644, stage, reflection['descriptor_set'], len(bindings), len(c1), *local,
              facts.get('pos_attr', -1) & 0xffffffff, flags, color_mask, 1]
    bias=reflection.get('storage_bias',{}).get('mask',0)
    if bias:
        header[0]=0x334b5644
        header += [bias,64 if stage==1 else 0,32 if stage==5 else 16,1]
    path.write_bytes(struct.pack('<%dI'%len(header), *header) + b''.join(struct.pack('<8i', *b) for b in bindings) + c1)


def build_builtins(out):
    """原 nvn_builtin.h 的四个计数核，以 uvec2 做 64 位减法，支持无 shaderInt64 的手机。 | The four nvn_builtin.h counter kernels use uvec2 subtraction on phones without shaderInt64."""
    import re
    source = Path(__file__).resolve().parents[1] / 'nvn' / 'nvn_builtin.h'
    kernels = re.findall(r'\{"([0-9a-f]{16})", "(q_\w+)"\}', source.read_text())
    operations = {
        'q_val0': 'uint b=id*4; uvec2 v=uvec2(src[b],src[b+1]);',
        'q_val8': 'uint b=id*4+2; uvec2 v=uvec2(src[b],src[b+1]);',
        'q_dlt0': 'uint b=id*8; uvec2 a=uvec2(src[b+4],src[b+5]), z=uvec2(src[b],src[b+1]); uvec2 v=uvec2(a.x-z.x,a.y-z.y-uint(a.x<z.x));',
        'q_dlt8': 'uint b=id*8; uvec2 a=uvec2(src[b+6],src[b+7]), z=uvec2(src[b+2],src[b+3]); uvec2 v=uvec2(a.x-z.x,a.y-z.y-uint(a.x<z.x));',
    }
    result = []
    for key, name in kernels:
        glsl = ('#version 450\nlayout(local_size_x=32) in;\n'
                'layout(set=2,binding=1,std140) uniform Count { uint count; };\n'
                'layout(set=2,binding=2,std430) readonly buffer Src { uint src[]; };\n'
                'layout(set=2,binding=3,std430) writeonly buffer Dst { uint dst[]; };\n'
                'void main() { uint id=gl_GlobalInvocationID.x; if(id>=count) return; ' + operations[name] + ' dst[id*2]=v.x; dst[id*2+1]=v.y; }\n')
        with tempfile.TemporaryDirectory() as temp:
            code = Path(temp, 'builtin.comp'); code.write_text(glsl)
            subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(code),'-o',str(out / (key+'.spv'))],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(out / (key+'.spv'))],check=True)
        header = [0x324b5644,5,2,3,0,32,1,1,0xffffffff,6,0,1]
        bindings = [[1,1,3,0,0,1,0,0],[2,2,0,0,0,1,0,0],[2,3,1,0,1,1,0,0]]
        (out / (key+'.meta')).write_bytes(struct.pack('<12I',*header)+b''.join(struct.pack('<8i',*b) for b in bindings))
        result.append({'key':key,'st':'ok','builtin':name})
    return result


if __name__ == '__main__':
    import dmsh_chain
    translator = os.environ.get('XLATE', dmsh_chain.DM_ROOT + '/build/gfx/dmsh_xlate/dmsh_xlate')
    if len(sys.argv) == 2 and sys.argv[1] == '--version':
        print(version(dmsh_chain.version(), translator))
    else:
        raise SystemExit('Invoke through DMSH_TARGET=vulkan dmsh_build.py, or use --version')
