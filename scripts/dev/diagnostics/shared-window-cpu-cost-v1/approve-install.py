from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E,re
R=Path('/home/dl/d2r-mobile');mode=sys.argv[1];version='eddb4ca-cpu-cost-v1';assert mode in['install','restore']
a=[str(R/'.local/android-sdk/platform-tools/adb'),'-s','10CG570J59001U1'];remote='/data/local/tmp/d2r-turnip-wsi-'+mode+'-'+version+'.xml';local=R/'.local/logs/shared-import-windows-eddb4ca-cpu-cost-v1'/(mode+'-installer-auto.xml')
subprocess.run(a+['shell','uiautomator','dump',remote],check=True,timeout=20)
subprocess.run(a+['pull',remote,str(local)],check=True,timeout=20)
nodes=list(E.parse(local).iter('node'));assert any(n.get('text')=='D2R Mobile'for n in nodes)
for check in[True,False]:
 selected=[n for n in nodes if(n.get('resource-id')=='com.android.packageinstaller:id/deleted_file_state_cb'if check else n.get('resource-id')=='com.android.packageinstaller:id/vbutton_title'and n.get('text')=='继续安装')];assert len(selected)==1
 n=selected[0];assert n.get('package')=='com.android.packageinstaller'
 if check and n.get('checked')=='true':continue
 x1,y1,x2,y2=map(int,re.findall(r'\d+',n.get('bounds')))
 subprocess.run(a+['shell','input','tap',str((x1+x2)//2),str((y1+y2)//2)],check=True,timeout=10)
print('confirmed D2R installer, selected acknowledgement and continue')
