"""Keep the production Store interpreter and its opcode admission mask closed."""
from pathlib import Path
import re
import unittest

VULKAN = Path(__file__).resolve().parents[1]


def code_only(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                  lambda m: ' ' * len(m[0]), text, flags=re.S)


def store_cases(text):
    text = code_only(text)
    match = re.search(r'static void store_index_op\([^;]*?\)\s*\{', text)
    if not match:
        raise AssertionError('production Store interpreter definition missing')
    begin, end, depth = match.end(), match.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    body = text[begin:end - 1]
    if not re.search(r'default\s*:\s*break\s*;\s*\}\s*$', body):
        raise AssertionError('Store default is no longer an empty dispatch')
    return set(re.findall(r'case\s+(OP_\w+)\s*:', body))


def admitted_cases(text):
    text = code_only(text)
    match = re.search(r'plan_properties\[OP_COUNT_\]\s*=\s*\{(.*?)\};', text, re.S)
    if not match:
        raise AssertionError('production opcode properties missing')
    return {name for name, expr in re.findall(
        r'\[(OP_\w+)\]\s*=\s*([^,}]+)', match[1])
        if 'PP_STORE' in re.findall(r'\b\w+\b', expr)}


def verify_contract(plan, store):
    active, admitted = store_cases(store), admitted_cases(plan)
    if active != admitted:
        raise AssertionError(f'Store mask mismatch: missing={sorted(active-admitted)}, '
                             f'extra={sorted(admitted-active)}')
    return active


class StoreOpcodePropertiesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plan = (VULKAN / 'nvn_vk_plan.c').read_text()
        cls.store = (VULKAN / 'nvn_vk_store_index.inc').read_text()

    def test_all_production_store_cases_are_admitted(self):
        self.assertTrue(verify_contract(self.plan, self.store))

    def test_new_effective_opcode_without_property_is_rejected(self):
        changed = self.store.replace('  default:break;',
                                     '  case OP_BIND_UBO:ctx->has_sc=1;break;\n'
                                     '  default:break;', 1)
        self.assertNotEqual(changed, self.store)
        with self.assertRaisesRegex(AssertionError, 'missing=.*OP_BIND_UBO'):
            verify_contract(self.plan, changed)

    def test_missing_existing_property_is_rejected(self):
        changed = self.plan.replace('[OP_SCISSOR]=PP_STORE', '[OP_SCISSOR]=0', 1)
        self.assertNotEqual(changed, self.plan)
        with self.assertRaisesRegex(AssertionError, 'missing=.*OP_SCISSOR'):
            verify_contract(changed, self.store)

    def test_nonempty_default_invalidates_filter_proof(self):
        changed = self.store.replace('  default:break;',
                                     '  default:ctx->has_sc=1;break;', 1)
        with self.assertRaisesRegex(AssertionError, 'empty dispatch'):
            verify_contract(self.plan, changed)


if __name__ == '__main__':
    unittest.main()
