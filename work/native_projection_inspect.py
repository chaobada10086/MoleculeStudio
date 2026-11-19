import xml.etree.ElementTree as ET
from pathlib import Path
r=ET.parse('work/newman-tags-chemdraw.cdxml').getroot()
for e in r.iter():
    if e.tag in ('n','b','graphic','arrow'):
        print(e.tag, e.attrib, [t.get('Name') for t in e.findall('objecttag')])
