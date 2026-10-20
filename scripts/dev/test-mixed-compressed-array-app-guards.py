#!/usr/bin/env python3
"""Offline only: execute the exact row gate, never imports the ADB controller."""
import ast,copy
from pathlib import Path
p=Path(__file__).with_name('run-turnip-native-window-family-fixture-v1.py')
f=next(n for n in ast.parse(p.read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='validate_mixed_rows')
ns={};exec(compile(ast.Module(body=[f],type_ignores=[]),str(p),'exec'),ns)
gate=ns['validate_mixed_rows'];checks=0
existing=[{'test_details':'defined-BC3-array-mixed-ASTC-preservation','result':'passed','actual_ASTC_GPU':True,'original_layer0_preserved':True,'incoming_layer41_correct':True,'physical_RGBA_not_ASTC':True},{'test':'shared-import-windows','result':'passed','actual_memfd_heap':True,'observer_union':True}]
mixed=[{'test':'mixed-compressed-array','result':'passed','checks':4100,'actual_ASTC_GPU':True,'prior_BC_layers_mips_preserved':True,'ROI_only':True}]
for case,rows in [('existing-defined',existing),('mixed-array',mixed)]:
 gate(rows,case);checks+=1
 for i,row in enumerate(rows):
  for key,value in row.items():
   bad=copy.deepcopy(rows);bad[i][key]=False if value is True else 0 if isinstance(value,int) else 'wrong'
   try:gate(bad,case)
   except (AssertionError,KeyError):checks+=1
   else:raise AssertionError((case,key,'negative accepted'))
 for bad in [[],rows+copy.deepcopy(rows),mixed if case=='existing-defined' else existing]:
  try:gate(bad,case)
  except (AssertionError,KeyError):checks+=1
  else:raise AssertionError((case,'incomplete/duplicate/wrong-case accepted'))
print('exact row guards passed',checks,'offline only; no ADB')
