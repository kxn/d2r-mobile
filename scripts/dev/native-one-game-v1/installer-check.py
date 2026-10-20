#!/usr/bin/env python3
"""Offline selector negative controls; never starts an ADB command."""
import argparse,ast,importlib.util,json
from pathlib import Path
import xml.etree.ElementTree as E
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('installer_offline',HERE/'installer.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
checks=[]
def ok(name,test):assert test,name;checks.append(name)
pkg='com.android.packageinstaller'
def xml(label='D2R Mobile',checked='false',enabled='false',package=pkg,ack=True,parent=True,title=True):
 tree=E.Element('hierarchy');E.SubElement(tree,'node',package=package,text=label)
 if ack:E.SubElement(tree,'node',package=package,text='已了解应用的风险检测结果',attrib={'resource-id':pkg+':id/deleted_file_state_cb','checked':checked,'enabled':'true','bounds':'[100,100][200,200]'})
 if parent:
  n=E.SubElement(tree,'node',package=package,text='',attrib={'resource-id':'android:id/button1','enabled':enabled,'bounds':'[300,300][500,500]'})
  if title:E.SubElement(n,'node',package=package,text='继续安装',attrib={'resource-id':pkg+':id/vbutton_title','enabled':enabled,'bounds':'[350,350][450,450]'})
 elif title:E.SubElement(tree,'node',package=package,text='继续安装',attrib={'resource-id':pkg+':id/vbutton_title','enabled':enabled,'bounds':'[350,350][450,450]'})
 return E.tostring(tree,encoding='unicode')
a=m.actions(xml());ok('risk-ack-allowed-before-disabled-continue',len(a)==1 and a[0]['kind']=='risk-ack'and a[0]['tap']==[150,150])
a=m.actions(xml(checked='true',enabled='true'));ok('fresh-checked-continue-parent-selected-once',len(a)==1 and a[0]['kind']=='continue-install'and a[0]['tap']==[400,400])
ok('unchecked-checkbox-never-continues',all(a['kind']!='continue-install'for a in m.actions(xml(enabled='true'))))
ok('checked-but-disabled-button-no-action',not m.actions(xml(checked='true')))
ok('wrong-app-label-no-action',not m.actions(xml(label='Another App',enabled='true')))
ok('fixture-name-not-production-no-action',not m.actions(xml(label='D2R Native Window Family Fixture',enabled='true')))
ok('wrong-UI-package-no-action',not m.actions(xml(package='evil.package',enabled='true')))
ok('malformed-XML-no-action',not m.actions('<hierarchy>'))
ok('missing-install-button-no-action',not m.actions(xml(ack=False,parent=False,title=False,enabled='true')))
a=m.actions(xml(ack=False,parent=False,title=True,enabled='true'));ok('observed-vivo-title-fallback-allowed',len(a)==1 and a[0]['tap']==[400,400])
tree=E.fromstring(xml(checked='true',enabled='true'));E.SubElement(tree,'node',package=pkg,text='安装',attrib={'resource-id':'android:id/button1','enabled':'true','bounds':'[1,1][9,9]'})
ok('ambiguous-continue-buttons-no-action',not m.actions(E.tostring(tree,encoding='unicode')))
tree=E.fromstring(xml());E.SubElement(tree,'node',package=pkg,text='已了解应用的风险检测结果',attrib={'resource-id':pkg+':id/deleted_file_state_cb','checked':'false','enabled':'true','bounds':'[1,1][9,9]'})
ok('ambiguous-risk-checkboxes-no-action',not m.actions(E.tostring(tree,encoding='unicode')))
tree=E.fromstring(xml());next(n for n in tree.iter('node')if n.get('resource-id')==pkg+':id/deleted_file_state_cb').set('bounds','[-1,0][9,9]')
ok('invalid-bounds-no-action',not m.actions(E.tostring(tree,encoding='unicode')))
s=(HERE/'installer.py').read_text();calls=[n for n in ast.walk(ast.parse(s))if isinstance(n,ast.Call)and isinstance(n.func,ast.Attribute)and n.func.attr=='Popen'];ok('exact-one-Popen-no-retry',len(calls)==1)
ok('no-system-policy-changes',all(x not in s for x in ['settings put','force-stop','pm disable','kill-server']))
run=(HERE/'run-game.py').read_text();ok('unresolved-install-blocks-restore','require(not installer.UNRESOLVED' in run)
ok('tool-import-never-starts-client',not m.UNRESOLVED)
arg=argparse.ArgumentParser();arg.add_argument('--report',type=Path,required=True);a=arg.parse_args();a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps({'offline':True,'ADB_access':False,'count':len(checks),'checks':checks},indent=2)+'\n');print(len(checks),'installer offline controls passed')
