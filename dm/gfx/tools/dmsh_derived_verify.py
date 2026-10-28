"""Explicit verifier registry for the two declared verified-cache stages."""
import argparse
import hashlib
import importlib
import json
from pathlib import Path

REGISTRY={
    'dmsh-verified-cache-texel-representation-v1':'dmsh_derived_texel_storage',
    'dmsh-verified-cache-predicates-v1':'dmsh_derived_predicates',
    'dmsh-verified-cache-ubo-address-v1':'dmsh_derived_ubo_address',
    'dmsh-verified-cache-address-combined-v2':'dmsh_derived_address_combined',
}

def verify(cache,expected_input_sha,expected_shaders=6931):
    record=json.loads((cache/'_derived_build.json').read_text())
    schema=record.get('schema')
    if schema not in REGISTRY:raise ValueError('Unknown verified-cache-derived stage')
    module=importlib.import_module(REGISTRY[schema])
    result=module.verify(cache,expected_input_sha,expected_shaders)
    if result.get('status')!='passed-not-deployed':raise ValueError('Derived stage did not independently verify')
    path=Path(module.__file__).resolve()
    return {**result,'derived_schema':schema,'verifier_module':str(path),
        'verifier_module_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'verifier_registry_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cache',type=Path,required=True)
    p.add_argument('--input-receipt-sha256',required=True);p.add_argument('--report',type=Path,required=True)
    a=p.parse_args();result=verify(a.cache.resolve(),a.input_receipt_sha256)
    a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
