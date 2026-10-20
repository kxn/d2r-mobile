import ast,copy,json,re
from pathlib import Path
p=Path(__file__).with_name('run-panvk-layer-probe-fixture.py')
tree=ast.parse(p.read_text());f=next(n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name=='validate_result');ns={'re':re};exec(compile(ast.Module(body=[f],type_ignores=[]),str(p),'exec'),ns);validate=ns['validate_result']
config={'native_vulkan_app_configuration':True,'uid':10350,'pid':1234,'validation_layer':'requested','synchronization_validation':'requested'}
terminal={'native_vulkan_app_terminal':True,'pid':1234,'rc':0}
final={'driverID':20,'private_ICD_verified':True,'positive_GPU_bytes':True,'positive_VUID_count':0,'negative_VUID':'VUID-VkBufferCreateInfo-size-00912','negative_count':1,'invalid_call_blocked':True}
log='PanVK layered private ICD mapping verified path=private driverID=20\nGPU_MAPPING_IMPORT=off\n[vulkan validation] VUID-VkBufferCreateInfo-size-00912: expected\n'
assert validate(log,[config,terminal],final,'1234')==config
mutants=[('driverID',13),('private_ICD_verified',False),('positive_GPU_bytes',False),('positive_VUID_count',1),('negative_VUID','other'),('negative_count',0),('negative_count',2),('invalid_call_blocked',False)]
for k,v in mutants:
 q=copy.deepcopy(final);q[k]=v
 try:validate(log,[config,terminal],q,'1234')
 except AssertionError:pass
 else:raise AssertionError(('accepted bad final',k,v))
for changed in [log.replace('private ICD mapping verified','not verified'),log.replace('GPU_MAPPING_IMPORT=off','GPU_MAPPING_IMPORT=on'),log.replace('00912','00000'),log+'[vulkan validation] VUID-unexpected: bad\n']:
 try:validate(changed,[config,terminal],final,'1234')
 except AssertionError:pass
 else:raise AssertionError('accepted bad route/log')
for rows in [[config], [config,terminal,terminal], [{**config,'pid':1235},terminal], [{**config,'validation_layer':'disabled_explicitly'},terminal], [config,{**terminal,'rc':1}]]:
 try:validate(log,rows,final,'1234')
 except AssertionError:pass
 else:raise AssertionError('accepted bad configuration/terminal')
print(json.dumps({'test':'official-VVL-controller-acceptance','positive':1,'negative':17,'result':'passed','device_access':False}))
