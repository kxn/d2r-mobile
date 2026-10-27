"""Rebuild the declared DSP1/DSP2 container for an equivalent base/payload.

This is structural packing, NOT selection, value specialization or a proof of
shader equivalence. The caller must prove both replacement modules equivalent
to their original modules. The original selector fields are preserved exactly;
no heuristic is run again and no opaque bytes are patched in place.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from dmsh_uniform_predicates import _old_recipe


def specialization_contract(code):
    """Use official reflection for the original producer's uint32 SpecId/default contract."""
    with tempfile.TemporaryDirectory(prefix='dmsh_spec_repack_') as temporary:
        path = Path(temporary)/'module.spv'; path.write_bytes(code)
        subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(path)], check=True, capture_output=True)
        info = json.loads(subprocess.check_output(['spirv-cross', str(path), '--reflect']))
    return sorted([{k:v for k,v in item.items() if k != 'variable_id'}
                   for item in info.get('specialization_constants', [])], key=lambda x:x['id'])


def rebuild_equivalent_recipe(original_base, original_recipe, equivalent_base, equivalent_payload):
    header = _old_recipe(original_base, original_recipe)
    original_payload = original_recipe[60:]
    if (len(equivalent_payload) < 20 or len(equivalent_payload) % 4 or
            struct.unpack_from('<I', equivalent_payload)[0] != 0x07230203):
        raise ValueError('Equivalent specialization payload is not complete SPIR-V')
    contract = specialization_contract(original_payload)
    if (len(contract) != 1 or contract[0].get('id') != header[5] or
            contract[0].get('type') != 'uint' or
            not isinstance(contract[0].get('default_value'), int) or
            not 0 <= contract[0]['default_value'] < 1 << 32):
        raise ValueError('Unsupported original selector specialization type; require original uint32 producer')
    if contract != specialization_contract(equivalent_payload):
        raise ValueError('Specialization IDs/type/default contract changed')
    # Same canonical packing used by dmsh_uniform_specialization.transform:
    # magic/version/binding/slot/offset/SpecId/word_count + base SHA + module.
    # Keep the six actual selection fields, including the integer-only flag.
    packed_header = struct.pack('<7I', *header[:6], len(equivalent_payload)//4)
    result = packed_header + hashlib.sha256(equivalent_base).digest() + equivalent_payload
    rebuilt = _old_recipe(equivalent_base, result)
    if rebuilt[:6] != header[:6]:
        raise ValueError('Original Uniform selector contract changed')
    return result, {'format': 'original DSP1/DSP2 seven-word header plus base SHA256 plus SPIR-V',
                    'original_header_selection_fields': list(header[:6]),
                    'rebuilt_header_selection_fields': list(rebuilt[:6]),
                    'original_base_sha256': hashlib.sha256(original_base).hexdigest(),
                    'original_recipe_sha256': hashlib.sha256(original_recipe).hexdigest(),
                    'original_payload_sha256': hashlib.sha256(original_payload).hexdigest(),
                    'equivalent_base_sha256': hashlib.sha256(equivalent_base).hexdigest(),
                    'equivalent_payload_sha256': hashlib.sha256(equivalent_payload).hexdigest(),
                    'rebuilt_recipe_sha256': hashlib.sha256(result).hexdigest(),
                    'specialization_contract': specialization_contract(equivalent_payload),
                    'selector_reselected': False,
                    'equivalence_authority': 'caller must provide separate exact proofs for base and payload'}
