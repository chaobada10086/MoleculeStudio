from pathlib import Path
import xml.etree.ElementTree as ET
r=ET.fromstring(Path('work/newman-interop-review.cdxml').read_text());nextid=max(int(e.get('id','0')) for e in r.iter())+1
for e in list(r.iter()):
    for key,value in list(e.attrib.items()):
        if key.startswith('MoleculeStudioProjection'):
            ET.SubElement(e,'objecttag',{'id':str(nextid),'Name':key+'='+value});nextid+=1
Path('work/newman-with-tags.cdxml').write_text(ET.tostring(r,encoding='unicode'),encoding='utf-8')
