"""Apply the shared branched alkyl projection to existing authored templates."""
from pathlib import Path
import json,re,sys
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from branched_alkyl_layout import xml_fragment,aibn_fragment


def normalize(raw):
    root=ET.fromstring(raw);count=0
    fragments=[root] if root.tag=='fragment' else list(root.findall('./page/fragment'))
    for fragment in fragments:count+=xml_fragment(fragment);count+=aibn_fragment(fragment)
    return (ET.tostring(root,encoding='unicode') if count else raw),count


def main():
    report={}
    for folder in ('reagent_templates','bio_templates'):
        for path in (ROOT/folder).glob('*.cdxml'):
            before=path.read_text(encoding='utf-8');after,count=normalize(before)
            if count:path.write_text(after,encoding='utf-8');report[str(path.relative_to(ROOT))]=count
    path=ROOT/'nicknames.json';data=json.loads(path.read_text(encoding='utf-8'))
    for name,raw in data.items():
        after,count=normalize(raw)
        if count:data[name]=after;report['nickname:'+name]=count
    if any(k.startswith('nickname:') for k in report):path.write_text(json.dumps(data,ensure_ascii=False,indent=1)+'\n',encoding='utf-8')
    for filename,tag in (('nickname_catalog.hpp','NICK'),('compound_catalog.hpp','CMP')):
        path=ROOT/filename;before=path.read_text(encoding='utf-8')
        def replace(match):
            raw=match.group(1);after,count=normalize(raw)
            if count:report[filename]=report.get(filename,0)+count
            return 'R"'+tag+'('+after+')'+tag+'"'
        after=re.sub('R"'+tag+r'\((.*?)\)'+tag+'"',replace,before,flags=re.S)
        if after!=before:path.write_text(after,encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
