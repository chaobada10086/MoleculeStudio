"""Export the native ring tables for the AI bridge; never maintain a second list."""
from pathlib import Path
import hashlib,json,re
root=Path(__file__).resolve().parents[1]
source=root/'ring_library_data.hpp'
def rows(kind,name):
    body=re.search(r'static const '+kind+r' '+name+r'\[\]=\{(.*?)\n\};',source.read_text(encoding='utf-8'),re.S).group(1)
    return [json.loads('['+line.strip().rstrip(',')[1:-1]+']') for line in body.splitlines() if line.strip().startswith('{')]
def generate():
    atoms,bonds,variants,parents=rows('Atom','atoms'),rows('Bond','bonds'),rows('Variant','variants'),rows('Parent','parents')
    entries=[]
    for i,v in enumerate(variants):
        parent,title,formula,smiles,first_a,na,first_b,nb=v;p=parents[parent]
        entries.append(dict(id=p[0]+':'+str(i-p[5]),index=i,name=p[1],english=p[2],aliases=p[3],category=p[4],variant=title,formula=formula,smiles=smiles,atoms=atoms[first_a:first_a+na],bonds=bonds[first_b:first_b+nb]))
    data=dict(schema=1,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),parents=len(parents),variants=entries)
    (root/'ai_ring_catalog.json').write_text(json.dumps(data,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
if __name__=='__main__':generate()
