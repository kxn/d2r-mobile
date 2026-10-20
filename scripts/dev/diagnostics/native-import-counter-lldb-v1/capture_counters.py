"""One all-stop official LLDB read of four pre-existing globals; no target calls."""
import json
import os
from pathlib import Path
import lldb

NAMES=('native_allocation_count','native_allocation_bytes','native_range_count','native_range_capacity')
TYPES=('native_image_allocation','native_address_range')


def metadata(target):
    modules=[m for m in target.modules if m.GetFileSpec().GetFilename()=='libdmhost_android.so']
    if len(modules)!=1:
        raise ValueError('host module absent or ambiguous')
    module=modules[0]
    result={'module_UUID':module.GetUUIDString().replace('-','').lower(),'globals':{},'sizeof':{}}
    for name in NAMES:
        matches=module.FindGlobalVariables(target,name,2)
        if matches.GetSize()!=1:
            raise ValueError('missing/ambiguous global: '+name)
        v=matches.GetValueAtIndex(0)
        address=v.GetAddress()
        if not v.IsValid() or not address.IsValid() or v.GetByteSize()!=8:
            raise ValueError('global type/address unavailable: '+name)
        result['globals'][name]={'type':v.GetTypeName(),'size':8,'file_address':address.GetFileAddress(),'load_address':address.GetLoadAddress(target)}
    for name in TYPES:
        t=module.FindFirstType(name)
        if not t.IsValid() or not 0<t.GetByteSize()<4096:
            raise ValueError('metadata type unavailable: '+name)
        result['sizeof'][name]=t.GetByteSize()
    return result


def snapshot(debugger):
    output=Path(os.environ['DM_NATIVE_COUNTER_OUTPUT'])/'native-import-counters.json'
    if output.exists():
        raise ValueError('one-shot output already exists')
    target=debugger.GetSelectedTarget();process=target.GetProcess()
    row={'schema':1,'status':'INCONCLUSIVE_METADATA_OR_TOOL','PID':process.GetProcessID(),
         'ELF_sha256':os.environ['DM_NATIVE_COUNTER_ELF_SHA256'],'errors':[],
         'scope':'Import envelopes and metadata counts, NOT resident RAM/unique physical bytes. One all-stop snapshot may interrupt an update; not a transactional point or performance sample. Existing game-state pool/allocator data and official dumpsys meminfo remain separate.',
         'official_API':'SBModule.FindGlobalVariables/FindFirstType, SBAddress, SBProcess.ReadMemory; no inferior expressions/functions, breakpoints, payload/linked-list reads or custom timing.'}
    try:
        if process.GetState()!=lldb.eStateStopped or row['PID']!=int(os.environ['DM_NATIVE_COUNTER_PID']):
            raise ValueError('owned first PID not stopped')
        description=metadata(target)
        if description['module_UUID']!=os.environ['DM_NATIVE_COUNTER_BUILD_ID']:
            raise ValueError('actual loaded ELF UUID mismatch')
        row.update(description);values={}
        for name,info in description['globals'].items():
            error=lldb.SBError();data=process.ReadMemory(info['load_address'],8,error)
            if error.Fail() or len(data)!=8:
                raise ValueError('unreadable global: '+name+' '+str(error))
            values[name]=int.from_bytes(data,'little')
        row['values']=values
        row['derived_metadata_bytes']={
            'native_allocation_structs':values['native_allocation_count']*row['sizeof']['native_image_allocation'],
            'native_range_active_entries':values['native_range_count']*row['sizeof']['native_address_range'],
            'native_range_capacity_entries':values['native_range_capacity']*row['sizeof']['native_address_range']}
        row['bytes_read']=32
        row['status']='EXISTING_GLOBALS_READ_ONCE'
    except Exception as error:
        row['errors'].append(repr(error))
    output.write_text(json.dumps(row,indent=2)+'\n')
