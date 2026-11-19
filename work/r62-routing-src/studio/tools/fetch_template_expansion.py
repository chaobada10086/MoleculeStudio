"""Reviewed template candidates. Network refresh is explicit; builds use cached JSON."""
from pathlib import Path
import json,time,urllib.request,urllib.parse,concurrent.futures
from rdkit import Chem
from rdkit.Chem import rdMolDescriptors
ROOT=Path(__file__).resolve().parents[1]
# title | aliases (first token is a stable key) | category | exact CAS
DATA='''
三羟甲基氨基甲烷|Tris tromethamine THAM|生化缓冲剂|77-86-1
羟乙基哌嗪乙磺酸|HEPES|生化缓冲剂|7365-45-9
吗啉乙磺酸|MES|生化缓冲剂|4432-31-9
吗啉丙磺酸|MOPS|生化缓冲剂|1132-61-2
哌嗪二乙磺酸|PIPES|生化缓冲剂|5625-37-6
双羟乙基甘氨酸|Bicine|生化缓冲剂|150-25-4
三羟甲基甲基甘氨酸|Tricine|生化缓冲剂|5704-04-1
三羟甲基甲氨基丙磺酸|TAPS|生化缓冲剂|29915-38-6
环己氨基丙磺酸|CAPS|生化缓冲剂|1135-40-6
环己氨基乙磺酸|CHES|生化缓冲剂|103-47-9
双羟乙基氨基三羟甲基甲烷|Bis-Tris bisTris|生化缓冲剂|6976-37-0
(2R,3R)-二硫苏糖醇|DTT dithiothreitol|生化实验试剂|3483-12-3
三羧乙基膦盐酸盐|TCEP-HCl|生化实验试剂|51805-45-9
2-巯基乙醇|BME beta-mercaptoethanol|生化实验试剂|60-24-2
十二烷基硫酸钠|SDS sodium-dodecyl-sulfate|生化表面活性剂|151-21-3
CHAPS两性去污剂|CHAPS|生化表面活性剂|75621-03-3
正辛基β-D-吡喃葡萄糖苷|OctylGlc octyl-glucoside|生化表面活性剂|29836-26-8
异丙基β-D-硫代半乳糖苷|IPTG|生化实验试剂|367-93-1
邻硝基苯基β-D-半乳糖苷|ONPG|生化实验试剂|369-07-3
对硝基苯磷酸二钠|pNPP-Na2|生化实验试剂|4264-83-9
碘乙酰胺|IAM iodoacetamide|生化实验试剂|144-48-9
N-乙基马来酰亚胺|NEM|生化实验试剂|128-53-0
双琥珀酰亚胺基辛二酸酯|DSS disuccinimidyl-suberate|生化实验试剂|68528-80-3
双琥珀酰亚胺基二硫代丙酸酯|DSP-crosslinker|生化实验试剂|57757-57-0
L-抗坏血酸|AscorbicAcid vitamin-C|生化维生素与辅因子|50-81-7
烟酰胺|Nicotinamide niacinamide|生化维生素与辅因子|98-92-0
烟酸|NicotinicAcid niacin|生化维生素与辅因子|59-67-6
D-泛酸|PantothenicAcid vitamin-B5|生化维生素与辅因子|79-83-4
吡哆醇|Pyridoxine vitamin-B6|生化维生素与辅因子|65-23-6
吡哆醛|Pyridoxal|生化维生素与辅因子|66-72-8
叶酸|FolicAcid vitamin-B9|生化维生素与辅因子|59-30-3
R-硫辛酸|R-LipoicAcid|生化维生素与辅因子|(R)-lipoic acid
辅酶Q10|CoQ10 ubiquinone-10|生化维生素与辅因子|303-98-0
肌酸|Creatine|生化代谢物|57-00-1
肌酐|Creatinine|生化代谢物|60-27-5
L-肉碱|L-Carnitine|生化代谢物|541-15-1
乙酰L-肉碱|Acetyl-L-carnitine ALCAR|生化代谢物|3040-38-8
牛磺酸|Taurine|生化代谢物|107-35-7
甜菜碱|Betaine trimethylglycine|生化代谢物|107-43-7
胆碱阳离子|Choline|生化代谢物|62-49-7
乙醇胺|Ethanolamine|生化代谢物|141-43-5
亚精胺|Spermidine|生化代谢物|124-20-9
精胺|Spermine|生化代谢物|71-44-3
腐胺|Putrescine|生化代谢物|110-60-1
L-鸟氨酸|L-Ornithine Orn|生化非蛋白氨基酸|70-26-8
L-瓜氨酸|L-Citrulline Cit|生化非蛋白氨基酸|372-75-8
L-胱氨酸|L-Cystine|生化非蛋白氨基酸|56-89-3
肌氨酸|Sarcosine|生化非蛋白氨基酸|107-97-1
马尿酸|HippuricAcid|生化代谢物|495-69-2
尿酸|UricAcid|生化代谢物|69-93-2
黄嘌呤|Xanthine|生化代谢物|69-89-6
次黄嘌呤|Hypoxanthine|生化代谢物|68-94-0
尿苷二磷酸|UDP|生化核苷酸|58-98-0
胞苷二磷酸|CDP|生化核苷酸|63-38-7
肌苷单磷酸|IMP inosine-monophosphate|生化核苷酸|131-99-7
脱氧腺苷三磷酸|dATP|生化核苷酸|1927-31-7
脱氧胞苷三磷酸|dCTP|生化核苷酸|2056-98-6
脱氧鸟苷三磷酸|dGTP|生化核苷酸|2564-35-4
脱氧胸苷三磷酸|dTTP|生化核苷酸|365-08-2
UDP-α-D-葡萄糖|UDP-Glc UDP-glucose|生化核苷酸|133-89-1
S-腺苷L-同型半胱氨酸|SAH AdoHcy|生化维生素与辅因子|979-92-0
咖啡因|Caffeine|生化常用小分子|58-08-2
茶碱|Theophylline|生化常用小分子|58-55-9
可可碱|Theobromine|生化常用小分子|83-67-0
姜黄素|Curcumin|生化常用小分子|458-37-7
反式白藜芦醇|trans-Resveratrol|生化常用小分子|501-36-0
槲皮素|Quercetin|生化常用小分子|117-39-5
胆酸|CholicAcid|生化脂质与甾体|81-25-4
脱氧胆酸|DeoxycholicAcid|生化脂质与甾体|83-44-3
麦角甾醇|Ergosterol|生化脂质与甾体|57-87-4
睾酮|Testosterone|生化脂质与甾体|58-22-0
17β-雌二醇|Estradiol|生化脂质与甾体|50-28-2
孕酮|Progesterone|生化脂质与甾体|57-83-0
氢化可的松|Cortisol hydrocortisone|生化脂质与甾体|50-23-7
4-二甲氨基吡啶氮氧化物|DMAPO|酰化与活化|1005-31-8
4-吡咯烷基吡啶|PPY|酰化与活化|2456-81-7
乙二醇二甲基丙烯酸酯|EGDMA|聚合与材料试剂|97-90-5
甲基丙烯酸2-羟乙酯|HEMA|聚合与材料试剂|868-77-9
甲基丙烯酸缩水甘油酯|GMA|聚合与材料试剂|106-91-2
2-溴异丁酰溴|BiBB|聚合与材料试剂|20769-85-1
4-氰基-4-戊酸二硫代苯甲酸酯|CPADB|聚合与材料试剂|201611-92-9
香豆素|Coumarin|光化学与标记试剂|91-64-5
荧光素|Fluorescein|光化学与标记试剂|2321-07-5
曙红Y二钠盐|EosinY-Na2|光化学与标记试剂|17372-87-1
'''

def fetch(row):
    title,alias,category,cas=row.split('|');url='https://pubchem.ncbi.nlm.nih.gov/rest/pug/compound/name/'+urllib.parse.quote(cas,safe='')+'/property/MolecularFormula,IsomericSMILES,InChIKey,IUPACName/JSON'
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url,timeout=35) as response:props=json.load(response)['PropertyTable']['Properties']
            if len(props)!=1:raise ValueError('ambiguous CAS: '+str(len(props)))
            p=props[0];smiles=p.get('SMILES',p.get('IsomericSMILES'));mol=Chem.MolFromSmiles(smiles)
            if mol is None:raise ValueError('invalid structure')
            formula=rdMolDescriptors.CalcMolFormula(mol)
            if formula!=p['MolecularFormula']:raise ValueError('formula '+formula+' != '+p['MolecularFormula'])
            if Chem.MolToInchiKey(mol)!=p['InChIKey']:raise ValueError('InChIKey mismatch')
            return dict(title=title,alias=alias,category=category,query=cas,cid=p['CID'],formula=formula,smiles=smiles,inchikey=p['InChIKey'],iupac=p['IUPACName'],formal_charge=Chem.GetFormalCharge(mol),inorganic=False,bio=category.startswith('生化'),source='https://pubchem.ncbi.nlm.nih.gov/compound/'+str(p['CID']))
        except Exception as exc:
            if attempt==2:return dict(title=title,query=cas,error=str(exc))
            time.sleep(1+attempt)
        finally:time.sleep(.4)

if __name__=='__main__':
    rows=DATA.strip().splitlines()
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(fetch,rows))
    good=[r for r in results if 'error' not in r];bad=[r for r in results if 'error' in r]
    (ROOT/'template_expansion_202611.json').write_text(json.dumps(good,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (ROOT/'tools/template_expansion_failures.json').write_text(json.dumps(bad,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('fetched',len(good),'failed',len(bad),flush=True)
    for r in bad:print(r,flush=True)
