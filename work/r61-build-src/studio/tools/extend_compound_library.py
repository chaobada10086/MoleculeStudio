"""Authored common small molecules; merge through the shared reagent generator."""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
DATA = """
甲烷|CH4|常见小分子|C
乙烷|C2H6|常见小分子|CC
乙烯|C2H4 ethylene|常见小分子|C=C
乙炔|C2H2 acetylene|常见小分子|C#C
氧气|O2|常见小分子|O=O
氮气|N2|常见小分子|N#N
丙烯|propene|常见小分子|CC=C
丙二烯|allene|常见小分子|C=C=C
1,3丁二烯|butadiene|常见小分子|C=CC=C
二氧化碳|CO2|常见小分子|O=C=O
硫酸铜五水合物|CuSO4.5H2O CuSO4*5H2O|常见无机盐|[Cu+2].[O-]S(=O)(=O)[O-].O.O.O.O.O
二硫化碳|CS2|常见小分子|S=C=S
一氧化碳|CO|常见小分子|[C-]#[O+]
水|H2O|常见小分子|O
重水|D2O|常见小分子|[2H]O[2H]
氨|NH3|常见小分子|N
肼|NH2NH2 N2H4|常见小分子|NN
羟胺|NH2OH|常见小分子|NO
磷化氢|PH3|常见小分子|P
硫化氢|H2S|常见小分子|S
二氧化硫|SO2|常见小分子|O=S=O
三氧化硫|SO3|常见小分子|O=S(=O)=O
一氧化二氮|N2O|常见小分子|[N-]=[N+]=O
一氧化氮|NO|常见小分子|[N]=O
二氧化氮|NO2|常见小分子|[O]N=O
氰化氢|HCN|常见小分子|C#N
硫氰酸|HSCN|常见小分子|SC#N
异氰酸|HNCO|常见小分子|N=C=O
羰基硫|COS OCS|常见小分子|O=C=S
二甲胺|Me2NH HNMe2|有机胺与膦|CNC
甲胺|MeNH2|有机胺与膦|CN
乙胺|EtNH2|有机胺与膦|CCN
二乙胺|Et2NH HNEt2|有机胺与膦|CCNCC
正丙胺|nPrNH2|有机胺与膦|CCCN
异丙胺|iPrNH2|有机胺与膦|CC(C)N
正丁胺|nBuNH2|有机胺与膦|CCCCN
叔丁胺|tBuNH2|有机胺与膦|CC(C)(C)N
三甲胺|Me3N NMe3|有机胺与膦|CN(C)C
三正丁胺|Bu3N NBu3|有机胺与膦|CCCCN(CCCC)CCCC
苯胺|PhNH2|有机胺与膦|Nc1ccccc1
苄胺|BnNH2|有机胺与膦|NCc1ccccc1
二苄胺|Bn2NH|有机胺与膦|N(Cc1ccccc1)Cc1ccccc1
三甲基膦|PMe3 Me3P|有机胺与膦|CP(C)C
三乙基膦|PEt3 Et3P|有机胺与膦|CCP(CC)CC
三正丁基膦|PBu3 Bu3P|有机胺与膦|CCCCP(CCCC)CCCC
亚磷酸三甲酯|P(OMe)3|有机胺与膦|COP(OC)OC
亚磷酸三乙酯|P(OEt)3|有机胺与膦|CCOP(OCC)OCC
甲醛|HCHO CH2O|醛酮与酸酐|C=O
乙醛|MeCHO CH3CHO|醛酮与酸酐|CC=O
丙醛|EtCHO|醛酮与酸酐|CCC=O
苯甲醛|PhCHO|醛酮与酸酐|O=Cc1ccccc1
丙酮|Me2CO acetone|溶剂与醇|CC(=O)C
丁酮|MEK EtCOMe|溶剂与醇|CCC(C)=O
二苯甲酮|Ph2CO|醛酮与酸酐|O=C(c1ccccc1)c1ccccc1
苯乙酮|PhCOMe|醛酮与酸酐|CC(=O)c1ccccc1
乙二醛|glyoxal|醛酮与酸酐|O=CC=O
乙酸酐|Ac2O|醛酮与酸酐|CC(=O)OC(C)=O
三氟乙酸酐|TFAA (CF3CO)2O|醛酮与酸酐|O=C(OC(=O)C(F)(F)F)C(F)(F)F
甲酸|HCOOH HCO2H|有机酸|OC=O
乙酸|AcOH CH3COOH MeCO2H|有机酸|CC(=O)O
丙酸|EtCO2H|有机酸|CCC(=O)O
苯甲酸|PhCO2H|有机酸|O=C(O)c1ccccc1
草酸|HO2CCO2H|有机酸|OC(=O)C(=O)O
甲磺酸|MsOH MeSO3H|有机酸|CS(=O)(=O)O
三氟甲磺酸|TfOH CF3SO3H|有机酸|OS(=O)(=O)C(F)(F)F
甲硫醇|MeSH|含硫小分子|CS
乙硫醇|EtSH|含硫小分子|CCS
苯硫酚|PhSH|含硫小分子|Sc1ccccc1
二甲硫醚|Me2S DMS|含硫小分子|CSC
二乙硫醚|Et2S|含硫小分子|CCSCC
二甲基二硫醚|MeSSMe DMDS|含硫小分子|CSSC
二苯基二硫醚|PhSSPh|含硫小分子|c1ccc(SSc2ccccc2)cc1
二甲基砜|Me2SO2|含硫小分子|CS(C)(=O)=O
硫脲|thiourea SC(NH2)2|含硫小分子|NC(N)=S
尿素|urea CO(NH2)2|常见小分子|NC(N)=O
乙腈|MeCN CH3CN|溶剂与醇|CC#N
丙腈|EtCN|溶剂与醇|CCC#N
苯甲腈|PhCN|溶剂与醇|N#Cc1ccccc1
丙二腈|CH2(CN)2|常见碳亲核试剂|N#CCC#N
硝基甲烷|MeNO2 CH3NO2|溶剂与醇|C[N+](=O)[O-]
硝基乙烷|EtNO2|常见碳亲核试剂|CC[N+](=O)[O-]
乙酸甲酯|MeOAc AcOMe|溶剂与醇|COC(C)=O
乙酸乙酯|EtOAc AcOEt|溶剂与醇|CCOC(C)=O
乙酸异丙酯|iPrOAc|溶剂与醇|CC(=O)OC(C)C
乙酸叔丁酯|tBuOAc|溶剂与醇|CC(=O)OC(C)(C)C
二氯甲烷|DCM CH2Cl2|溶剂与醇|ClCCl
氯仿|CHCl3|溶剂与醇|ClC(Cl)Cl
四氯化碳|CCl4|溶剂与醇|ClC(Cl)(Cl)Cl
二氯乙烷|DCE ClCH2CH2Cl|溶剂与醇|ClCCCl
氯苯|PhCl|溶剂与醇|Clc1ccccc1
溴苯|PhBr|芳基与烷基卤化物|Brc1ccccc1
碘苯|PhI|芳基与烷基卤化物|Ic1ccccc1
氟苯|PhF|芳基与烷基卤化物|Fc1ccccc1
苯|benzene C6H6|溶剂与醇|c1ccccc1
甲苯|PhMe toluene|溶剂与醇|Cc1ccccc1
均三甲苯|mesitylene|溶剂与醇|Cc1cc(C)cc(C)c1
环己烷|cHexH cyclohexane|溶剂与醇|C1CCCCC1
正己烷|nHexH hexane|溶剂与醇|CCCCCC
正戊烷|nPentH pentane|溶剂与醇|CCCCC
正庚烷|nHeptH heptane|溶剂与醇|CCCCCCC
异丙醇|iPrOH IPA|溶剂与醇|CC(O)C
正丙醇|nPrOH|溶剂与醇|CCCO
正丁醇|nBuOH|溶剂与醇|CCCCO
仲丁醇|sBuOH|溶剂与醇|CCC(C)O
异丁醇|iBuOH|溶剂与醇|CC(C)CO
苄醇|BnOH PhCH2OH|溶剂与醇|OCc1ccccc1
苯酚|PhOH|溶剂与醇|Oc1ccccc1
乙二醇|HOCH2CH2OH EG|溶剂与醇|OCCO
甘油|glycerol|溶剂与醇|OCC(O)CO
三氟乙醇|TFE CF3CH2OH|溶剂与醇|OCC(F)(F)F
六氟异丙醇|HFIP|溶剂与醇|OC(C(F)(F)F)C(F)(F)F
叔丁基甲醚|MTBE tBuOMe|溶剂与醇|COC(C)(C)C
二异丙醚|iPr2O DIPE|溶剂与醇|CC(C)OC(C)C
环戊基甲醚|CPME|溶剂与醇|COC1CCCC1
二甲氧基乙烷|DME glyme|溶剂与醇|COCCOC
二甘醇二甲醚|diglyme|溶剂与醇|COCCOCCOC
三甘醇二甲醚|triglyme|溶剂与醇|COCCOCCOCCOC
四甘醇二甲醚|tetraglyme|溶剂与醇|COCCOCCOCCOCCOC
二甲基甲酰胺|DMF HCONMe2|溶剂与醇|CN(C)C=O
二甲基乙酰胺|DMAc AcNMe2|溶剂与醇|CC(=O)N(C)C
N甲基吡咯烷酮|NMP|溶剂与醇|CN1CCCC1=O
1,3二甲基咪唑烷酮|DMI|溶剂与醇|CN1CCN(C)C1=O
二甲基丙烯脲|DMPU|溶剂与醇|CN1CCCN(C)C1=O
环丁砜|sulfolane|溶剂与醇|O=S1(=O)CCCC1
碳酸乙烯酯|EC ethylene-carbonate|溶剂与醇|O=C1OCCO1
乙酰氯|AcCl MeCOCl|酰化与活化|CC(=O)Cl
苯甲酰氯|BzCl PhCOCl|酰化与活化|O=C(Cl)c1ccccc1
草酰氯|(COCl)2|酰化与活化|O=C(Cl)C(=O)Cl
氯甲酸甲酯|ClCO2Me|酰化与活化|COC(=O)Cl
氯甲酸乙酯|ClCO2Et|酰化与活化|CCOC(=O)Cl
碳酸二甲酯|DMC (MeO)2CO|酰化与活化|COC(=O)OC
碳酸二乙酯|DEC (EtO)2CO|酰化与活化|CCOC(=O)OCC
甲磺酰氯|MsCl|酰化与活化|CS(=O)(=O)Cl
甲磺酸酐|Ms2O|酰化与活化|CS(=O)(=O)OS(C)(=O)=O
三氟甲磺酸酐|Tf2O|酰化与活化|O=S(=O)(OS(=O)(=O)C(F)(F)F)C(F)(F)F
甲基碘|MeI CH3I|芳基与烷基卤化物|CI
甲基溴|MeBr CH3Br|芳基与烷基卤化物|CBr
乙基碘|EtI|芳基与烷基卤化物|CCI
乙基溴|EtBr|芳基与烷基卤化物|CCBr
正丁基溴|nBuBr|芳基与烷基卤化物|CCCCBr
叔丁基溴|tBuBr|芳基与烷基卤化物|CC(C)(C)Br
苄溴|BnBr|芳基与烷基卤化物|BrCc1ccccc1
苄氯|BnCl|芳基与烷基卤化物|ClCc1ccccc1
烯丙基溴|allyl-Br|芳基与烷基卤化物|C=CCBr
溴代乙酸乙酯|BrCH2CO2Et|常见碳亲核试剂|CCOC(=O)CBr
丙二酸二乙酯|DEM EtO2CCH2CO2Et|常见碳亲核试剂|CCOC(=O)CC(=O)OCC
乙酰乙酸乙酯|EAA MeCOCH2CO2Et|常见碳亲核试剂|CCOC(=O)CC(C)=O
乙酰丙酮|acacH Hacac|常见碳亲核试剂|CC(=O)CC(C)=O
氰乙酸乙酯|NCCH2CO2Et|常见碳亲核试剂|CCOC(=O)CC#N
三甲基硅烷|Me3SiH|硅与锡试剂|C[SiH](C)C
苯硼酸|PhB(OH)2|硼试剂|OB(O)c1ccccc1
甲基硼酸|MeB(OH)2|硼试剂|CB(O)O
苯硼酸频哪醇酯|PhBpin|硼试剂|CC1(C)OB(c2ccccc2)OC1(C)C
苯乙烯|styrene PhCH=CH2|烯烃与炔烃|C=Cc1ccccc1
苯乙炔|PhCCH PhC#CH|烯烃与炔烃|C#Cc1ccccc1
三异丙基硅烷|iPr3SiH TIPS-H|硅与锡试剂|CC(C)[SiH](C(C)C)C(C)C
三苯基硅烷|Ph3SiH|硅与锡试剂|[SiH](c1ccccc1)(c1ccccc1)c1ccccc1
六甲基二硅烷|Me3SiSiMe3|硅与锡试剂|C[Si](C)(C)[Si](C)(C)C
六甲基二硅氧烷|Me3SiOSiMe3 HMDSO|硅与锡试剂|C[Si](C)(C)O[Si](C)(C)C
六甲基二硅氮烷|HMDS HN(SiMe3)2|硅与锡试剂|C[Si](C)(C)N[Si](C)(C)C
三甲基氢化锡|Me3SnH|硅与锡试剂|C[SnH](C)C
三苯基氢化锡|Ph3SnH|硅与锡试剂|[SnH](c1ccccc1)(c1ccccc1)c1ccccc1
三正丁基氯化锡|Bu3SnCl|硅与锡试剂|CCCC[Sn](Cl)(CCCC)CCCC
六正丁基二锡|Bu3SnSnBu3|硅与锡试剂|CCCC[Sn](CCCC)(CCCC)[Sn](CCCC)(CCCC)CCCC
三甲基氯化锡|Me3SnCl|硅与锡试剂|C[Sn](C)(C)Cl
甲基三苯基溴化膦|MePPh3Br|膦盐与叶立德|C[P+](c1ccccc1)(c1ccccc1)c1ccccc1.[Br-]
苄基三苯基氯化膦|BnPPh3Cl|膦盐与叶立德|[Cl-].c1ccc(C[P+](c2ccccc2)(c2ccccc2)c2ccccc2)cc1
甲氧羰基亚甲基三苯基膦|Ph3PCHCO2Me|膦盐与叶立德|COC(=O)C=P(c1ccccc1)(c1ccccc1)c1ccccc1
乙氧羰基亚甲基三苯基膦|Ph3PCHCO2Et|膦盐与叶立德|CCOC(=O)C=P(c1ccccc1)(c1ccccc1)c1ccccc1
氯化铵|NH4Cl|常见无机盐|[NH4+].[Cl-]
氯化钠|NaCl|常见无机盐|[Na+].[Cl-]
溴化钠|NaBr|常见无机盐|[Na+].[Br-]
氯化钾|KCl|常见无机盐|[K+].[Cl-]
溴化锂|LiBr|常见无机盐|[Li+].[Br-]
硫酸钠|Na2SO4|常见无机盐|[Na+].[Na+].[O-]S(=O)(=O)[O-]
硫酸镁|MgSO4|常见无机盐|[Mg+2].[O-]S(=O)(=O)[O-]
硫酸铜|CuSO4|常见无机盐|[Cu+2].[O-]S(=O)(=O)[O-]
硫代硫酸钠|Na2S2O3|常见无机盐|[Na+].[Na+].[S-]S(=O)(=O)[O-]
碳酸钙|CaCO3|常见无机盐|[Ca+2].[O-]C(=O)[O-]
硫酸氢钠|NaHSO4|常见无机盐|[Na+].OS(=O)(=O)[O-]
亚硫酸氢钠|NaHSO3|常见无机盐|[Na+].OS(=O)[O-]
氟化铯|CsF|常见无机盐|[Cs+].[F-]
氟化钠|NaF|常见无机盐|[Na+].[F-]
乙酸钠|NaOAc AcONa|常见无机盐|[Na+].CC(=O)[O-]
乙酸钾|KOAc AcOK|常见无机盐|[K+].CC(=O)[O-]
氰酸钾|KOCN|常见无机盐|[K+].[O-]C#N
硫氰酸钾|KSCN|常见无机盐|[K+].[S-]C#N
三丁基锡自由基|Bu3Sn· Bu3Sn• Bu3Sn. Bu3Sn*|自由基中间体|CCCC[Sn](CCCC)CCCC
三甲基锡自由基|Me3Sn· Me3Sn• Me3Sn. Me3Sn*|自由基中间体|C[Sn](C)C
三苯基锡自由基|Ph3Sn· Ph3Sn• Ph3Sn. Ph3Sn*|自由基中间体|[Sn](c1ccccc1)(c1ccccc1)c1ccccc1
三乙基硅自由基|Et3Si· Et3Si• Et3Si. Et3Si*|自由基中间体|CC[Si](CC)CC
三甲基硅自由基|Me3Si· Me3Si• Me3Si. Me3Si*|自由基中间体|C[Si](C)C
三苯基硅自由基|Ph3Si· Ph3Si• Ph3Si. Ph3Si*|自由基中间体|[Si](c1ccccc1)(c1ccccc1)c1ccccc1
"""
SPIN_TRAPS = r"""
二甲基吡咯啉氮氧化物|DMPO|自旋捕获剂与氮氧自由基|[O-][N+]1=CCCC1(C)C
苯基叔丁基硝酮|PBN|自旋捕获剂与氮氧自由基|[O-]/[N+](C(C)(C)C)=C\C1=CC=CC=C1
吡啶氧化物叔丁基硝酮|POBN 4-POBN|自旋捕获剂与氮氧自由基|[O-][N+]1=CC=C(/C=[N+]([O-])/C(C)(C)C)C=C1
二乙氧基磷酰基甲基吡咯啉氮氧化物|DEPMPO|自旋捕获剂与氮氧自由基|[O-][N+]1=CCCC1(P(OCC)(OCC)=O)C
叔丁氧羰基甲基吡咯啉氮氧化物|BMPO BocMPO|自旋捕获剂与氮氧自由基|CC1(CCC=[N+]1[O-])C(=O)OC(C)(C)C
4-羟基TEMPO自由基|TEMPOL 4-OH-TEMPO|自旋捕获剂与氮氧自由基|[O]N1C(C)(C)CC(O)CC1(C)C
4-氧代TEMPO自由基|TEMPONE 4-oxo-TEMPO|自旋捕获剂与氮氧自由基|O=C1CC(C)(C)N([O])C(C)(C)C1
4-羧基TEMPO自由基|4-COOH-TEMPO 4-carboxy-TEMPO|自旋捕获剂与氮氧自由基|CC1(C)N([O])C(C)(C)CC(C(O)=O)C1
"""
SPIN_SOURCES={
    'DMPO':'https://www.caymanchem.com/product/10006436/dmpo',
    'PBN':'https://www.caymanchem.com/product/15412/n-tert-butyl-alpha-phenylnitrone',
    'POBN':'https://www.caymanchem.com/product/20623/pobn',
    'DEPMPO':'https://www.caymanchem.com/product/10006435/depmpo',
    'BMPO':'https://pubchem.ncbi.nlm.nih.gov/compound/11241066',
    'TEMPOL':'https://www.caymanchem.com/product/27051/tempol',
    'TEMPONE':'https://www.caymanchem.com/product/14877/tempone',
    '4-COOH-TEMPO':'https://www.caymanchem.com/product/23139/4-carboxy-tempo',
}
rows=[]
for line in (DATA.strip()+'\n'+SPIN_TRAPS.strip()).splitlines():
    title,alias,category,smiles=line.split('|')
    inorganic=category=='常见无机盐' or (category=='常见小分子' and alias.split()[0] in {
        'CO2','CS2','CO','H2O','D2O','NH3','NH2NH2','NH2OH','PH3','H2S','SO2','SO3','N2O','NO','NO2','HCN','HSCN','HNCO','COS','O2','N2'})
    if category=='常见小分子' and not inorganic:category='常见有机小分子'
    rows.append(dict(title=title,alias=alias,category=category,smiles=smiles,
                     inorganic=inorganic,
                     source=SPIN_SOURCES.get(alias.split()[0],'https://pubchem.ncbi.nlm.nih.gov/')))
    if category=='自由基中间体':rows[-1]['radical_elements']=[50 if 'Sn' in alias else 14]
(ROOT/'library_additions.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('authored',len(rows),'common compounds')
