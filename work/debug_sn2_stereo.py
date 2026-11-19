from pathlib import Path
import sys,xml.etree.ElementTree as ET
sys.path.insert(0,str(Path('out/Studio').resolve()))
from chemistry_open import one_molecule,identity
from rdkit import Chem,RDLogger
RDLogger.DisableLog('rdApp.warning')
p=Path('work/test-r13-projection/projection-mechanism-0-0-1-product.cdxml');tree=ET.fromstring(p.read_text());nodes={n.get('id'):n for n in tree.findall('.//page/fragment/n')};bonds=tree.findall('.//page/fragment/b')
o=next(n for n in nodes if nodes[n].get('Element')=='8');link=next(b for b in bonds if o in [b.get('B'),b.get('E')]);center=link.get('E') if link.get('B')==o else link.get('B')
for b in bonds:
    if center in (b.get('B'),b.get('E')):b.attrib.pop('Display',None)
for b in bonds:
    if center not in (b.get('B'),b.get('E')):continue
    begin,end=b.get('B'),b.get('E');b.set('B',center);b.set('E',end if begin==center else begin)
    for display in ('WedgeBegin','WedgedHashBegin'):
        b.set('Display',display);m=one_molecule(ET.tostring(tree,encoding='unicode'));print(b.get('B'),b.get('E'),display,identity(m))
    b.attrib.pop('Display',None);b.set('B',begin);b.set('E',end)
