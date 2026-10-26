#!/usr/bin/env python3
"""核对原着色器加工链与每个产物，手机全量验哈希后切换缓存。
Verify the original shader chain and all artifacts, then publish only after
full on-device hash verification. Game modules and saves are never modified.
"""
import argparse
import concurrent.futures
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'dm/gfx/tools'))
import dmsh_chain
import dmsh_vulkan


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(value, message):
    if not value:
        raise SystemExit(message)


def verify_driver_artifacts(cache, key, reflection):
    """Optional DSP1 cannot authorize an unverified range or alter DDK1."""
    from dmsh_driver_keys import build_with_spans, SUFFIXES
    driver_blob, driver_proof, span_blob, span_proof = build_with_spans(
        {suffix:(cache/(key+'.'+suffix)).read_bytes() for suffix in SUFFIXES
         if (cache/(key+'.'+suffix)).is_file()},reflection.get('texel_storage_alternative'))
    require((cache/(key+'.dkey')).read_bytes()==driver_blob and
            reflection.get('driver_keys')=={'sha256':hashlib.sha256(driver_blob).hexdigest(),'proof':driver_proof},
            'driver read schema regeneration mismatch: '+key)
    span=cache/(key+'.dspan')
    if 'driver_spans' in reflection:
        require(span.is_file() and span.read_bytes()==span_blob and
                reflection['driver_spans']=={'sha256':hashlib.sha256(span_blob).hexdigest(),'proof':span_proof},
                'driver span regeneration mismatch: '+key)
        return True
    require(not span.exists(), 'undeclared driver span: '+key)
    return False


def verify_quad_traits(flags, stage, reflection, options, key):
    """Bind new wire traits to the explicit source profile and actual reflection.

    Native chains preserve their previous zero-trait contract. A readonly
    source trait is never authorized without the entry quad-transfer trait.
    """
    quad = reflection.get('uses_fragment_quad_transfer', False)
    readonly = reflection.get('uses_fragment_quad_readonly_sources', False)
    require(type(quad) is bool and type(readonly) is bool,
            'invalid quad reflection type: '+key)
    require((flags & 24) == ((int(quad)<<3) | (int(readonly)<<4)),
            'quad metadata/reflection mismatch: '+key)
    mode = options.get('fragment_subgroup_mode', 'native')
    require(not (flags & ~31) and (not readonly or quad),
            'invalid quad metadata traits: '+key)
    require(not quad or (stage == 1 and mode in ('quad_transfer_v1','quad_transfer_readonly_v2')),
            'quad trait outside fragment target profile: '+key)
    require(not readonly or mode == 'quad_transfer_readonly_v2',
            'readonly quad trait outside target profile: '+key)


def verify_fragment_profile_module(code, stage, options, key):
    if stage != 1 or options.get('fragment_subgroup_mode') not in ('quad_transfer_v1','quad_transfer_readonly_v2'):
        return
    require(len(code)>=20 and len(code)%4==0, 'malformed quad profile SPIR-V: '+key)
    words=struct.unpack('<%dI'%(len(code)//4),code)
    require(words[0]==0x07230203, 'invalid quad profile SPIR-V magic: '+key)
    at=5
    while at<len(words):
        count,opcode=words[at]>>16,words[at]&65535
        require(count and at+count<=len(words), 'malformed quad profile instruction: '+key)
        require(not 333<=opcode<=366, 'unsupported fragment subgroup instruction: '+key)
        at+=count


def verify_empty_pixel_local(blobs, reflection, payload, report_data, stage, descriptor_set):
    """An exact empty DPL cannot authorize a shader replacement.

    Check its complete artifact/module closure, but do not repeat eligibility
    analysis merely to prove that an optional optimization was omitted. Nonempty
    records always use the original independent regeneration below.
    """
    from dmsh_pixel_local_wire import actual_modules, MAGIC, VERSION, HEADER_BYTES, SUFFIXES
    from dmsh_pixel_local import VERSION as REFLECTION_VERSION
    declared = reflection.get('pixel_local_runtime', {})
    if type(declared.get('records')) is not int or declared['records'] != 0:
        return False
    require(len(payload) == HEADER_BYTES, 'empty pixel-local runtime size mismatch')
    header = struct.unpack_from('<8I', payload)
    require(header[:3] == (MAGIC, VERSION, HEADER_BYTES) and
            header[3] in (0, 1, 5) and header[4:] == (descriptor_set, 0, HEADER_BYTES, 0),
            'empty pixel-local runtime header mismatch')
    require('spv' in blobs and 'meta' in blobs and 0 <= stage < 6 and 0 <= descriptor_set < 6 and
            reflection.get('stage') == ('vs', 'ps', 'gs', 'hs', 'ds', 'cs')[stage] and
            reflection.get('descriptor_set') == descriptor_set,
            'empty pixel-local metadata stage/set mismatch')
    closure = b''.join(hashlib.sha256(blobs[s]).digest() if s in blobs else bytes(32) for s in SUFFIXES)
    require(payload[32:] == closure, 'empty pixel-local artifact closure mismatch')
    modules = actual_modules(blobs, reflection)
    module_hashes = {name: hashlib.sha256(code).hexdigest() for name, code in modules.items()}
    require(declared == {'schema': VERSION, 'sha256': hashlib.sha256(payload).hexdigest(),
                         'records': 0, 'source_modules': module_hashes},
            'empty pixel-local source module closure mismatch')
    require(reflection.get('pixel_local') == {
                'schema': REFLECTION_VERSION, 'sha256': hashlib.sha256(report_data).hexdigest()},
            'empty pixel-local reflection hash mismatch')
    report = json.loads(report_data)
    require(set(report) == {'schema', 'closure_verified', 'consumer_rule', 'artifacts', 'modules'} and
            report['schema'] == REFLECTION_VERSION and report['closure_verified'] is True and
            report['consumer_rule'] == 'exact module SHA only; do not reuse base proof for alternatives' and
            report['artifacts'] == {s: hashlib.sha256(code).hexdigest() for s, code in blobs.items()} and
            isinstance(report['modules'], dict) and set(report['modules']) == set(modules),
            'empty pixel-local reflection closure mismatch')
    for name, digest in module_hashes.items():
        entry = report['modules'][name]
        require(isinstance(entry, dict) and entry.get('schema') == REFLECTION_VERSION and
                entry.get('source_sha256') == digest,
                'empty pixel-local reflection source mismatch')
    base = report['modules']['base']
    source_stage = base.get('stage')
    # build() intentionally emits stage5 both for unsupported stages and for
    # rejected analysis with no stage. Many current VS/FS use that sentinel.
    # Neither case carries records or grants runtime authority.
    if source_stage is None:
        require(isinstance(base.get('rejected'), str) and header[3] == 5,
                'empty pixel-local rejected-stage sentinel mismatch')
    else:
        require(source_stage == ('Vertex', 'Fragment', 'Geometry', 'TessellationControl',
                                  'TessellationEvaluation', 'GLCompute')[stage] and
                header[3] == {'Vertex': 0, 'Fragment': 1}.get(source_stage, 5),
                'empty pixel-local source stage mismatch')
    return True


def verify_pixel_local(cache, output, key, reflection, stage, descriptor_set, options):
    """Keep nonempty runtime authorization on the full regeneration path."""
    if options.get('pixel_local_runtime') and options.get('pixel_local_reflection'):
        from dmsh_pixel_local_wire import SUFFIXES
        blobs = {s: (cache/(key+'.'+s)).read_bytes() for s in SUFFIXES
                 if (cache/(key+'.'+s)).is_file()}
        try:
            empty = verify_empty_pixel_local(blobs, reflection, (cache/(key+'.plocal')).read_bytes(),
                                            (cache/(key+'.plocal.json')).read_bytes(), stage, descriptor_set)
        except (SystemExit, ValueError, KeyError, TypeError, struct.error) as error:
            raise SystemExit('pixel-local empty closure invalid: '+key+': '+str(error)) from error
        if empty:
            return 'empty_fallback'
    if options.get('pixel_local_reflection'):
        from dmsh_pixel_local import emit as emit_pixel_local
        for suffix in ('spv', 'meta', 'spec', 'pred', 'tssbo'):
            target = output/(key+'.'+suffix); target.unlink(missing_ok=True)
            source = cache/(key+'.'+suffix)
            if source.is_file(): target.write_bytes(source.read_bytes())
        proof = emit_pixel_local(output, key, reflection)
        require(reflection.get('pixel_local') == proof and
                (output/(key+'.plocal.json')).read_bytes() == (cache/(key+'.plocal.json')).read_bytes(),
                'pixel-local reflection regeneration mismatch: '+key)
    else:
        require(not reflection.get('pixel_local'), 'undeclared pixel-local reflection: '+key)
    if options.get('pixel_local_runtime'):
        from dmsh_pixel_local_wire import build as build_pixel_local, SUFFIXES
        payload, proof = build_pixel_local({s: (cache/(key+'.'+s)).read_bytes() for s in SUFFIXES
            if (cache/(key+'.'+s)).is_file()}, reflection)
        require(reflection.get('pixel_local_runtime') == proof and
                (cache/(key+'.plocal')).read_bytes() == payload,
                'pixel-local runtime module regeneration mismatch: '+key)
    else:
        require(not reflection.get('pixel_local_runtime'), 'undeclared pixel-local runtime module: '+key)
    return 'regenerated' if options.get('pixel_local_reflection') else 'disabled'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, help='require identical SPIR-V; paired texel chains also require identical metadata/specialization')
    parser.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--deploy', action='store_true', help='game must already be saved and stopped')
    parser.add_argument('--verify-workers',type=int,choices=range(1,9),default=4,help='independent proof/tool verification workers; all checks retained')
    args = parser.parse_args()
    cache = args.cache.resolve()
    translator = Path(os.environ.get('XLATE', str(ROOT/'.local/build/dotnet/bin/dmsh_xlate/release/dmsh_xlate')))
    chain = dmsh_vulkan.version(dmsh_chain.version(), str(translator))
    require(cache.name == 'vk-' + chain and (cache/'.chain').read_text().strip() == chain,
            'cache does not match current source/tool fingerprints')
    manifest = json.loads((cache/'_vk_manifest.json').read_text())
    require(manifest == {'chain':chain, 'expected':6931, 'validated':6931, 'target':'vulkan1.1',
                        'toolchain':'original dmsh_xlate + spirv-opt capability trimming',
                        'compiler_options':dmsh_vulkan.compiler_options()}, 'incomplete shader manifest')
    results = json.loads((cache/'_vk_build.json').read_text())
    require(len(results) == 6931 and all(r.get('st') == 'ok' for r in results), 'incomplete build results')
    builtin_keys = set(re.findall(r'\{"([0-9a-f]{16})", "q_\w+"\}', (ROOT/'dm/gfx/nvn/nvn_builtin.h').read_text()))
    keys = {p.stem for p in cache.glob('*.spv')}
    require(len(keys) == 6931 and keys == {r['key'] for r in results}, 'shader key set mismatch')
    require(all(re.fullmatch('[0-9a-f]{16}', k) for k in keys), 'invalid shader key')
    require(keys == {p.stem for p in cache.glob('*.meta')}, 'metadata key set mismatch')
    require(keys-builtin_keys == {p.stem for p in cache.glob('*.json') if re.fullmatch('[0-9a-f]{16}',p.stem)},
            'reflection key set mismatch')
    expected_files = {'.chain','_vk_manifest.json','_vk_build.json'} | {
        key+suffix for key in keys for suffix in ['.spv','.meta']} | {
        key+suffix for key in keys-builtin_keys for suffix in ['.json','.dkey']}
    options=dmsh_vulkan.compiler_options()
    if options.get('quad_derivatives') or options.get('component_fold'):
        expected_files.add('_spirv_rewrites.json')
        rewrite=json.loads((cache/'_spirv_rewrites.json').read_text())
        source_root=ROOT/'dm/gfx/tools/spirv_rewrites'
        source_manifest=json.loads((source_root/'SOURCE.json').read_text())
        require(rewrite.get('sources')==source_manifest, 'SPIR-V rewrite provenance mismatch')
        require(all(sha(source_root/name)==digest for name,digest in source_manifest['files'].items()),
                'SPIR-V rewrite source changed from declared upstream')
        require(rewrite.get('compiler')==subprocess.check_output(['c++','--version'],text=True),
                'SPIR-V rewrite host compiler mismatch')
        require(rewrite.get('tool_sha256')==sha(cache.parent/('_spirv_rewrites-'+cache.name)),
                'SPIR-V rewrite executable hash mismatch')
    specialization_keys=set();texel_alternative_keys=set();predicate_keys=set()
    for key in keys-builtin_keys:
        reflection=json.loads((cache/(key+'.json')).read_text())
        if 'driver_spans' in reflection:
            expected_files.add(key+'.dspan')
        if reflection.get('pixel_local',{}).get('sha256'):
            expected_files.add(key+'.plocal.json')
        if reflection.get('pixel_local_runtime',{}).get('sha256'):
            expected_files.add(key+'.plocal')
        if reflection.get('uniform_specialization',{}).get('specialization_sha256'):
            specialization_keys.add(key);expected_files.add(key+'.spec')
        if reflection.get('uniform_predicates',{}).get('predicate_sha256'):
            predicate_keys.add(key);expected_files.add(key+'.pred')
        if reflection.get('texel_storage_alternative',{}).get('pair_sha256'):
            texel_alternative_keys.add(key);expected_files.add(key+'.tssbo')
    require({p.name for p in cache.iterdir()} == expected_files and
            all(p.is_file() and not p.is_symlink() for p in cache.iterdir()),
            'undeclared files or links in generated shader cache')
    upstream = json.loads((ROOT/'scripts/dev/upstream-source.json').read_text())
    revision = upstream['revision']
    # 原几何证明和静态事实提取器必须保持原实现；不新增 shader 键特判。
    # Preserve original geometry/static-fact proofs, without new shader-key exceptions.
    cover = ROOT/'dm/gfx/nvn/nvn_cover.h'
    require(cover.read_bytes() == subprocess.check_output(['git','show',revision+':dm/gfx/nvn/nvn_cover.h'],cwd=ROOT),
            'original geometry proof changed')
    def raster_ast(source):
        return ast.dump(next(n for n in ast.parse(source).body if isinstance(n,ast.FunctionDef) and n.name=='raster_facts'))
    original = subprocess.check_output(['git','show',revision+':dm/gfx/tools/dmsh_build.py'],cwd=ROOT,text=True)
    require(raster_ast(original) == raster_ast((ROOT/'dm/gfx/tools/dmsh_build.py').read_text()),
            'original raster fact extractor changed')
    facts = {'vertex':0, 'position_proven':0, 'fragment':0, 'raster_errors':0}
    pixel_local_verification = {'empty_fallback':0, 'regenerated':0, 'disabled':0}
    artifacts = {p.name: {'sha256':sha(p), 'bytes':p.stat().st_size} for p in sorted(cache.iterdir()) if p.is_file()}
    with tempfile.TemporaryDirectory(prefix='d2r-shader-verify-') as temporary:
        builtin_output = Path(temporary)
        builtins = dmsh_vulkan.build_builtins(builtin_output)
        require({r['key'] for r in builtins} == builtin_keys, 'builtin source mismatch')
        def verify_one(key):
            facts = {'vertex':0, 'position_proven':0, 'fragment':0, 'raster_errors':0}
            pixel_local_verification = {'empty_fallback':0, 'regenerated':0, 'disabled':0}
            # Proof generators use fixed scratch names: give each worker a
            # private directory instead of sharing files or relaxing checks.
            with tempfile.TemporaryDirectory(prefix='d2r-shader-entry-') as entry:
                output=Path(entry)
                meta = (cache/(key+'.meta')).read_bytes()
                require(len(meta)>=48, 'short shader metadata: '+key)
                h = struct.unpack('<12I', meta[:48])
                header_bytes=64 if h[0]==0x334b5644 else 48
                require(h[0] in (0x324b5644,0x334b5644) and h[1]<6 and h[2]<6 and h[3]<=128 and
                        len(meta)==header_bytes+h[3]*32+h[4] and h[8] in [0xffffffff,*range(16)] and
                        not (h[9]&~31) and h[10]<=255 and h[11]==1, 'invalid raster metadata: '+key)
                if header_bytes==64:
                    mask,offset,slots,version=struct.unpack_from('<4I',meta,48)
                    require(h[1] in (0,1,5) and mask and (h[1]==5 or mask<65536) and
                            offset==(64 if h[1]==1 else 0) and slots==(32 if h[1]==5 else 16) and version==1,
                            'invalid storage bias ABI: '+key)
                if key in builtin_keys:
                    require(all(sha(builtin_output/(key+s)) == artifacts[key+s]['sha256'] for s in ['.spv','.meta']),
                            'builtin regeneration mismatch: '+key)
                else:
                    reflection = json.loads((cache/(key+'.json')).read_text())
                    require(reflection['chain']==chain and reflection['key']==key and
                            reflection.get('compiler_options')==dmsh_vulkan.compiler_options() and
                            reflection['spirv_sha256']==artifacts[key+'.spv']['sha256'] and
                            reflection['metadata_sha256']==artifacts[key+'.meta']['sha256'], 'reflection hash mismatch: '+key)
                    verify_quad_traits(h[9],h[1],reflection,dmsh_vulkan.compiler_options(),key)
                    verify_fragment_profile_module((cache/(key+'.spv')).read_bytes(),h[1],dmsh_vulkan.compiler_options(),key)
                    # Recompute the complete proof from the actual shipped modules,
                    # not just its self-reported hash. Optional runtime failures keep
                    # full keys, but declared deployment artifacts must be exact.
                    verify_driver_artifacts(cache,key,reflection)
                    if key in specialization_keys:
                        proof=reflection['uniform_specialization'];recipe=(cache/(key+'.spec')).read_bytes()
                        require(sha(cache/(key+'.spec'))==proof['specialization_sha256'] and
                                proof['base_sha256']==artifacts[key+'.spv']['sha256'] and len(recipe)>=80,
                                'specialization artifact hash mismatch: '+key)
                        sh=struct.unpack_from('<7I',recipe)
                        require(sh==((0x32505344 if 'integer_only'in proof else 0x31505344),1|(256 if proof.get('integer_only')else 0),proof['binding'],proof['reg'],proof['offset'],proof['spec_id'],(len(recipe)-60)//4) and
                                3<=sh[3]<19 and sh[4]<=65532 and sh[4]%4==0 and
                                recipe[28:60]==bytes.fromhex(proof['base_sha256']), 'specialization recipe invalid: '+key)
                        verify=output/'verify-spec.spv';verify.write_bytes(recipe[60:])
                        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(verify)],check=True,capture_output=True)
                    if key in predicate_keys:
                        from dmsh_uniform_predicates import unpack as unpack_predicates
                        require(key in specialization_keys and h[1]==1 and dmsh_vulkan.compiler_options().get('uniform_predicates'),
                                'unexpected predicate sidecar: '+key)
                        module,predicates=unpack_predicates((cache/(key+'.pred')).read_bytes(),(cache/(key+'.spv')).read_bytes(),
                            (cache/(key+'.spec')).read_bytes(),reflection['uniform_predicates'])
                        verify=output/'verify-pred.spv';verify.write_bytes(module)
                        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(verify)],check=True,capture_output=True)
                    if key in texel_alternative_keys:
                        from dmsh_texel_ssbo import unpack_pair
                        alt,spec_alt,bindings=unpack_pair((cache/(key+'.tssbo')).read_bytes(),(cache/(key+'.spv')).read_bytes(),
                            (cache/(key+'.spec')).read_bytes()if key in specialization_keys else None,reflection['texel_storage_alternative'])
                        require(h[1]==1 and dmsh_vulkan.compiler_options()['texel_storage_alternatives'],'unexpected texel alternative: '+key)
                        require(reflection['texel_storage_alternative'].get('load_model','identity')==
                                dmsh_vulkan.compiler_options().get('texel_storage_load_model','identity'),
                                'texel alternative load-model mismatch: '+key)
                        for n,payload in enumerate([alt,spec_alt]):
                            if not payload:continue
                            verify=output/('verify-texel-'+str(n)+'.spv');verify.write_bytes(payload)
                            subprocess.run(['spirv-val','--target-env','vulkan1.1',str(verify)],check=True,capture_output=True)
                    # Verify every actual module closure. Rebuild all authorizing
                    # records; an empty fallback grants no replacement capability.
                    verification = verify_pixel_local(cache, output, key, reflection, h[1], h[2],
                                                      dmsh_vulkan.compiler_options())
                    pixel_local_verification[verification] += 1
                    dmsh_vulkan.write_metadata(output/'verify.meta', reflection)
                    require((output/'verify.meta').read_bytes()==meta, 'metadata regeneration mismatch: '+key)
                    raster = reflection.get('raster', {})
                    facts['vertex'] += reflection['stage']=='vs'
                    facts['fragment'] += reflection['stage']=='ps'
                    facts['position_proven'] += reflection['stage']=='vs' and raster.get('pos_attr',-1)>=0
                    facts['raster_errors'] += 'raster_error' in reflection
                if args.baseline:
                    require(sha(args.baseline/(key+'.spv'))==artifacts[key+'.spv']['sha256'], 'SPIR-V changed: '+key)
                    if dmsh_vulkan.compiler_options()['texel_storage_alternatives'] or dmsh_vulkan.compiler_options().get('uniform_predicates'):
                        require(sha(args.baseline/(key+'.meta'))==artifacts[key+'.meta']['sha256'],
                                'paired shader metadata changed: '+key)
                        reference_spec=args.baseline/(key+'.spec')
                        current_spec=cache/(key+'.spec')
                        require(reference_spec.exists()==current_spec.exists() and
                                (not current_spec.exists() or sha(reference_spec)==sha(current_spec)),
                                'paired shader specialization changed: '+key)
            return facts,pixel_local_verification
        with concurrent.futures.ThreadPoolExecutor(args.verify_workers) as pool:
            for index,(entry_facts,entry_pixel) in enumerate(pool.map(verify_one,sorted(keys)),1):
                for name,value in entry_facts.items():facts[name]+=value
                for name,value in entry_pixel.items():pixel_local_verification[name]+=value
                if index%1000==0 or index==len(keys):print(f'Verified {index}/{len(keys)} shader closures',flush=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    report = {'chain':chain, 'cache':str(cache), 'upstream':upstream, 'validated_game_and_builtin_shaders':len(keys),
              'raster':facts, 'original_proofs_unchanged':True,'specialization_recipes':len(specialization_keys),'texel_alternative_pairs':len(texel_alternative_keys),'predicate_sidecars':len(predicate_keys), 'baseline_spirv_identical':bool(args.baseline),
              'pixel_local_verification':pixel_local_verification, 'artifacts':artifacts, 'deployed':False}
    if args.deploy:
        package = 'com.d2rmobile.nativeport'
        base = ['adb','-s',args.serial]
        def adb(*parts,check=True):
            return subprocess.run([*base,*parts],capture_output=True,text=True,check=check)
        def stopped():
            require(not adb('shell','pidof',package,check=False).stdout.strip(),
                    'save and stop the game before deployment')
        stopped()
        token = uuid.uuid4().hex
        remote = '/data/local/tmp/d2r-shaders-'+token
        incoming = 'files/runtime/.shaders-'+token
        previous = 'files/runtime/.shaders-previous-'+token
        target = 'files/runtime/shaders'
        sums = args.report.with_suffix('.sha256')
        sums.write_text(''.join(a['sha256']+'  '+name+'\n' for name,a in artifacts.items()))
        adb('shell','mkdir','-p',remote)
        adb('push',str(cache)+'/.',remote+'/')
        adb('push',str(sums),remote+'/_deployment_sha256.txt')
        adb('shell','run-as',package,'mkdir','-p',incoming)
        adb('shell','run-as',package,'cp','-r',remote+'/.',incoming+'/')
        script = 'set -e; cd '+shlex.quote(incoming)+'; sha256sum -c _deployment_sha256.txt > _deployment_check.log'
        adb('shell','run-as '+package+' sh -c '+shlex.quote(script))
        stopped()
        # 完整验哈希后才发布；保留上个缓存，切换失败时恢复它。
        # Publish only after full hashes pass; retain the previous cache and
        # restore it if publication fails.
        script = ('set -e; if test -d '+target+'; then mv '+target+' '+previous+'; fi; '
                  'if ! mv '+incoming+' '+target+'; then if test -d '+previous+'; then mv '+previous+' '+target+'; fi; exit 1; fi')
        adb('shell','run-as '+package+' sh -c '+shlex.quote(script))
        adb('shell','rm','-r',remote)
        report.update(deployed=True, serial=args.serial, phone_verified_files=len(artifacts), previous=previous)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='artifacts'}),flush=True)


if __name__=='__main__':
    main()
