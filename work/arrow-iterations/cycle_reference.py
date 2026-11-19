from pathlib import Path
import sys
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path('out/Studio-r62').resolve()))
import ai_assistant as ai
from ai_plugins import draw_plugin
draw_plugin.install_plugin(vars(ai))
panels=[]
def panel(title,states,steps):
    panels.append(dict(title=title,states=states.split('|'),steps=steps.split('|')))
panel('01   Heck · 烯烃芳基化','L₂Pd⁰|L₂Pd(Ar)X|LPd(Ar)(X)(η²-烯烃)|LPd(X)–CH₂CH(Ar)R|LPd(H)X','ArX 氧化加成|解离 L，配位 CH₂=CHR|同面迁移插入|顺式共平面 β-H 消除；释放 CH₂=C(Ar)R|碱捕获 HX，重新配位 L')
panel('02   Suzuki–Miyaura · 羟基钯路径','L₂Pd⁰|L₂Pd(Ar)X|L₂Pd(Ar)OH|cis-L₂Pd(Ar)R','ArX 氧化加成|OH⁻ 取代 X⁻|RB(OH)₂ 转金属，释放 B(OH)₃|还原消除，释放 Ar–R')
panel('03   Negishi · 有机锌转金属','L₂Pd⁰|L₂Pd(Ar)X|cis-L₂Pd(Ar)R','ArX 氧化加成|RZnX 转金属，形成 ZnX₂|还原消除，释放 Ar–R')
panel('04   Sonogashira · 钯主循环','L₂Pd⁰|L₂Pd(Ar)X|cis-L₂Pd(Ar)(C≡CR)','ArX 氧化加成|RC≡CCu 转金属，CuX 返回辅环|还原消除，释放 Ar–C≡C–R')
panel('04b   铜辅循环 · 与钯环共享炔基','CuX|CuX(η²-HC≡CR)|Cu–C≡CR','末端炔配位|胺碱协助脱质子，形成铵盐 BH⁺X⁻|向 Ar–Pd–X 转移炔基，回收 CuX')
panels.append(dict(title='读图说明',rows=[['符号','含义'],['L / X','中性配体 / 卤素'],['Ar / R','芳基 / 有机取代基'],['简化范围','仅示代表路径，未展开配体异构化'],['Heck','示一种插入取向，不代表唯一产物']]))
engine=ai.DrawingTools({})
result=engine.call('draw_poster',dict(title='钯催化偶联  |  四种代表性周转路径',columns=2,panels=panels))
assert result['cycle_lengths']==[5,4,3,3,3,0]
root=ET.Element('result',ok='true')
ET.SubElement(root,'actions').extend(engine.actions)
ET.ElementTree(root).write('work/arrow-iterations/cycles-reviewed.xml',encoding='utf-8')
