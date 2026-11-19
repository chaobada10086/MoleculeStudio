from pathlib import Path
import xml.etree.ElementTree as ET
p=Path('work/test-r7-final/converted-0-2.cdxml');r=ET.fromstring(p.read_text());ids={n.get('id') for n in r.iter('n') if n.get('MoleculeStudioProjection') in ('20','21')}
for n in r.iter('n'):
    if n.get('id') in ids:n.set('IgnoreWarnings','yes')
for b in r.iter('b'):
    if b.get('B') in ids and b.get('E') in ids:b.set('IgnoreWarnings','yes')
Path('work/newman-interop-review.cdxml').write_text(ET.tostring(r,encoding='unicode'),encoding='utf-8')
