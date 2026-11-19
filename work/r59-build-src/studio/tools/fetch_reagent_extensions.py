"""Refresh a reviewed list of named synthesis reagents from PubChem PUG REST.

The checked-in JSON is the build input; release builds do not use the network.
Aliases denote individual compounds, rather than variable reagent families.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import time
from urllib.parse import quote
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
REICH = 'https://organicchemistrydata.org/hansreich/resources/named_reagents/'
# Some database deposits omit hydrides, use neutral acid + metal components,
# or resolve a CAS number to several incompatible metal representations.
# These reviewed formulations retain the stoichiometry and counterions.
CURATED = {
    '1075198-30-9': ('CCOC(=O)C(C#N)=NOC(=[N+](C)C)N1CCOCC1.F[P-](F)(F)(F)(F)F', 'C12H19F6N4O4P', 'https://www.thermofisher.com/order/catalog/product/es/es/439480050'),
    '3240-49-5': ('[Li+].CC(C)[N-]C1CCCCC1', 'C9H18LiN', REICH),
    '66319-03-5': ('O=C(Cl)c1c(Cl)cc(Cl)cc1Cl', 'C7H2Cl4O', REICH),
    '32005-36-0': ('O=C(/C=C/c1ccccc1)/C=C/c1ccccc1.O=C(/C=C/c1ccccc1)/C=C/c1ccccc1.[Pd]', 'C34H28O2Pd', REICH),
    '172222-30-9': ('Cl[Ru](Cl)(=Cc1ccccc1)(<-P(C1CCCCC1)(C2CCCCC2)C3CCCCC3)<-P(C4CCCCC4)(C5CCCCC5)C6CCCCC6', 'C43H72Cl2P2Ru', 'https://www.sigmaaldrich.com/US/en/product/aldrich/579726'),
    '67719-69-1': ('C[Al]1(C)Cl[Ti](=[CH2]1)(C2=CC=CC2)C3=CC=CC3', 'C13H18AlClTi', REICH),
    '14024-18-1': ('[Fe+3].CC([O-])=CC(=O)C.CC([O-])=CC(=O)C.CC([O-])=CC(=O)C', 'C15H21FeO6', REICH),
    '14248-17-8': ('O=S(=O)(N=[N+]=[N-])C(F)(F)F', 'CF3N3O2S', REICH),
    '16774-21-3': ('[Ce+4].[NH4+].[NH4+].O=[N+]([O-])[O-].O=[N+]([O-])[O-].O=[N+]([O-])[O-].O=[N+]([O-])[O-].O=[N+]([O-])[O-].O=[N+]([O-])[O-]', 'CeH8N8O18', REICH),
    '70693-62-8': ('[K+].[K+].[K+].[K+].[K+].OOS(=O)(=O)[O-].OOS(=O)(=O)[O-].OS(=O)(=O)[O-].[O-]S(=O)(=O)[O-]', 'H3K5O18S4', REICH),
    '114615-82-6': ('CCC[N+](CCC)(CCC)CCC.[O-][Ru](=O)(=O)=O', 'C12H28NO4Ru', REICH),
    '22560-16-3': ('[Li+].[BH-](CC)(CC)CC', 'C6H16BLi', REICH),
    '38721-52-7': ('[Li+].[BH-](C(C)CC)(C(C)CC)C(C)CC', 'C12H28BLi', REICH),
    '54575-49-4': ('[K+].[BH-](C(C)CC)(C(C)CC)C(C)CC', 'C12H28BK', REICH),
    '67276-04-4': ('[Na+].[BH-](C(C)CC)(C(C)CC)C(C)CC', 'C12H28BNa', REICH),
    '22722-98-1': ('[Na+].[AlH2-](OCCOC)OCCOC', 'C6H16AlNaO4', REICH),
    '13292-87-0': ('[BH3].CSC', 'C2H9BS', REICH),
    '25015-63-8': ('[BH]1OC(C)(C)C(C)(C)O1', 'C6H13BO2', REICH),
    '865-48-5': ('[Na+].CC(C)(C)[O-]', 'C4H9NaO', REICH),
    '90965-06-3': ('CC(=O)C(=[N+]=[N-])P(=O)(OC)OC', 'C5H9N2O4P', REICH),
    '1271-66-5': ('C[Ti](C)(C1=CC=CC1)C2=CC=CC2', 'C12H16Ti', REICH),
    '34946-82-2': ('[Cu+2].[O-]S(=O)(=O)C(F)(F)F.[O-]S(=O)(=O)C(F)(F)F', 'C2CuF6O6S2', 'https://www.sigmaaldrich.com/MS/en/product/aldrich/283673'),
    '144026-79-9': ('[Sc+3].[O-]S(=O)(=O)C(F)(F)F.[O-]S(=O)(=O)C(F)(F)F.[O-]S(=O)(=O)C(F)(F)F', 'C3F9O9S3Sc', REICH),
    '546-67-8': ('[Pb+4].CC(=O)[O-].CC(=O)[O-].CC(=O)[O-].CC(=O)[O-]', 'C8H12O8Pb', REICH),
    '90076-65-6': ('[Li+].O=S(=O)([N-]S(=O)(=O)C(F)(F)F)C(F)(F)F', 'C2F6LiNO4S2', REICH),
    '14694-95-2': ('Cl[Rh](<-P(c1ccccc1)(c2ccccc2)c3ccccc3)(<-P(c4ccccc4)(c5ccccc5)c6ccccc6)<-P(c7ccccc7)(c8ccccc8)c9ccccc9', 'C54H45ClP3Rh', 'https://www.sigmaaldrich.com/NL/en/product/aldrich/199982'),
    '39416-48-3': ('[nH+]1ccccc1.Br[Br-]Br', 'C5H6Br3N', REICH),
    '6638-79-5': ('CO[NH2+]C.[Cl-]', 'C2H8ClNO', REICH),
}
DATA = """
劳森试剂|Lawesson Lawessons reagent|硫化与脱水|19172-47-5
HATU 缩合试剂|HATU|缩合与活化|148893-10-1
HBTU 缩合试剂|HBTU|缩合与活化|94790-37-1
TBTU 缩合试剂|TBTU|缩合与活化|125700-67-6
HCTU 缩合试剂|HCTU|缩合与活化|330645-87-9
COMU 缩合试剂|COMU|缩合与活化|1075198-30-9
PyBOP 缩合试剂|PyBOP|缩合与活化|128625-52-5
BOP 缩合试剂|BOP|缩合与活化|56602-33-6
PyAOP 缩合试剂|PyAOP|缩合与活化|156311-83-0
PyBrOP 缩合试剂|PyBrOP|缩合与活化|132705-51-2
TFFH 氟化甲脒盐|TFFH|缩合与活化|164298-23-1
TCFH 氯化甲脒盐|TCFH|缩合与活化|94790-35-9
TSTU 缩合试剂|TSTU|缩合与活化|105832-38-0
HSTU 缩合试剂|HSTU|缩合与活化|265651-18-1
DMTMM 氯化三嗪盐|DMTMM|缩合与活化|3945-69-5
氰基羟基亚胺乙酸乙酯|OxymaPure Oxyma|缩合与活化|3849-21-6
羟基氮杂苯并三唑|HOAt|缩合与活化|39968-33-7
N羟基丁二酰亚胺|NHS HOSu|缩合与活化|6066-82-6
N羟基邻苯二甲酰亚胺|NHPI|缩合与活化|524-38-9
N,N二琥珀酰亚胺碳酸酯|DSC|缩合与活化|74124-79-1
2氯1甲基吡啶碘盐|CMPI Mukaiyama reagent|缩合与活化|14338-32-0
二苯基磷酰叠氮|DPPA|缩合与活化|26386-88-9
伯吉斯试剂|Burgess reagent|硫化与脱水|29684-56-8
马丁硫烷|Martin sulfurane|硫化与脱水|32133-82-7
伍林斯试剂|Woollins reagent|硫化与脱水|122039-27-4
三氟甲磺酸三甲基硅酯|TMSOTf TMS-triflate|卤化与保护|27607-77-8
碘代三甲基硅烷|TMSI|卤化与保护|16029-98-4
溴代三甲基硅烷|TMSBr|卤化与保护|2857-97-8
三乙基氯硅烷|TESCl|卤化与保护|994-30-9
三异丙基氯硅烷|TIPSCl|卤化与保护|13154-24-0
叔丁基二苯基氯硅烷|TBDPSCl|卤化与保护|58479-61-1
叔丁基二甲基硅基三氟甲磺酸酯|TBSOTf TBDMSOTf|卤化与保护|69739-34-0
三异丙基硅基三氟甲磺酸酯|TIPSOTf|卤化与保护|80522-42-5
三乙基硅基三氟甲磺酸酯|TESOTf|卤化与保护|79271-56-0
六甲基二硅氮烷|HMDS HN(SiMe3)2|卤化与保护|999-97-3
N,O双三甲基硅基乙酰胺|BSA-silylating|卤化与保护|10416-59-8
N,O双三甲基硅基三氟乙酰胺|BSTFA|卤化与保护|25561-30-2
N甲基N三甲基硅基三氟乙酰胺|MSTFA|卤化与保护|24589-78-4
三甲基硅基咪唑|TMS-imidazole TMSIm|卤化与保护|18156-74-6
甲氧基甲基氯|MOMCl|卤化与保护|107-30-2
2甲氧基乙氧基甲基氯|MEMCl|卤化与保护|3970-21-6
2三甲基硅基乙氧基甲基氯|SEMCl|卤化与保护|76513-69-4
对甲氧基苄氯|PMBCl|卤化与保护|824-94-2
苄氧羰基氯|CbzCl ZCl|卤化与保护|501-53-1
芴甲氧羰基氯|FmocCl|卤化与保护|28920-43-6
芴甲氧羰基琥珀酰亚胺酯|FmocOSu|卤化与保护|82911-69-1
烯丙氧羰基氯|AllocCl|卤化与保护|2937-50-0
二氢吡喃|DHP|卤化与保护|110-87-2
2,2二甲氧基丙烷|2,2-DMP|卤化与保护|77-76-9
三氟乙酸酐|TFAA|卤化与保护|407-25-0
邻硝基苯磺酰氯|oNsCl o-NsCl|卤化与保护|1694-92-4
对硝基苯磺酰氯|pNsCl p-NsCl|卤化与保护|98-74-8
丹磺酰氯|DansylCl DNSCl|卤化与保护|605-65-2
戴斯马丁氧化剂|DMP Dess-Martin|氧化试剂|87413-09-0
邻碘酰基苯甲酸|IBX|氧化试剂|61717-82-6
四丙基高钌酸铵|TPAP|氧化试剂|114615-82-6
过氧化二叔丁基|DTBP|氧化试剂|110-05-4
叔丁基过氧苯甲酸酯|TBPB|氧化试剂|614-45-9
过氧化二枯基|DCP|氧化试剂|80-43-3
N氟代双苯磺酰亚胺|NFSI|氟化与卤化|133745-75-2
选择性氟化试剂|Selectfluor F-TEDA-BF4|氟化与卤化|140681-55-6
德奥索氟试剂|Deoxo-Fluor|氟化与卤化|202289-38-1
石井氟化试剂|XtalFluor-E|氟化与卤化|63517-29-3
三氟甲基三甲基硅烷|TMSCF3 Ruppert-Prakash|氟化与卤化|81290-20-2
氯磺酰异氰酸酯|CSI|氟化与卤化|1189-71-5
三溴化吡啶盐|PyHBr3 pyridinium-tribromide|氟化与卤化|39416-48-3
N溴代乙酰胺|NBA|氟化与卤化|79-15-2
1,3二溴5,5二甲基海因|DBDMH|氟化与卤化|77-48-5
1,3二氯5,5二甲基海因|DCDMH|氟化与卤化|118-52-5
1,3二碘5,5二甲基海因|DIDMH|氟化与卤化|2232-12-4
溴代二甲基硫鎓溴盐|BDMS|氟化与卤化|50450-21-0
四丁基三溴化铵|TBATB|氟化与卤化|38932-80-8
三氯异氰尿酸|TCCA|氟化与卤化|87-90-1
二氯异氰尿酸钠|NaDCC|氟化与卤化|2893-78-9
三乙基硼氢化锂|LiEt3BH Super-Hydride|还原试剂|22560-16-3
三仲丁基硼氢化锂|L-Selectride|还原试剂|38721-52-7
三仲丁基硼氢化钾|K-Selectride|还原试剂|54575-49-4
三仲丁基硼氢化钠|N-Selectride|还原试剂|67276-04-4
双2甲氧基乙氧基氢化铝钠|Red-Al Vitride|还原试剂|22722-98-1
硼烷二甲硫醚|BH3-SMe2 BMS|还原试剂|13292-87-0
9硼杂双环壬烷|9-BBN 9-BBN-H|还原试剂|280-64-8
二叔戊基硼烷|Sia2BH disiamylborane|还原试剂|1069-54-1
儿茶酚硼烷|HBcat|还原试剂|274-07-7
频哪醇硼烷|HBpin|还原试剂|25015-63-8
苯基硅烷|PhSiH3|还原试剂|694-53-1
三三甲基硅基硅烷|TTMSS|还原试剂|1873-77-4
汉斯酯|Hantzsch ester HEH|还原试剂|1149-23-1
双频哪醇硼酸酯|B2pin2|硼试剂|73183-34-3
双新戊二醇硼酸酯|B2neop2|硼试剂|201733-56-4
四羟基二硼|B2(OH)4|硼试剂|13675-18-8
三甲氧基硼烷|B(OMe)3|硼试剂|121-43-7
三异丙氧基硼烷|B(OiPr)3|硼试剂|5419-55-6
苯硼酸|PhB(OH)2|硼试剂|98-80-6
苯基三氟硼酸钾|PhBF3K|硼试剂|153766-81-5
双三甲硅基氨基钾|KHMDS K[N(SiMe3)2]|有机碱|40949-94-8
四甲基哌啶锂|LiTMP|有机碱|38227-87-1
四甲基哌啶|TMP-base|有机碱|768-66-1
1,5,7三氮杂双环癸烯|TBD|有机碱|5807-14-7
7甲基三氮杂双环癸烯|MTBD|有机碱|84030-20-6
四甲基胍|TMG|有机碱|80-70-6
质子海绵|Proton-sponge|有机碱|20734-58-1
2,6二甲基吡啶|2,6-lutidine|有机碱|108-48-5
2,4,6三甲基吡啶|2,4,6-collidine|有机碱|108-75-8
2,6二叔丁基吡啶|DTBP-base|有机碱|585-48-8
1甲基咪唑|NMI|有机碱|616-47-7
N甲基吗啉|NMM|有机碱|109-02-4
叔丁醇钠|NaOtBu|有机碱|865-48-5
叔丁醇锂|LiOtBu|有机碱|1907-33-1
环己基异丙基氨基锂|LICA|有机碱|3240-49-5
樟脑磺酸|CSA|有机酸|5872-08-2
对甲苯磺酸吡啶盐|PPTS|有机酸|24057-28-1
三氟甲磺酸|TfOH HOTf|有机酸|1493-13-6
三氟甲磺酰亚胺|HNTf2 Tf2NH|有机酸|82113-65-3
三氯乙酸|TCA|有机酸|76-03-9
乙酰氯|AcCl|酰化与碳源|75-36-5
乙酸酐|Ac2O|酰化与碳源|108-24-7
苯甲酰氯|BzCl|酰化与碳源|98-88-4
特戊酰氯|PivCl|酰化与碳源|3282-30-2
山口试剂|TCBC Yamaguchi|酰化与碳源|66319-03-5
三光气|BTC triphosgene|酰化与碳源|32315-10-9
双光气|TCF diphosgene|酰化与碳源|503-38-8
异硫氰酸苯酯|PITC Edman-reagent|酰化与碳源|103-72-0
甲基乙烯基酮|MVK|酰化与碳源|78-94-4
丁炔二酸二甲酯|DMAD|酰化与碳源|762-42-5
丁炔二酸二乙酯|DEAD-alkyne DEADC|酰化与碳源|762-21-0
四氰基乙烯|TCNE|酰化与碳源|670-54-2
四氰基对苯醌二甲烷|TCNQ|酰化与碳源|1518-16-7
对甲苯磺酰甲基异腈|TosMIC|酰化与碳源|36635-61-7
贝斯特曼奥希拉试剂|Bestmann-Ohira|酰化与碳源|90965-06-3
吉尔伯特塞弗斯试剂|Seyferth-Gilbert|酰化与碳源|27491-70-9
曼德试剂|Mander reagent methyl-cyanoformate|酰化与碳源|17640-15-2
三甲基磷酰乙酸酯|TM-phosphonoacetate|酰化与碳源|5927-18-4
三乙基磷酰乙酸酯|TEPA triethyl-phosphonoacetate|酰化与碳源|867-13-0
乙氧羰基亚甲基三苯基膦|Ph3P=CHCO2Et|酰化与碳源|1099-45-2
甲氧羰基亚甲基三苯基膦|Ph3P=CHCO2Me|酰化与碳源|2605-67-6
甲基三苯基溴化膦|MePPh3Br|酰化与碳源|1779-49-3
乙基三苯基溴化膦|EtPPh3Br|酰化与碳源|1530-32-1
三甲基碘化锍|Me3SI|酰化与碳源|2181-42-2
三甲基碘化亚砜锍|Me3SOI|酰化与碳源|1774-47-6
三甲基氧鎓四氟硼酸盐|Me3OBF4 Meerwein-Me|酰化与碳源|420-37-1
三乙基氧鎓四氟硼酸盐|Et3OBF4 Meerwein-Et|酰化与碳源|368-39-8
N,O二甲基羟胺盐酸盐|Weinreb-amine MeONHMe-HCl|酰化与碳源|6638-79-5
三苯基氧化膦|OPPh3 Ph3PO|膦与配体|791-28-6
三丁基膦|PBu3 Bu3P|膦与配体|998-40-3
三环己基膦|PCy3|膦与配体|2622-14-2
三叔丁基膦|PtBu3|膦与配体|13716-12-6
三邻甲苯基膦|P(oTol)3|膦与配体|6163-58-2
双二苯基膦甲烷|dppm|膦与配体|2071-20-7
1,2双二苯基膦乙烷|dppe diphos|膦与配体|1663-45-2
1,3双二苯基膦丙烷|dppp|膦与配体|6737-42-4
1,4双二苯基膦丁烷|dppb|膦与配体|7688-25-7
1,1双二苯基膦二茂铁|dppf|膦与配体|12150-46-8
2,2双二苯基膦联萘|BINAP|膦与配体|98327-87-8
9,9二甲基双二苯基膦氧杂蒽|Xantphos|膦与配体|161265-03-8
4,5双二苯基膦二苯醚|DPEphos|膦与配体|166330-10-5
布赫瓦尔德配体|XPhos|膦与配体|564483-18-7
甲氧基联苯二环己基膦|SPhos|膦与配体|657408-07-6
二叔丁基膦联苯|JohnPhos|膦与配体|224311-51-7
二甲氨基联苯二环己基膦|DavePhos|膦与配体|213697-53-1
二异丙氧基联苯二环己基膦|RuPhos|膦与配体|787618-22-8
布雷特膦|BrettPhos|膦与配体|1070663-78-3
叔丁基布雷特膦|tBuBrettPhos|膦与配体|1160861-53-9
1,10菲咯啉|phen 1,10-phen|氮与氧配体|66-71-7
2,2联吡啶|bpy bipy|氮与氧配体|366-18-7
2,2,6,2三联吡啶|terpy terpyridine|氮与氧配体|1148-79-4
四甲基乙二胺|TMEDA|氮与氧配体|110-18-9
五甲基二乙烯三胺|PMDTA PMDETA|氮与氧配体|3030-47-5
环辛二烯|COD 1,5-COD|氮与氧配体|111-78-4
二亚苄基丙酮|dba|氮与氧配体|538-58-9
乙二胺四乙酸|EDTA|氮与氧配体|60-00-4
乙酰丙酮|Hacac acacH|氮与氧配体|123-54-6
12冠4|12-crown-4|氮与氧配体|294-93-9
15冠5|15-crown-5|氮与氧配体|33100-27-5
18冠6|18-crown-6|氮与氧配体|17455-13-9
二苯并18冠6|DB18C6|氮与氧配体|14187-32-7
乙酸钯|Pd(OAc)2|金属催化剂|3375-31-3
四三苯基膦钯|Pd(PPh3)4|金属催化剂|14221-01-3
双三苯基膦二氯化钯|PdCl2(PPh3)2|金属催化剂|13965-03-2
双二亚苄基丙酮钯|Pd(dba)2|金属催化剂|32005-36-0
三二亚苄基丙酮二钯|Pd2(dba)3|金属催化剂|51364-51-3
威尔金森催化剂|RhCl(PPh3)3 Wilkinson|金属催化剂|14694-95-2
格拉布斯一代催化剂|Grubbs-I Grubbs-1|金属催化剂|172222-30-9
格拉布斯二代催化剂|Grubbs-II Grubbs-2|金属催化剂|246047-72-3
霍维达格拉布斯二代催化剂|Hoveyda-Grubbs-II HG-II|金属催化剂|301224-40-8
施瓦茨试剂|Cp2ZrHCl Schwartz|金属催化剂|37342-97-5
特贝试剂|Tebbe reagent|金属催化剂|67719-69-1
彼塔西试剂|Cp2TiMe2 Petasis|金属催化剂|1271-66-5
乙酰丙酮铁|Fe(acac)3|金属催化剂|14024-18-1
乙酰丙酮铜|Cu(acac)2|金属催化剂|13395-16-9
三氟甲磺酸铜|Cu(OTf)2|金属催化剂|34946-82-2
三氟甲磺酸钪|Sc(OTf)3|金属催化剂|144026-79-9
三氟甲磺酸镱|Yb(OTf)3|金属催化剂|54761-04-5
三氟甲磺酸银|AgOTf|金属催化剂|2923-28-6
四乙腈六氟磷酸铜|Cu(MeCN)4PF6|金属催化剂|64443-05-6
四丁基溴化铵|TBAB|相转移与添加剂|1643-19-2
四丁基碘化铵|TBAI|相转移与添加剂|311-28-4
四丁基氯化铵|TBACl|相转移与添加剂|1112-67-0
四丁基氢氧化铵|TBAOH|相转移与添加剂|2052-49-5
四乙基溴化铵|TEAB|相转移与添加剂|71-91-0
苄基三乙基氯化铵|TEBA BTEAC|相转移与添加剂|56-37-1
十六烷基三甲基溴化铵|CTAB|相转移与添加剂|57-09-0
四苯基溴化膦|Ph4PBr|相转移与添加剂|2751-90-8
2,6二叔丁基4甲基苯酚|BHT|自由基与碳源|128-37-0
2,2偶氮双2,4二甲基戊腈|AMVN V-65|自由基与碳源|4419-11-8
1,1偶氮双环己烷甲腈|ABCN V-40|自由基与碳源|2094-98-6
N苯基三唑啉二酮|PTAD|自由基与碳源|4233-33-4
4甲基三唑啉二酮|MTAD|自由基与碳源|13274-43-6
N甲基N亚硝基对甲苯磺酰胺|Diazald|自由基与碳源|80-11-5
三氟甲磺酰叠氮|TfN3|自由基与碳源|14248-17-8
对甲苯磺酰叠氮|TsN3|自由基与碳源|941-55-9
对乙酰氨基苯磺酰叠氮|pABSA|自由基与碳源|2158-14-7
咪唑磺酰叠氮盐酸盐|ISA-HCl|自由基与碳源|952234-36-5
二叔丁基偶氮二甲酸酯|DBAD|自由基与碳源|870-50-8
1,1偶氮二甲酰二哌啶|ADDP|自由基与碳源|10465-81-3
三苯基甲烷醇|Ph3COH trityl-alcohol|常用辅助试剂|76-84-6
苄基三氯乙酰亚胺酸酯|Bn-TCAI|常用辅助试剂|81927-55-1
四氢呋喃|THF|溶剂与添加剂|109-99-9
2甲基四氢呋喃|2-MeTHF|溶剂与添加剂|96-47-9
二氯甲烷|DCM CH2Cl2|溶剂与添加剂|75-09-2
1,2二氯乙烷|DCE 1,2-DCE|溶剂与添加剂|107-06-2
N,N二甲基甲酰胺|DMF|溶剂与添加剂|68-12-2
N,N二甲基乙酰胺|DMAc|溶剂与添加剂|127-19-5
二甲基亚砜|DMSO|溶剂与添加剂|67-68-5
N甲基吡咯烷酮|NMP|溶剂与添加剂|872-50-4
二甲基丙烯脲|DMPU|溶剂与添加剂|7226-23-5
六甲基磷酰三胺|HMPA HMPT|溶剂与添加剂|680-31-9
1,2二甲氧基乙烷|DME glyme|溶剂与添加剂|110-71-4
二甘醇二甲醚|diglyme|溶剂与添加剂|111-96-6
甲基叔丁基醚|MTBE|溶剂与添加剂|1634-04-4
环戊基甲醚|CPME|溶剂与添加剂|5614-37-9
六氟异丙醇|HFIP|溶剂与添加剂|920-66-1
三氟乙醇|TFE|溶剂与添加剂|75-89-8
乙腈|MeCN ACN CH3CN|溶剂与添加剂|75-05-8
乙酸乙酯|EtOAc|溶剂与添加剂|141-78-6
乙酸异丙酯|iPrOAc|溶剂与添加剂|108-21-4
异丙醇|iPrOH IPA|溶剂与添加剂|67-63-0
叔丁醇|tBuOH|溶剂与添加剂|75-65-0
甲醇|MeOH|溶剂与添加剂|67-56-1
乙醇|EtOH|溶剂与添加剂|64-17-5
二乙醚|Et2O|溶剂与添加剂|60-29-7
1,4二氧六环|1,4-dioxane|溶剂与添加剂|123-91-1
二甲基碳酸酯|DMC|溶剂与添加剂|616-38-6
碳酸丙烯酯|propylene-carbonate|溶剂与添加剂|108-32-7
二氧化硫脲|TDO FAS|还原试剂|1758-73-2
异丙醇铝|Al(OiPr)3|还原试剂|555-31-7
四异丙氧基钛|Ti(OiPr)4 TTIP|金属催化剂|546-68-9
硝酸铈铵|CAN|无机氧化剂|16774-21-3
过一硫酸氢钾复合盐|Oxone|无机氧化剂|70693-62-8
四乙酸铅|Pb(OAc)4 LTA|无机氧化剂|546-67-8
三氟化硼乙醚|BF3-Et2O BF3.OEt2|无机路易斯酸|109-63-7
双三氟甲磺酰亚胺锂|LiNTf2 LiTFSI|无机盐与添加剂|90076-65-6
六氟磷酸银|AgPF6|无机盐与添加剂|26042-63-7
六氟锑酸银|AgSbF6|无机盐与添加剂|26042-64-8
四氟硼酸银|AgBF4|无机盐与添加剂|14104-20-2
六氟磷酸铵|NH4PF6|无机盐与添加剂|16941-11-0
三氟甲磺酸锂|LiOTf|无机盐与添加剂|33454-82-9
"""


def fetch(record):
    title, alias, category, query = record.split('|')
    if query in CURATED:
        smiles, formula, source = CURATED[query]
        return dict(title=title, alias=alias, category=category, query=query,
                    smiles=smiles, formula=formula, source=source,
                    inorganic=category.startswith('无机'), representation='reviewed formulation')
    url = 'https://pubchem.ncbi.nlm.nih.gov/rest/pug/compound/name/' + quote(query, safe='') + '/property/CanonicalSMILES,IsomericSMILES,MolecularFormula,InChIKey/JSON'
    for attempt in range(3):
        try:
            with urlopen(url, timeout=25) as response:
                result = json.load(response)['PropertyTable']['Properties']
            if len(result) != 1:
                raise ValueError('ambiguous identity: '+query)
            value = result[0]
            return dict(title=title, alias=alias, category=category,
                        query=query, cid=value['CID'], formula=value['MolecularFormula'],
                        inchikey=value['InChIKey'],
                        smiles=value.get('SMILES', value.get('ConnectivitySMILES')),
                        inorganic=category.startswith('无机'),
                        source='https://pubchem.ncbi.nlm.nih.gov/compound/'+str(value['CID']))
        except Exception:
            if attempt == 2:
                raise
            time.sleep(1+attempt)


def main():
    target = ROOT/'reagents_extra.json'
    cached = json.loads(target.read_text(encoding='utf-8')) if target.exists() else []
    cache = {item['query']: item for item in cached}
    lines = DATA.strip().splitlines()
    results, failures = {}, []
    with ThreadPoolExecutor(max_workers=3) as pool:
        pending = {pool.submit(fetch, line): line for line in lines if line.split('|')[-1] not in cache}
        for future in as_completed(pending):
            line = pending[future]
            try:
                value = future.result(); results[value['query']] = value
                print('OK', value['alias'], value['formula'], flush=True)
            except Exception as exc:
                failures.append(dict(record=line, error=str(exc)))
                print('FAIL', line.split('|')[1], str(exc), flush=True)
    cache.update(results)
    entries = []
    for line in lines:
        title, alias, category, query = line.split('|')
        if query not in cache:
            continue
        if query in CURATED:
            cache[query] = fetch(line)
        value = dict(cache[query], title=title, alias=alias, category=category,
                     inorganic=category.startswith('无机'))
        entries.append(value)
    target.write_text(json.dumps(entries, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (ROOT/'tools/reagent_fetch_failures.json').write_text(json.dumps(failures, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Resolved', len(entries), 'unresolved', len(failures))


if __name__ == '__main__':
    main()
