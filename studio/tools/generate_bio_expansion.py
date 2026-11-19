"""Publish reviewed shared compounds into the native biomolecule picker."""
from pathlib import Path
import json,shutil
ROOT=Path(__file__).resolve().parents[1]

def build():
    path=ROOT/'template_expansion_202611.json'
    if not path.exists():return
    extra=json.loads(path.read_text(encoding='utf-8'))
    catalog_path=ROOT/'bio_library_catalog.json'
    catalog=[r for r in json.loads(catalog_path.read_text(encoding='utf-8')) if not r['file'].startswith('add202611_')]
    reagents={r['title']:r for r in json.loads((ROOT/'reagents.json').read_text(encoding='utf-8'))['reagents']}
    for record in extra:
        if not record.get('bio'):continue
        source=reagents[record['title']];key=record['alias'].split()[0].lower().replace('-','_')
        filename='add202611_'+key+'-flat.cdxml'
        shutil.copyfile(ROOT/'reagent_templates'/source['file'],ROOT/'bio_templates'/filename)
        catalog.append(dict(title=record['title']+' · 标准结构',category=record['category'],file=filename,key='add202611_'+key,
                            english=record['alias'],formula=record['formula'],smiles=record['smiles'],source=record['source'],existing=False))
    catalog_path.write_text(json.dumps(catalog,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    lines=['#pragma once','struct BioComponentEntry {','    std::string title,category,file,english,formula,source;',
           '    BioComponentEntry(std::string t,std::string c,std::string f,std::string e,std::string m,std::string s):title(t),category(c),file(f),english(e),formula(m),source(s){}','};',
           'const std::vector<BioComponentEntry>& bioComponents(){static const std::vector<BioComponentEntry> entries={']
    lines.extend('{'+','.join(json.dumps(r[k],ensure_ascii=False) for k in ('title','category','file','english','formula','source'))+'},' for r in catalog)
    lines+=['};return entries;}']
    (ROOT/'bio_catalog.hpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    lines=['#pragma once','// RDKit-validated formulas, in stable template order.','static const char* bioLibraryFormulas[]={']
    lines.extend(json.dumps(r['formula'])+', // '+r['file'] for r in catalog);lines+=['};']
    (ROOT/'bio_library_formulas.hpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print('biomolecule variants',len(catalog))

if __name__=='__main__':build()
