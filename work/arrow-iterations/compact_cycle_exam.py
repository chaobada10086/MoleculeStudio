from pathlib import Path
import sys
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path('out/Studio-r62').resolve()))
import ai_assistant as ai
from ai_plugins import draw_plugin
draw_plugin.install_plugin(vars(ai))
engine=ai.DrawingTools({})
calls=[]


def cycle(title,rows,metal='Pd',coupling=None):
    nodes=[]
    for state,ligands,step,incoming,outgoing in rows:
        nodes.append(dict(metal=metal,oxidation=state,ligands=ligands.split(),step=step,incoming=incoming,outgoing=outgoing))
    args=dict(title=title,nodes=nodes,**({'coupling':coupling} if coupling else {}))
    calls.append(args)
    return engine.call('draw_catalytic_cycle',args)


start=('0','L L','氧化加成','Ar–X','')
cycle('01   Heck   /   烯烃芳基化',[
    start,('II','L L Ar X','配位取代','CH₂=CHR','L'),
    ('II','L Ar X η²-烯烃','同面迁移插入','',''),
    ('II','L X CH₂CH(Ar)R','顺式 β-H 消除','','CH₂=C(Ar)R'),
    ('II','L H X','碱辅助再生','B + L','BH⁺X⁻')])
cycle('02   Suzuki–Miyaura   /   羟基钯路径',[
    start,('II','L L Ar X','卤素置换','OH⁻','X⁻'),
    ('II','L L Ar OH','转金属','RB(OH)₂','B(OH)₃'),
    ('II','Ar R L L','还原消除','','Ar–R')])
cycle('03   Negishi   /   有机锌偶联',[
    start,('II','L L Ar X','转金属','RZnX','ZnX₂'),
    ('II','Ar R L L','还原消除','','Ar–R')])
cycle('04   Sonogashira   /   钯主环',[
    start,('II','L L Ar X','转金属','Cu–C≡CR','CuX'),
    ('II','Ar C≡CR L L','还原消除','','Ar–C≡CR')])
cycle('04b   铜辅环',[
    ('I','X','末端炔配位','HC≡CR',''),
    ('I','X η²-炔','碱辅助去质子化','B','BH⁺X⁻'),
    ('I','C≡CR','向钯转移炔基','','')],'Cu',dict(parent_edge=2,couple_edge=3))
root=ET.Element('result',ok='true')
ET.SubElement(root,'actions').extend(engine.actions)
assert len(root.findall('.//catalytic_cycle'))==5 and len(root.findall('.//metal'))==18
ET.ElementTree(root).write('work/arrow-iterations/compact-reviewed.xml',encoding='utf-8')
for filename,items in zip(('Heck','Suzuki','Negishi','Sonogashira'),([calls[0]],[calls[1]],[calls[2]],calls[3:])):
    single=ai.DrawingTools({})
    for args in items:single.call('draw_catalytic_cycle',args)
    root=ET.Element('result',ok='true')
    ET.SubElement(root,'actions').extend(single.actions)
    ET.ElementTree(root).write('work/arrow-iterations/compact-'+filename+'.xml',encoding='utf-8')
