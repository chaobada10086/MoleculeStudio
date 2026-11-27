"""Compile the same complete structures used by the reagent picker into typing/analysis data."""
import json,re,xml.etree.ElementTree as ET
from rdkit import Chem
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def build():
    records=json.loads((ROOT/'reagents.json').read_text(encoding='utf-8'))['reagents']
    from generate_reagents import records as source_records
    sources=source_records()
    if len(records)!=len(sources):raise ValueError('reagent source/catalog mismatch')
    rows=[]
    for record,source in zip(records,sources):
        frag=ET.parse(ROOT/'reagent_templates'/record.get('full_file',record['file'])).find('.//fragment')
        aliases=[a for a in record['alias'].split() if a.lower() not in {'reagent','acid','catalyst','base','hydride'}]
        if not aliases:continue
        mol=Chem.MolFromSmiles(source['smiles'],sanitize=False);mol.UpdatePropertyCache(strict=False)
        identity=Chem.MolToSmiles(mol)
        rows.append(dict(name=aliases[0],aliases='|'.join(aliases),title=record['title'],formula=record['formula'],identity=identity,
                         fragment=ET.tostring(frag,encoding='unicode'),file=record['file']))
    lines=['#pragma once','#include <string>','#include <map>','#include <vector>',
           'namespace compounddata {',
           'struct Entry {const char* name;const char* aliases;const char* title;const char* formula;const char* identity;const char* fragment;bool standalone;};',
           'static const Entry entries[]={', '#include "polyatomic_ion_entries.hpp"', '#include "reference_compounds.inc"']
    for r in rows:
        lines.append('{'+','.join(json.dumps(r[k],ensure_ascii=False) for k in ('name','aliases','title','formula','identity'))+',R"CMP('+r['fragment']+')CMP"},')
    hydrides=[]
    for r in rows:
        if r['name'] in {'MeOH','EtOH','nPrOH','iPrOH','tBuOH','BnOH','PhOH'}:
            frag=ET.fromstring(r['fragment']);nodes={n.get('id'):n for n in frag.findall('n')}
            oxygens=[n for n in nodes.values() if n.get('Element')=='8' and n.get('NumHydrogens')=='1' and not n.get('Charge')]
            if len(oxygens)==1:
                oxygen=oxygens[0];oxygen.set('NumHydrogens','0')
                endpoint=str(max(map(int,nodes))+1);x,y=map(float,oxygen.get('p').split())
                ET.SubElement(frag,'n',{'id':endpoint,'p':f'{x+22} {y}','NodeType':'ExternalConnectionPoint','NumHydrogens':'0'})
                bid=str(max([int(b.get('id')) for b in frag.findall('b')]+[1000])+1)
                ET.SubElement(frag,'b',{'id':bid,'B':oxygen.get('id'),'E':endpoint,'Order':'1'})
                hydrides.append((r['name'],r['name'][:-1],8,ET.tostring(frag,encoding='unicode')))
            continue
        if r['name'] not in {'Bu3SnH','Me3SnH','Ph3SnH','Et3SiH','Me3SiH','Ph3SiH'}:continue
        frag=ET.fromstring(r['fragment']);nodes={n.get('id'):n for n in frag.findall('n')}
        found=False
        for bond in frag.findall('b'):
            for a,b in [(bond.get('B'),bond.get('E')),(bond.get('E'),bond.get('B'))]:
                if nodes[a].get('Element')=='1' and nodes[b].get('Element') in {'14','50'}:
                    nodes[a].set('NodeType','ExternalConnectionPoint');nodes[a].attrib.pop('Element',None)
                    hydrides.append((r['name'],r['name'][:-1],int(nodes[b].get('Element')),ET.tostring(frag,encoding='unicode')));found=True;break
            if found:break
    lines+=['};','static const size_t count=sizeof(entries)/sizeof(entries[0]);',
      'inline std::string key(std::string s){for(const auto& p:std::vector<std::pair<std::string,std::string>>{{"⁻","-"},{"−","-"},{"⁺","+"},{"²","2"},{"³","3"},{"₀","0"},{"₁","1"},{"₂","2"},{"₃","3"},{"₄","4"},{"₅","5"},{"₆","6"},{"₇","7"},{"₈","8"},{"₉","9"}}){size_t i=0;while((i=s.find(p.first,i))!=std::string::npos){s.replace(i,p.first.size(),p.second);i+=p.second.size();}}std::string k;for(size_t i=0;i<s.size();++i){unsigned char c=s[i];if(c==\'-\'&&i+1<s.size()&&s.find_first_not_of("0123456789",i+1)!=std::string::npos)continue;k+=char(c>=\'A\'&&c<=\'Z\'?c+32:c);}return k;}',
      'struct Alias {const Entry* entry;std::string spelling;};',
      'inline const std::map<std::string,Alias>& index(){static const auto values=[](){std::map<std::string,Alias> m;for(const auto& e:entries){std::string all=e.aliases;for(size_t at=0;at<all.size();){size_t end=all.find(\'|\',at);if(end==std::string::npos)end=all.size();auto alias=all.substr(at,end-at),k=key(alias);auto i=m.find(k);if(i==m.end())m[k]={&e,alias};else if(i->second.entry&&std::string(i->second.entry->identity)!=e.identity)i->second={nullptr,""};at=end+1;}}return m;}();return values;}',
      'inline const Entry* find(const std::string& s){auto i=index().find(key(s));if(i==index().end()||!i->second.entry)return nullptr;if(s.size()>2&&s[0]>=\'A\'&&s[0]<=\'Z\'&&s[1]>=\'a\'&&s[1]<=\'z\'&&s.find_first_not_of("0123456789",2)==std::string::npos&&s!=i->second.spelling)return nullptr;return i->second.entry;}',
      'inline bool polyatomicIon(const Entry* entry){if(!entry)return false;for(const char* name:{"N3-","CN-","SCN-","OCN-","NO2-","NO3-","CO3-2","HCO3-","SO4-2","SO3-2","HSO4-","HSO3-","H2PO4-","HPO4-2","PO4-3","ClO-","ClO2-","ClO3-","ClO4-"})if(std::string(entry->name)==name)return true;return false;}\ninline int ionCharge(const Entry* entry){\n    if(!entry)return 0;std::string formula=entry->formula;auto at=formula.find_last_of("+-");\n    if(at==std::string::npos||formula.find_first_not_of("0123456789",at+1)!=std::string::npos)return 0;\n    int magnitude=at+1==formula.size()?1:std::atoi(formula.substr(at+1).c_str());return (formula[at]==\'+\'?1:-1)*magnitude;\n}\ninline const Entry* ion(const std::string& label,int charge=0){\n    auto e=find(label);if(polyatomicIon(e))return e;\n    if(charge){const auto suffix=(std::abs(charge)>1?std::to_string(std::abs(charge)):std::string())+(charge>0?"+":"-");e=find(label+suffix);if(polyatomicIon(e)&&ionCharge(e)==charge)return e;}\n    return nullptr;\n}\ninline std::string ionBody(const Entry& e){std::string name=e.name;auto at=name.find_last_of("+-");return at==std::string::npos?name:name.substr(0,at);}\n',
      'inline std::string canonical(const std::string& s){if(!find(s))return "";if(s.find("\\xE2\\x82")!=std::string::npos)return s;auto i=index().find(key(s));std::string name=i->second.entry->name;if(name.find("(Oi-Pr)")!=std::string::npos||name.find("(Ot-Bu)")!=std::string::npos)return name;if(name.size()>1&&name.compare(name.size()-2,2,"·")==0)return name;std::string aliases=i->second.entry->aliases;if(("|"+aliases+"|").find("|"+s+"|")!=std::string::npos)return s;auto spelling=i->second.spelling;if(s.find(\'-\')!=std::string::npos&&s.size()>1&&s.back()!=\'-\'){std::string out;size_t at=0;for(char c:s){if(c==\'-\')out+=c;else{while(at<spelling.size()&&spelling[at]==\'-\')++at;if(at<spelling.size())out+=spelling[at++];}}if(at==spelling.size())return out;}return spelling;}',
      'struct Hydride {const char* name;const char* base;int element;const char* fragment;};',
      'static const Hydride hydrides[]={']
    for name,base,element,fragment in hydrides:lines.append('{'+json.dumps(name)+','+json.dumps(base)+','+str(element)+',R"CMP('+fragment+')CMP"},')
    lines+=['};','inline const Hydride* hydride(const std::string& label){const auto* e=find(label);if(!e)return nullptr;for(const auto& h:hydrides)if(std::string(e->name)==h.name)return &h;return nullptr;}','}']
    (ROOT/'compound_catalog.hpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    docs=['# 内置常见化合物与缩写','',f'共 {len(rows)} 个完整化合物结构；与试剂模板、输入排版、分子式/分子量、展开/收起共用数据。','',
          '来源：[PubChem](https://pubchem.ncbi.nlm.nih.gov/)、[Hans Reich Named Reagents](https://organicchemistrydata.org/hansreich/resources/named_reagents/)、[Reich Acronyms](https://organicchemistrydata.org/hansreich/resources/acronyms/)。结构由本项目的 SMILES 定义生成；不从分子式猜测异构体。','',
          '|名称|缩写/别名|分子式|','|---|---|---|']
    docs += [f"|{r['title']}|{r['aliases'].replace('|',', ')}|{r['formula']}|" for r in rows]
    (ROOT.parent/'docs/COMMON_COMPOUNDS.md').write_text('\n'.join(docs)+'\n',encoding='utf-8')
    print('compiled',len(rows),'complete compounds')
if __name__=='__main__':build()
