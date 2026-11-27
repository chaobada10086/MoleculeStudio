# 官能团与缩写目录

> 本文件由 `studio/tools/build_functional_groups_doc.py` 从 `studio/nicknames.src.json`
> 与 `studio/nickname_catalog.hpp` 生成，**请勿手工编辑**。修改缩写后依次运行：
>
> `python studio/tools/build_nickname_catalog.py`
> `python studio/tools/build_functional_groups_doc.py`

## 统计

| 项目 | 数量 |
| --- | --- |
| 授权源数据条目（nicknames.src.json） | 671 |
| 编译进编辑器目录（nickname_catalog.hpp） | 663 |
| 未编译条目（独立分子，无连接点） | 8 |

以下名称不在单端基团表中。完整分子和歧义候选另表管理。

`BTC`、`Edc`、`THF`、`dC`、`dT`、`rA`、`rG`、`rU`

每条编译条目都是一段带恰好一个外部连接点的 CDXML 片段：在键末端输入其缩写后，
画布上的原子会被识别为对应官能团，可用“官能团展开”还原为完整结构。

## 自动大小写规则

输入在提交时经过 `chemcompletion::normalize`（原子标签）或
`chemcompletion::chemicalToken`（整段文字），规则按顺序如下：

1. **完全匹配的元素符号原样保留**：`Co`、`Cn`、`N`、`O`、`Fe` 等保留标准大小写。同一拼写兼指元素和基团时由用户选择。
2. **已经等于目录标准写法的输入不重写**：`Boc`、`CN`、`i-Bu` 原样保留。
3. **键匹配忽略大小写和连字符**：键由字母数字组成，去掉 `-` 并转小写。
   小写或大小写混写输入匹配到唯一标准写法后替换，例如 `bpin` → `Bpin`、`boc` → `Boc`、`mgbr` → `MgBr`。
4. **存在多个结构时**，输入框列出候选并显示连接点 R。可以记住本次选择，也可以在基团补齐设置中恢复询问。
5. **人工评审的连字符写法**：以下常用烷基前缀统一采用紧凑写法（旧目录中的连字符形式
   仍然可以被显式输入识别，只是不再作为小写输入的目标）：

| 输入键 | 标准写法 | 目录中同名异写 |
| --- | --- | --- |
| `tbu` | `tBu` | `t-Bu / tBu` |
| `sbu` | `sBu` | `s-Bu / sBu` |
| `ibu` | `iBu` | `i-Bu / iBu` |
| `nbu` | `nBu` | `n-Bu / nBu` |
| `ipr` | `iPr` | `i-Pr / iPr` |
| `npr` | `nPr` | `n-Pr / nPr` |

6. **元素符号同时是常用缩写**（`Ac`、`Np`、`Pr`、`Ts`、`Li` 等）：
   精确元素符号写入原子序数。同一拼写还表示基团时，选择框区分元素与基团。

## 歧义处理

- `pin` 收录为 **O-连接的频哪醇醚**（pinacolato O-linked）；如需频哪醇硼酸酯请用 `Bpin`。
- `DIBAL` 是试剂简写，片段按 Al 连接的二异丁基铝绘制（不含 Al–H 氢）。
- `Cp` 按 η1 碳连接绘制；与金属的 η5 配位需另行绘制配位键。
- 单端基团、双端连接基和独立化合物分别保存其连接信息。双端连接基需要两条单键。
- 补齐设置可查询缺少可靠连接定义的条目。

### 补充目录

- [有明确候选的缩写](../studio/group_choice_sources.json) · 81 条
- [双端连接基](../studio/bridge_groups.json) · 30 条
- [补充完整分子](../studio/reference_compounds.json) · 57 条
- [待定结构](../studio/tools/nicknames_skipped.json) · 45 条
- 核对来源包括用户提供的《参考缩写表》、[CAS 保护基表](https://web.cas.org/marketing/pdf/blocking.pdf) 和 [Hans Reich 有机化学缩写表](https://organicchemistrydata.org/hansreich/resources/acronyms/)。
- 大小写冲突的元素符号（`Ac`、`Np`、`Pr`、`Ts`、`Li`）不做自动大小写改写；
  停用某个条目后，该缩写完全按用户输入保留。

## 全部缩写

| 缩写 | 含义 | SMILES（源数据） | 原子/键 | 分子式 | 置信度 |
| --- | --- | --- | --- | --- | --- |
| `1-Naph` | 1-naphthyl (alpha) | `[*]c1cccc2ccccc12` | 11/12 | C10H7 | high |
| `2-4DCZ` | 2,4-dichlorobenzyloxycarbonyl (2,4-diCl-Z) | `[*]C(=O)OCc1ccc(Cl)cc1Cl` | 13/13 | C8H5Cl2O2 | medium |
| `2-6Clb` | (2,6-dichlorophenyl)methyl | `*Cc1c(Cl)cccc1Cl` | 10/10 | C7H5Cl2 | high |
| `2-6DCZ` | 2,6-dichlorobenzyloxycarbonyl (2,6-diCl-Z) | `[*]C(=O)OCc1c(Cl)cccc1Cl` | 13/13 | C8H5Cl2O2 | medium |
| `2-Naph` | 2-naphthyl (beta) | `[*]c1ccc2ccccc2c1` | 11/12 | C10H7 | high |
| `2Abz` | 2-aminobenzoyl | `*C(=O)c1ccccc1N` | 10/10 | C7H6NO | high |
| `2BZ` | [(2-bromophenyl)methoxy]carbonyl | `*C(=O)OCc1ccccc1Br` | 12/12 | C8H6BrO2 | high |
| `2CZ` | 2-chlorobenzyloxycarbonyl (2-Cl-Z) | `[*]C(=O)OCc1ccccc1Cl` | 12/12 | C8H6ClO2 | medium |
| `2OHEt` | 2-hydroxyethyl | `OCC[*]` | 4/3 | C2H5O | high |
| `2OHPh` | 2-hydroxyphenyl | `[*]c1ccccc1O` | 8/8 | C6H5O | high |
| `2Pip` | 2-piperidinyl | `*C1CCCCN1` | 7/7 | C5H10N | high |
| `3Py` | pyridin-3-yl | `[*]c1cccnc1` | 7/7 | C5H4N | high |
| `4-Tosyl` | tosyl (4-methylbenzenesulfonyl) | `*S(=O)(=O)c1ccc(C)cc1` | 11/11 | C7H7O2S | high |
| `4Abz` | 4-aminobenzoyl | `*C(=O)c1ccc(N)cc1` | 10/10 | C7H6NO | high |
| `4BZ` | [(4-bromophenyl)methoxy]carbonyl | `*C(=O)OCc1ccc(Br)cc1` | 12/12 | C8H6BrO2 | high |
| `4CZ` | 4-chlorobenzyloxycarbonyl (4-Cl-Z) | `[*]C(=O)OCc1ccc(Cl)cc1` | 12/12 | C8H6ClO2 | medium |
| `4Py` | pyridin-4-yl | `[*]c1ccncc1` | 7/7 | C5H4N | high |
| `Aac` | 2-amino-2-oxoethyl | `*CC(N)=O` | 5/4 | C2H4NO | high |
| `Aad` | L-2-aminoadipic acid residue | `N[C@@H](CCCC(=O)O)C(=O)[*]` | 11/10 | C6H10NO3 | high |
| `Abu` | L-2-aminobutyric acid | `CC[C@H](N)C(=O)[*]` | 7/6 | C4H8NO | high |
| `Abz` | 2-aminobenzoyl (anthraniloyl) | `Nc1ccccc1C(=O)[*]` | 10/10 | C7H6NO | medium |
| `Ac` | acetyl | `CC(=O)[*]` | 4/3 | C2H3O | high |
| `Aca` | 2-aminodecanoyl | `*C(=O)C(N)CCCCCCCC` | 13/12 | C10H20NO | high |
| `Ace` | Common functional group | `*C(C)=O` | 4/3 | C2H3O | high |
| `Acetyl` | Common functional group | `*C(C)=O` | 4/3 | C2H3O | high |
| `Acm` | acetamidomethyl (Cys protection) | `CC(=O)NC[*]` | 6/5 | C3H6NO | high |
| `AcNH` | acetylamino | `CC(=O)N[*]` | 5/4 | C2H4NO | high |
| `AcO` | acetoxy (attached through O) | `CC(=O)O[*]` | 5/4 | C2H3O2 | high |
| `Acr` | acridin-9-yl | `*c1c2ccccc2nc2ccccc12` | 15/17 | C13H8N | high |
| `Ad` | 1-adamantyl | `[*]C12CC3CC(CC(C3)C1)C2` | 11/13 | C10H15 | high |
| `Adc` | adamantan-1-yloxy | `*OC12CC3CC(CC(C3)C1)C2` | 12/14 | C10H15O | high |
| `Adoc` | 1-adamantyloxycarbonyl | `[*]C(=O)OC12CC3CC(CC(C3)C1)C2` | 14/16 | C11H15O2 | medium |
| `Aet` | 2-aminoethyl | `*CCN` | 4/3 | C2H6N | high |
| `Ahx` | 6-aminohexanoyl | `NCCCCCC(=O)[*]` | 9/8 | C6H12NO | high |
| `Aib` | 2-aminoisobutyric acid (Aib) | `CC(C)(N)C(=O)[*]` | 7/6 | C4H8NO | high |
| `aIle` | L-allo-isoleucine (2S,3R) | `CC[C@@H](C)[C@H](N)C(=O)[*]` | 9/8 | C6H12NO | medium |
| `Al(OEt)2` | Authored metal-centred alkoxide attachment group | `[*][Al](OCC)OCC` | 8/7 | C4H10AlO2 | high |
| `Al(Oi-Pr)2` | Authored metal-centred alkoxide attachment group | `[*][Al](OC(C)C)OC(C)C` | 10/9 | C6H14AlO2 | high |
| `Al(OMe)2` | Authored metal-centred alkoxide attachment group | `[*][Al](OC)OC` | 6/5 | C2H6AlO2 | high |
| `Al(Ot-Bu)2` | Authored metal-centred alkoxide attachment group | `[*][Al](OC(C)(C)C)OC(C)(C)C` | 12/11 | C8H18AlO2 | high |
| `Ala` | L-alanine residue | `N[C@@H](C)C(=O)[*]` | 6/5 | C3H6NO | high |
| `All` | allyl | `C=CC[*]` | 4/3 | C3H5 | high |
| `Alloc` | allyloxycarbonyl | `C=CCOC(=O)[*]` | 7/6 | C4H5O2 | high |
| `Allyl` | allyl | `C=CC[*]` | 4/3 | C3H5 | high |
| `Am` | n-pentyl | `*CCCCC` | 6/5 | C5H11 | high |
| `Amoc` | (9-anthracenylmethoxy)carbonyl | `*C(=O)OCc1c2ccccc2cc2ccccc12` | 19/21 | C16H11O2 | high |
| `An` | 4-methoxyphenyl (p-anisyl); supplied Zhihu abbreviation table | `[*]c1ccc(OC)cc1` | 9/9 | C7H7O | high |
| `Aoc` | (1,1-dimethylpropoxy)carbonyl | `*C(=O)OC(C)(C)CC` | 9/8 | C6H11O2 | high |
| `Apm` | 2-aminoheptanedioic acid residue | `*C(=O)C(N)CCCCC(=O)O` | 12/11 | C7H12NO3 | high |
| `App` | 4-amino-3-hydroxy-5-phenylpentanoyl | `*C(=O)CC(O)C(N)Cc1ccccc1` | 15/15 | C11H14NO2 | high |
| `Arg` | L-arginine residue | `N[C@@H](CCCNC(=N)N)C(=O)[*]` | 12/11 | C6H13N4O | high |
| `Asn` | L-asparagine | `N[C@@H](CC(N)=O)C(=O)[*]` | 9/8 | C4H7N2O2 | high |
| `Asp` | L-aspartic acid residue | `N[C@@H](CC(=O)O)C(=O)[*]` | 9/8 | C4H6NO3 | high |
| `Asu` | 2-aminooctanedioic acid residue | `*C(=O)C(N)CCCCCC(=O)O` | 13/12 | C8H14NO3 | high |
| `aThr` | L-allo-threonine (2S,3R) | `C[C@H](O)[C@@H](N)C(=O)[*]` | 8/7 | C4H8NO2 | medium |
| `Aze` | L-azetidine-2-carboxylic acid | `O=C([*])[C@@H]1CCN1` | 7/7 | C4H6NO | high |
| `Azoc` | [1-methyl-1-[4-(phenylazo)phenyl]ethoxy]carbonyl | `*C(=O)OC(C)(C)c1ccc(N=Nc2ccccc2)cc1` | 21/22 | C16H15N2O2 | high |
| `B(OH)2` | Common functional group; attachment at * | `*B(O)O` | 4/3 | H2BO2 | authored |
| `B(Oi-Pr)2` | Authored metal-centred alkoxide attachment group | `[*][B](OC(C)C)OC(C)C` | 10/9 | C6H14BO2 | high |
| `Baam` | anthranilamide-protected boron; B-bound | `[*]B1Nc2ccccc2C(=O)N1` | 12/13 | C7H6BN2O | high |
| `Bac` | bromoacetyl | `*C(=O)CBr` | 5/4 | C2H2BrO | high |
| `Bal` | beta-alanyl | `*C(=O)CCN` | 6/5 | C3H6NO | high |
| `bAla` | beta-alanine (3-aminopropanoic acid) | `NCCC(=O)[*]` | 6/5 | C3H6NO | high |
| `Bam` | (benzoylamino)methyl | `*CNC(=O)c1ccccc1` | 11/11 | C8H8NO | high |
| `bArg` | beta3-homoarginine residue | `N[C@@H](CCCNC(=N)N)CC(=O)[*]` | 13/12 | C7H15N4O | medium |
| `Bas` | beta-aspartyl | `*C(=O)CC(N)C(=O)O` | 9/8 | C4H6NO3 | high |
| `bAsn` | beta3-asparagine; C3 stereochemistry left unspecified | `NC(=O)CC(N)CC(=O)[*]` | 10/9 | C5H9N2O2 | medium |
| `bAsp` | beta3-homoaspartic acid residue | `N[C@@H](CC(=O)O)CC(=O)[*]` | 10/9 | C5H8NO3 | medium |
| `Bcat` | catecholato boron; B-bound | `[*]B1Oc2ccccc2O1` | 10/11 | C6H4BO2 | high |
| `bCys` | beta3-cysteine; C3 stereochemistry left unspecified | `NC(CS)CC(=O)[*]` | 8/7 | C4H8NOS | medium |
| `Bdan` | 1,8-diaminonaphthalene-protected boron; B-bound | `[*]B1Nc2cccc3cccc(N1)c23` | 14/16 | C10H8BN2 | high |
| `Benzoyl` | benzoyl | `c1ccccc1C(=O)[*]` | 9/9 | C7H5O | high |
| `Benzyl` | benzyl | `[*]Cc1ccccc1` | 8/8 | C7H7 | high |
| `Beoc` | (2-bromoethoxy)carbonyl | `*C(=O)OCCBr` | 7/6 | C3H4BrO2 | high |
| `BF3K` | potassium organotrifluoroborate; B-bound; explicit K+ counterion | `[*][B-](F)(F)F.[K+]` | 6/4 | BF3K | high |
| `bGln` | beta3-homoglutamine residue | `N[C@@H](CCC(N)=O)CC(=O)[*]` | 11/10 | C6H11N2O2 | medium |
| `bGlu` | beta3-glutamic acid; C3 stereochemistry left unspecified | `OC(=O)CCC(N)CC(=O)[*]` | 11/10 | C6H10NO3 | medium |
| `bHis` | beta3-homohistidine residue | `N[C@@H](Cc1c[nH]cn1)CC(=O)[*]` | 12/12 | C7H10N3O | medium |
| `Bhoc` | benzhydryloxycarbonyl | `[*]C(=O)OC(c1ccccc1)c1ccccc1` | 17/18 | C14H11O2 | high |
| `Bic` | (5-benzisoxazolylmethoxy)carbonyl | `*C(=O)OCc1ccc2oncc2c1` | 14/15 | C9H6NO3 | high |
| `bIle` | beta3-isoleucine; C3 stereochemistry left unspecified | `CCC(C)C(N)CC(=O)[*]` | 10/9 | C7H14NO | medium |
| `Bip` | biphenyl-4-sulfonyl; supplied Zhihu abbreviation table | `[*]S(=O)(=O)c1ccc(-c2ccccc2)cc1` | 16/17 | C12H9O2S | high |
| `bLeu` | beta3-homoleucine residue | `N[C@@H](CC(C)C)CC(=O)[*]` | 10/9 | C7H14NO | medium |
| `Bly` | 3,6-diaminohexanoyl | `*C(=O)CC(N)CCCN` | 10/9 | C6H13N2O | high |
| `bLys` | beta3-lysine; C3 stereochemistry left unspecified | `NCCCCC(N)CC(=O)[*]` | 11/10 | C7H15N2O | medium |
| `bMet` | beta3-homomethionine residue | `N[C@@H](CCSC)CC(=O)[*]` | 10/9 | C6H12NOS | medium |
| `BMIDA` | N-methyliminodiacetato boron; B-bound; charge-separated B-/N+ single-bond representation of N-to-B donation | `[*][B-]12OC(=O)C[N+]1(C)CC(=O)O2` | 12/13 | C5H7BNO4 | high |
| `Bmv` | 1-methyl-3-oxo-3-phenyl-1-propenyl | `*C(C)=CC(=O)c1ccccc1` | 12/12 | C10H9O | high |
| `Bn` | benzyl | `[*]Cc1ccccc1` | 8/8 | C7H7 | high |
| `Bneop` | neopentyl glycolato boron; B-bound | `[*]B1OCC(C)(C)CO1` | 9/9 | C5H10BO2 | high |
| `Bnps` | (3-bromo-2-nitrophenyl)thio | `*Sc1cccc(Br)c1[N+](=O)[O-]` | 12/12 | C6H3BrNO2S | high |
| `BOB` | 4-benzyloxybutyryl; supplied Zhihu abbreviation table | `[*]C(=O)CCCOCc1ccccc1` | 14/14 | C11H13O2 | high |
| `BOC` | (1,1-dimethylethoxy)carbonyl | `*C(=O)OC(C)(C)C` | 8/7 | C5H9O2 | high |
| `Boc` | tert-butoxycarbonyl | `CC(C)(C)OC(=O)[*]` | 8/7 | C5H9O2 | high |
| `Bocae` | 2-[(tert-butoxycarbonyl)amino]ethyl | `*CCNC(=O)OC(C)(C)C` | 11/10 | C7H14NO2 | high |
| `BOM` | benzyloxymethyl | `c1ccccc1COC[*]` | 10/10 | C8H9O | high |
| `bPhe` | beta3-phenylalanine; C3 stereochemistry left unspecified | `NC(c1ccccc1)CC(=O)[*]` | 12/12 | C9H10NO | medium |
| `Bpin` | pinacolboranyl (4,4,5,5-tetramethyl-1,3,2-dioxaborolan-2-yl, B-bound) | `[*]B1OC(C)(C)C(C)(C)O1` | 10/10 | C6H12BO2 | high |
| `Bpoc` | 2-(biphenyl-4-yl)propan-2-yloxycarbonyl | `[*]C(=O)OC(C)(C)c1ccc(-c2ccccc2)cc1` | 19/20 | C16H15O2 | high |
| `BPS` | tert-butyldiphenylsilyl | `*[Si](c1ccccc1)(c1ccccc1)C(C)(C)C` | 18/19 | C16H19Si | high |
| `Bs` | 4-bromobenzenesulfonyl (brosyl) | `[*]S(=O)(=O)c1ccc(Br)cc1` | 11/11 | C6H4BrO2S | high |
| `bSer` | beta3-serine; C3 stereochemistry left unspecified | `OCC(N)CC(=O)[*]` | 8/7 | C4H8NO2 | medium |
| `Bt` | benzotriazol-1-yl | `[*]n1nnc2ccccc12` | 10/11 | C6H4N3 | high |
| `BTC(CAS)` | [(phenylmethyl)thio]carbonyl | `*C(=O)SCc1ccccc1` | 11/11 | C8H7OS | high |
| `Btm` | [(phenylmethyl)thio]methyl | `*CSCc1ccccc1` | 10/10 | C8H9S | high |
| `bTrp` | beta3-tryptophan; C3 stereochemistry left unspecified | `NC(Cc1c[nH]c2ccccc12)CC(=O)[*]` | 16/17 | C12H13N2O | medium |
| `bTyr` | beta3-homotyrosine residue | `N[C@@H](Cc1ccc(O)cc1)CC(=O)[*]` | 14/14 | C10H12NO2 | medium |
| `Bu` | n-butyl | `CCCC[*]` | 5/4 | C4H9 | high |
| `Bua` | butanoyl | `*C(=O)CCC` | 6/5 | C4H7O | high |
| `Bum` | [(2-methyl-1-oxopropyl)amino]methyl | `*CNC(=O)C(C)C` | 8/7 | C5H10NO | high |
| `Bux` | 4-amino-3-hydroxybutanoyl | `*C(=O)CC(O)CN` | 8/7 | C4H8NO2 | high |
| `bVal` | beta3-valine; C3 stereochemistry left unspecified | `CC(C)C(N)CC(=O)[*]` | 9/8 | C6H12NO | medium |
| `Bz` | benzoyl | `O=C([*])c1ccccc1` | 9/9 | C7H5O | high |
| `Bza` | 1H-benzimidazol-2-yl | `*c1nc2ccccc2[nH]1` | 10/11 | C7H5N2 | high |
| `Bzh` | diphenylmethyl | `*C(c1ccccc1)c1ccccc1` | 14/15 | C13H11 | high |
| `Bzl` | benzyl | `[*]Cc1ccccc1` | 8/8 | C7H7 | high |
| `BzOM` | benzoyloxymethyl | `c1ccccc1C(=O)OC[*]` | 11/11 | C8H7O2 | medium |
| `C(=O)Me` | acetyl, written as C(=O)Me | `CC(=O)[*]` | 4/3 | C2H3O | high |
| `C(=O)NH2` | carbamoyl | `NC(=O)[*]` | 4/3 | CH2NO | high |
| `C(O)Me` | 1-hydroxyethyl (-CH(OH)CH3) | `CC(O)[*]` | 4/3 | C2H5O | medium |
| `C(O)NH2` | carbamoyl | `NC(=O)[*]` | 4/3 | CH2NO | high |
| `c-Bu` | cyclobutyl | `*C1CCC1` | 5/5 | C4H7 | high |
| `c-But` | cyclobutyl | `*C1CCC1` | 5/5 | C4H7 | high |
| `c-Butyl` | cyclobutyl | `*C1CCC1` | 5/5 | C4H7 | high |
| `c-C3H5` | cyclopropyl | `[*]C1CC1` | 4/4 | C3H5 | high |
| `c-C4H7` | cyclobutyl | `C1CCC1[*]` | 5/5 | C4H7 | high |
| `c-C5H9` | cyclopentyl | `[*]C1CCCC1` | 6/6 | C5H9 | high |
| `c-C6H11` | cyclohexyl | `C1CCCCC1[*]` | 7/7 | C6H11 | high |
| `c-C7H13` | cycloheptyl | `[*]C1CCCCCC1` | 8/8 | C7H13 | high |
| `c-C8H15` | cyclooctyl | `C1CCCCCCC1[*]` | 9/9 | C8H15 | high |
| `c-Heptyl` | cycloheptyl | `*C1CCCCCC1` | 8/8 | C7H13 | high |
| `c-Hexyl` | cyclohexyl | `*C1CCCCC1` | 7/7 | C6H11 | high |
| `c-Hx` | cyclohexyl | `[*]C1CCCCC1` | 7/7 | C6H11 | high |
| `c-Pentyl` | cyclopentyl | `*C1CCCC1` | 6/6 | C5H9 | high |
| `c-Pr` | cyclopropyl | `*C1CC1` | 4/4 | C3H5 | high |
| `c-Pro` | cyclopropyl | `*C1CC1` | 4/4 | C3H5 | high |
| `c-Propyl` | cyclopropyl | `*C1CC1` | 4/4 | C3H5 | high |
| `C10H21` | n-alkyl (C10H21) | `CCCCCCCCCC[*]` | 11/10 | C10H21 | high |
| `C11H23` | n-alkyl (C11H23) | `CCCCCCCCCCC[*]` | 12/11 | C11H23 | high |
| `C12H25` | n-alkyl (C12H25) | `CCCCCCCCCCCC[*]` | 13/12 | C12H25 | high |
| `C13H27` | n-alkyl (C13H27) | `CCCCCCCCCCCCC[*]` | 14/13 | C13H27 | high |
| `C14H29` | n-alkyl (C14H29) | `CCCCCCCCCCCCCC[*]` | 15/14 | C14H29 | high |
| `C15H31` | n-alkyl (C15H31) | `CCCCCCCCCCCCCCC[*]` | 16/15 | C15H31 | high |
| `C16H33` | n-alkyl (C16H33) | `CCCCCCCCCCCCCCCC[*]` | 17/16 | C16H33 | high |
| `C17H35` | n-alkyl (C17H35) | `CCCCCCCCCCCCCCCCC[*]` | 18/17 | C17H35 | high |
| `C18H37` | n-alkyl (C18H37) | `CCCCCCCCCCCCCCCCCC[*]` | 19/18 | C18H37 | high |
| `C19H39` | n-alkyl (C19H39) | `CCCCCCCCCCCCCCCCCCC[*]` | 20/19 | C19H39 | high |
| `C20H41` | n-alkyl (C20H41) | `CCCCCCCCCCCCCCCCCCCC[*]` | 21/20 | C20H41 | high |
| `C2H5` | n-alkyl (C2H5) | `CC[*]` | 3/2 | C2H5 | high |
| `C3H7` | n-alkyl (C3H7) | `CCC[*]` | 4/3 | C3H7 | high |
| `C4H9` | n-alkyl (C4H9) | `CCCC[*]` | 5/4 | C4H9 | high |
| `C5H11` | n-alkyl (C5H11) | `CCCCC[*]` | 6/5 | C5H11 | high |
| `C6F5` | pentafluorophenyl | `[*]c1c(F)c(F)c(F)c(F)c1F` | 12/12 | C6F5 | high |
| `C6H13` | n-alkyl (C6H13) | `CCCCCC[*]` | 7/6 | C6H13 | high |
| `C6H5` | phenyl | `[*]c1ccccc1` | 7/7 | C6H5 | high |
| `C7H15` | n-heptyl, linear isomer assumed | `CCCCCCC[*]` | 8/7 | C7H15 | medium |
| `C8H17` | n-octyl, linear isomer assumed | `CCCCCCCC[*]` | 9/8 | C8H17 | medium |
| `C9H19` | n-nonyl, linear isomer assumed | `CCCCCCCCC[*]` | 10/9 | C9H19 | medium |
| `Cac` | carboxyacetyl | `*C(=O)CC(=O)O` | 7/6 | C3H3O3 | high |
| `Cap` | caproyl (n-hexanoyl) | `CCCCCC(=O)[*]` | 8/7 | C6H11O | medium |
| `Cbm` | aminocarbonyl | `*C(N)=O` | 4/3 | CH2NO | high |
| `Cbs` | (4-chlorophenyl)sulfonyl | `*S(=O)(=O)c1ccc(Cl)cc1` | 11/11 | C6H4ClO2S | high |
| `cBu` | cyclobutyl | `*C1CCC1` | 5/5 | C4H7 | high |
| `cButyl` | cyclobutyl | `*C1CCC1` | 5/5 | C4H7 | high |
| `CBz` | (phenylmethoxy)carbonyl | `*C(=O)OCc1ccccc1` | 11/11 | C8H7O2 | high |
| `Cbz` | benzyloxycarbonyl | `[*]C(=O)OCc1ccccc1` | 11/11 | C8H7O2 | high |
| `CCl3` | trichloromethyl | `[*]C(Cl)(Cl)Cl` | 5/4 | CCl3 | high |
| `Cdf` | chlorodifluoroacetyl | `*C(=O)C(F)(F)Cl` | 7/6 | C2ClF2O | high |
| `Ceoc` | (2-chloroethoxy)carbonyl | `*C(=O)OCCCl` | 7/6 | C3H4ClO2 | high |
| `CF3` | Common functional group; attachment at * | `*C(F)(F)F` | 5/4 | CF3 | authored |
| `CH2Cl` | chloromethyl | `[*]CCl` | 3/2 | CH2Cl | high |
| `CH2F` | Common functional group; attachment at * | `*CF` | 3/2 | CH2F | authored |
| `CH2NH2` | aminomethyl | `NC[*]` | 3/2 | CH4N | high |
| `CH2OH` | hydroxymethyl | `OC[*]` | 3/2 | CH3O | high |
| `CH3` | methyl | `*C` | 2/1 | CH3 | high |
| `Cha` | L-cyclohexylalanine | `N[C@@H](CC1CCCCC1)C(=O)[*]` | 12/12 | C9H16NO | high |
| `Chc` | cyclohexylcarbonyl | `*C(=O)C1CCCCC1` | 9/9 | C7H11O | high |
| `cHeptyl` | cycloheptyl | `*C1CCCCCC1` | 8/8 | C7H13 | high |
| `cHexyl` | cyclohexyl | `*C1CCCCC1` | 7/7 | C6H11 | high |
| `CHF2` | Common functional group; attachment at * | `*C(F)F` | 4/3 | CHF2 | authored |
| `CHO` | formyl | `O=C[*]` | 3/2 | CHO | high |
| `Chp` | cycloheptyl | `*C1CCCCCC1` | 8/8 | C7H13 | high |
| `Chx` | cyclohexyl; alias of Cy; supplied Zhihu abbreviation table | `[*]C1CCCCC1` | 7/7 | C6H11 | high |
| `cHx` | cyclohexyl | `C1CCCCC1[*]` | 7/7 | C6H11 | high |
| `Chxa` | cyclohexylacetyl | `*C(=O)CC1CCCCC1` | 10/10 | C8H13O | high |
| `cinnamyl` | (E)-cinnamyl | `*C/C=C/c1ccccc1` | 10/10 | C9H9 | high |
| `Cit` | L-citrulline residue | `N[C@@H](CCCNC(N)=O)C(=O)[*]` | 12/11 | C6H12N3O2 | high |
| `Cm` | carboxymethyl | `*CC(=O)O` | 5/4 | C2H3O2 | high |
| `CN` | cyano | `N#C[*]` | 3/2 | CN | high |
| `CO2Bn` | (phenylmethoxy)carbonyl | `*C(=O)OCc1ccccc1` | 11/11 | C8H7O2 | high |
| `CO2Et` | ethoxycarbonyl | `CCOC(=O)[*]` | 6/5 | C3H5O2 | high |
| `CO2H` | carboxy | `OC(=O)[*]` | 4/3 | CHO2 | high |
| `CO2Me` | methoxycarbonyl | `COC(=O)[*]` | 5/4 | C2H3O2 | high |
| `COBr` | bromoformyl (acyl bromide) | `BrC(=O)[*]` | 4/3 | CBrO | high |
| `COCF3` | Common functional group; attachment at * | `*C(=O)C(F)(F)F` | 7/6 | C2F3O | authored |
| `COCH3` | Common functional group; attachment at * | `*C(=O)C` | 4/3 | C2H3O | authored |
| `COCl` | chlorocarbonyl | `O=C(Cl)[*]` | 4/3 | CClO | high |
| `COMe` | Common functional group | `*C(C)=O` | 4/3 | C2H3O | high |
| `CON3` | acyl azide (charge separation required by the azide) | `[N-]=[N+]=NC(=O)[*]` | 6/5 | CN3O | high |
| `CONH2` | carbamoyl | `NC(=O)[*]` | 4/3 | CH2NO | high |
| `CONHEt` | (ethylamino)carbonyl | `*C(=O)NCC` | 6/5 | C3H6NO | high |
| `CONHMe` | N-methylcarbamoyl | `CNC(=O)[*]` | 5/4 | C2H4NO | high |
| `CONMe2` | N,N-dimethylcarbamoyl | `CN(C)C(=O)[*]` | 6/5 | C3H6NO | high |
| `COOCH3` | methoxycarbonyl | `COC(=O)[*]` | 5/4 | C2H3O2 | high |
| `COOEt` | ethoxycarbonyl | `CCOC(=O)[*]` | 6/5 | C3H5O2 | high |
| `COOH` | carboxy | `OC(=O)[*]` | 4/3 | CHO2 | high |
| `COOMe` | methoxycarbonyl | `COC(=O)[*]` | 5/4 | C2H3O2 | high |
| `COOtBu` | Common functional group; attachment at * | `*C(=O)OC(C)(C)C` | 8/7 | C5H9O2 | authored |
| `Cp` | cyclopentadienyl (eta-1 C-bound; coordination to metals is shown separately) | `[*]C1C=CC=C1` | 6/6 | C5H5 | medium |
| `Cpc` | cyclopentylcarbonyl | `*C(=O)C1CCCC1` | 8/8 | C6H9O | high |
| `Cpe` | cyclopentyl | `*C1CCCC1` | 6/6 | C5H9 | high |
| `cPentyl` | cyclopentyl | `*C1CCCC1` | 6/6 | C5H9 | high |
| `Cpm` | cyclopropylmethyl | `[*]CC1CC1` | 5/5 | C4H7 | medium |
| `cPr` | cyclopropyl | `*C1CC1` | 4/4 | C3H5 | high |
| `cPropyl` | cyclopropyl | `*C1CC1` | 4/4 | C3H5 | high |
| `Cy` | cyclohexyl | `C1CCCCC1[*]` | 7/7 | C6H11 | high |
| `Cya` | cysteic acid residue | `*C(=O)C(N)CS(=O)(=O)O` | 10/9 | C3H6NO4S | high |
| `cyclobutyl` | cyclobutyl | `C1CCC1[*]` | 5/5 | C4H7 | high |
| `cycloheptyl` | cycloheptyl | `[*]C1CCCCCC1` | 8/8 | C7H13 | high |
| `cyclooctyl` | cyclooctyl | `C1CCCCCCC1[*]` | 9/9 | C8H15 | high |
| `cyclopentyl` | cyclopentyl | `[*]C1CCCC1` | 6/6 | C5H9 | high |
| `cyclopropyl` | cyclopropyl | `C1CC1[*]` | 4/4 | C3H5 | high |
| `Cys` | L-cysteine | `N[C@@H](CS)C(=O)[*]` | 7/6 | C3H6NOS | high |
| `Dab` | L-2,4-diaminobutyric acid residue | `N[C@@H](CCN)C(=O)[*]` | 8/7 | C4H9N2O | high |
| `Dan` | 5-(dimethylamino)naphthalene-1-sulfonyl (dansyl) | `*S(=O)(=O)c1cccc2c(N(C)C)cccc12` | 17/18 | C12H12NO2S | high |
| `Dansyl` | 5-(dimethylamino)naphthalene-1-sulfonyl (dansyl) | `*S(=O)(=O)c1cccc2c(N(C)C)cccc12` | 17/18 | C12H12NO2S | high |
| `Dap` | L-2,3-diaminopropionic acid residue | `N[C@@H](CN)C(=O)[*]` | 7/6 | C3H7N2O | high |
| `Dbpoc` | (2,2-dibromopropoxy)carbonyl | `*C(=O)OCC(C)(Br)Br` | 9/8 | C4H5Br2O2 | high |
| `Dcpm` | dicyclopropylmethyl; supplied Zhihu abbreviation table | `[*]C(C1CC1)C1CC1` | 8/9 | C7H11 | high |
| `Dde` | 1-(4,4-dimethyl-2,6-dioxocyclohex-1-ylidene)ethyl; attaches to the protected amine N | `[*]C(C)=C1C(=O)CC(C)(C)CC1=O` | 13/13 | C10H13O2 | high |
| `Ddz` | 2-(3,5-dimethoxyphenyl)propan-2-yloxycarbonyl | `[*]C(=O)OC(C)(C)c1cc(OC)cc(OC)c1` | 17/17 | C12H15O4 | high |
| `De` | 2-(diethylamino)ethyl | `*CCN(CC)CC` | 8/7 | C6H14N | high |
| `DEAE` | 2-(diethylamino)ethyl | `CCN(CC)CC[*]` | 8/7 | C6H14N | high |
| `Dec` | n-decyl | `CCCCCCCCCC[*]` | 11/10 | C10H21 | high |
| `Dec(CAS)` | 1-oxodecyl | `*C(=O)CCCCCCCCC` | 12/11 | C10H19O | high |
| `DEIPS` | diethylisopropylsilyl | `CC[Si](CC)(C(C)C)[*]` | 9/8 | C7H17Si | high |
| `Dep` | 2,6-diethylphenyl | `[*]c1c(CC)cccc1CC` | 11/11 | C10H13 | high |
| `Dha` | dehydroalanine | `C=C(N)C(=O)[*]` | 6/5 | C3H4NO | high |
| `DIBAL` | diisobutylaluminium (DIBAL-H without its hydride; Al-bound reagent shorthand) | `[*][Al](CC(C)C)CC(C)C` | 10/9 | C8H18Al | medium |
| `Dip` | [2-methyl-1-(1-methylethyl)propoxy]carbonyl | `*C(=O)OC(C(C)C)C(C)C` | 11/10 | C8H15O2 | high |
| `Dipp` | 2,6-diisopropylphenyl | `[*]c1c(C(C)C)cccc1C(C)C` | 13/13 | C12H17 | high |
| `DMB` | 2,4-dimethoxybenzyl | `COc1ccc(C[*])c(OC)c1` | 12/12 | C9H11O2 | high |
| `Dmb` | 2,4-dimethoxybenzyl | `*Cc1ccc(OC)cc1OC` | 12/12 | C9H11O2 | high |
| `DMB(CAS)` | (3,4-dimethylphenyl)methyl | `*Cc1ccc(C)c(C)c1` | 10/10 | C9H11 | high |
| `DMIPS` | dimethylisopropylsilyl | `C[Si](C)(C(C)C)[*]` | 7/6 | C5H13Si | high |
| `Dmoc` | [(dimethylamino)oxy]carbonyl | `*C(=O)ON(C)C` | 7/6 | C3H6NO2 | high |
| `DMPM` | 3,4-dimethoxybenzyl | `*Cc1ccc(OC)c(OC)c1` | 12/12 | C9H11O2 | high |
| `DMPS` | dimethylphenylsilyl | `C[Si](C)(c1ccccc1)[*]` | 10/10 | C8H11Si | medium |
| `Dmt` | bis(4-methoxyphenyl)methyl | `*C(c1ccc(OC)cc1)c1ccc(OC)cc1` | 18/19 | C15H15O2 | high |
| `DMTr` | 4,4'-dimethoxytrityl | `[*]C(c1ccccc1)(c1ccc(OC)cc1)c1ccc(OC)cc1` | 24/26 | C21H19O2 | high |
| `DNAA(3'-O)` | DNAA, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1C[C@H](n2cnc3c(N)ncnc32)O[C@@H]1CO` | 19/21 | C10H12N5O3 | high |
| `DNAA(5'-O)` | DNAA, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2cnc3c(N)ncnc32)C[C@@H]1O` | 19/21 | C10H12N5O3 | high |
| `DNAC(3'-O)` | DNAC, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1C[C@H](n2ccc(N)nc2=O)O[C@@H]1CO` | 17/18 | C9H12N3O4 | high |
| `DNAC(5'-O)` | DNAC, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2ccc(N)nc2=O)C[C@@H]1O` | 17/18 | C9H12N3O4 | high |
| `DNAG(3'-O)` | DNAG, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1C[C@H](n2cnc3c(=O)[nH]c(N)nc32)O[C@@H]1CO` | 20/22 | C10H12N5O4 | high |
| `DNAG(5'-O)` | DNAG, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2cnc3c(=O)[nH]c(N)nc32)C[C@@H]1O` | 20/22 | C10H12N5O4 | high |
| `DNAT(3'-O)` | DNAT, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1C[C@H](n2cc(C)c(=O)[nH]c2=O)O[C@@H]1CO` | 18/19 | C10H13N2O5 | high |
| `DNAT(5'-O)` | DNAT, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2cc(C)c(=O)[nH]c2=O)C[C@@H]1O` | 18/19 | C10H13N2O5 | high |
| `DNP` | 2,4-dinitrophenyl | `[*]c1ccc([N+](=O)[O-])cc1[N+](=O)[O-]` | 13/13 | C6H3N2O4 | high |
| `DNPS` | (2,4-dinitrophenyl)thio | `*Sc1ccc([N+](=O)[O-])cc1[N+](=O)[O-]` | 14/14 | C6H3N2O4S | high |
| `DNS` | 5-(dimethylamino)naphthalene-1-sulfonyl (dansyl) | `[*]S(=O)(=O)c1cccc2c(N(C)C)cccc12` | 17/18 | C12H12NO2S | high |
| `DPIPS` | diisopropylsilyl | `[*][SiH](C(C)C)C(C)C` | 8/7 | C6H15Si | medium |
| `Dpm` | diphenylmethyl (benzhydryl) | `c1ccccc1C(c1ccccc1)[*]` | 14/15 | C13H11 | high |
| `Dpp` | diphenoxyphosphinyl | `*P(=O)(Oc1ccccc1)Oc1ccccc1` | 17/18 | C12H10O3P | high |
| `Dpr` | L-2,3-diaminopropionic acid | `N[C@@H](CN)C(=O)[*]` | 7/6 | C3H7N2O | high |
| `DPS` | tert-butyldiphenylsilyl | `*[Si](c1ccccc1)(c1ccccc1)C(C)(C)C` | 18/19 | C16H19Si | high |
| `DPTBS` | tert-butoxydiphenylsilyl | `*[Si](OC(C)(C)C)(c1ccccc1)c1ccccc1` | 19/20 | C16H19OSi | high |
| `Dsu` | 2,7-diaminooctanedioic acid residue | `*C(=O)C(N)CCCCC(N)C(=O)O` | 14/13 | C8H15N2O3 | high |
| `DTBMS` | di-tert-butylmethylsilyl | `[*][Si](C)(C(C)(C)C)C(C)(C)C` | 11/10 | C9H21Si | medium |
| `DTBS` | di-tert-butylsilyl (Si-H retained) | `[*][SiH](C(C)(C)C)C(C)(C)C` | 10/9 | C8H19Si | medium |
| `dU(3'-O)` | dU, 3'-O attachment; no phosphate | `*O[C@H]1C[C@H](n2ccc(=O)[nH]c2=O)O[C@@H]1CO` | 17/18 | C9H11N2O5 | high |
| `dU(5'-O)` | dU, 5'-O attachment; no phosphate | `*OC[C@H]1O[C@@H](n2ccc(=O)[nH]c2=O)C[C@@H]1O` | 17/18 | C9H11N2O5 | high |
| `Eac` | (ethylamino)carbonyl | `*C(=O)NCC` | 6/5 | C3H6NO | high |
| `EE` | 1-ethoxyethyl; stereochemistry unspecified | `[*]C(C)OCC` | 6/5 | C4H9O | high |
| `Eoc` | ethoxycarbonyl | `*C(=O)OCC` | 6/5 | C3H5O2 | high |
| `EOM` | ethoxymethyl; supplied Zhihu abbreviation table | `[*]COCC` | 5/4 | C3H7O | high |
| `Et` | ethyl | `CC[*]` | 3/2 | C2H5 | high |
| `Ethyl` | ethyl | `*CC` | 3/2 | C2H5 | high |
| `Fmoc` | 9-fluorenylmethoxycarbonyl | `O=C(OCC1c2ccccc2c2ccccc21)[*]` | 18/20 | C15H11O2 | high |
| `For` | formyl | `O=C[*]` | 3/2 | CHO | high |
| `Formyl` | formyl | `*C=O` | 3/2 | CHO | high |
| `FRNAA(3'-O)` | FRNAA, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](F)[C@H](n2cnc3c(N)ncnc32)O[C@@H]1CO` | 20/22 | C10H11FN5O3 | high |
| `FRNAA(5'-O)` | FRNAA, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2cnc3c(N)ncnc32)[C@H](F)[C@@H]1O` | 20/22 | C10H11FN5O3 | high |
| `FRNAC(3'-O)` | FRNAC, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](F)[C@H](n2ccc(N)nc2=O)O[C@@H]1CO` | 18/19 | C9H11FN3O4 | high |
| `FRNAC(5'-O)` | FRNAC, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2ccc(N)nc2=O)[C@H](F)[C@@H]1O` | 18/19 | C9H11FN3O4 | high |
| `FRNAG(3'-O)` | FRNAG, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](F)[C@H](n2cnc3c(=O)[nH]c(N)nc32)O[C@@H]1CO` | 21/23 | C10H11FN5O4 | high |
| `FRNAG(5'-O)` | FRNAG, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2cnc3c(=O)[nH]c(N)nc32)[C@H](F)[C@@H]1O` | 21/23 | C10H11FN5O4 | high |
| `FRNAU(3'-O)` | FRNAU, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](F)[C@H](n2ccc(=O)[nH]c2=O)O[C@@H]1CO` | 18/19 | C9H10FN2O5 | high |
| `FRNAU(5'-O)` | FRNAU, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2ccc(=O)[nH]c2=O)[C@H](F)[C@@H]1O` | 18/19 | C9H10FN2O5 | high |
| `Ft` | (1,3-dihydro-1,3-dioxo-2H-isoindol-2-yl)methyl | `*CN1C(=O)c2ccccc2C1=O` | 13/14 | C9H6NO2 | high |
| `Ggu` | gamma-glutamyl | `*C(=O)CCC(N)C(=O)O` | 10/9 | C5H8NO3 | high |
| `Gla` | gamma-carboxy-L-glutamic acid (C-gamma stereochemistry unspecified) | `O=C([*])[C@@H](N)C(C(=O)O)CC(=O)O` | 13/12 | C6H8NO5 | medium |
| `Glc` | glycolyl | `*C(=O)CO` | 5/4 | C2H3O2 | high |
| `Glc(alpha-D)` | alpha-D-glucopyranosyl, attached at C1 | `*[C@H]1O[C@H](CO)[C@@H](O)[C@H](O)[C@H]1O` | 12/12 | C6H11O5 | high |
| `Glc(beta-D)` | beta-D-glucopyranosyl, attached at C1 | `*[C@@H]1O[C@H](CO)[C@@H](O)[C@H](O)[C@H]1O` | 12/12 | C6H11O5 | high |
| `Gln` | L-glutamine | `N[C@@H](CCC(N)=O)C(=O)[*]` | 10/9 | C5H9N2O2 | high |
| `Glp` | L-pyroglutamyl (5-oxo-L-prolyl) residue | `[*]C(=O)[C@@H]1CCC(=O)N1` | 9/9 | C5H6NO2 | high |
| `Glt` | 4-carboxy-1-oxobutyl | `*C(=O)CCCC(=O)O` | 9/8 | C5H7O3 | high |
| `Glu` | L-glutamic acid residue | `N[C@@H](CCC(=O)O)C(=O)[*]` | 10/9 | C5H8NO3 | high |
| `Gly` | glycine residue | `NCC(=O)[*]` | 5/4 | C2H4NO | high |
| `Har` | L-homoarginine | `N[C@@H](CCCCNC(N)=N)C(=O)[*]` | 13/12 | C7H15N4O | medium |
| `Hcy` | L-homocysteine residue | `N[C@@H](CCS)C(=O)[*]` | 8/7 | C4H8NOS | high |
| `Hex` | n-hexyl | `CCCCCC[*]` | 7/6 | C6H13 | high |
| `Hex(CAS)` | 1-oxohexyl | `*C(=O)CCCCC` | 8/7 | C6H11O | high |
| `Hippuryl` | hippuryl (N-benzoylglycyl) | `O=C(NCC(=O)[*])c1ccccc1` | 13/13 | C9H8NO2 | high |
| `His` | L-histidine | `N[C@@H](Cc1c[nH]cn1)C(=O)[*]` | 11/11 | C6H8N3O | high |
| `Hiv` | 2-hydroxyisovaleryl | `CC(C)C(O)C(=O)[*]` | 8/7 | C5H9O2 | medium |
| `Hmb` | 2-hydroxy-4-methoxybenzyl; supplied Zhihu abbreviation table | `[*]Cc1c(O)cc(OC)cc1` | 11/11 | C8H9O2 | high |
| `Hph` | L-homophenylalanine | `N[C@@H](CCCc1ccccc1)C(=O)[*]` | 14/14 | C11H14NO | medium |
| `Hse` | L-homoserine residue | `N[C@@H](CCO)C(=O)[*]` | 8/7 | C4H8NO2 | high |
| `Hva` | 2-hydroxypentanoyl | `*C(=O)C(O)CCC` | 8/7 | C5H9O2 | high |
| `Hyl` | L-5-hydroxylysine (2S,5R) residue | `N[C@@H](CC[C@@H](O)CN)C(=O)[*]` | 11/10 | C6H13N2O2 | medium |
| `Hyp` | trans-4-hydroxy-L-proline | `O=C([*])[C@@H]1C[C@@H](O)CN1` | 9/9 | C5H8NO2 | medium |
| `i-Am` | isopentyl (3-methylbutan-1-yl) | `CC(C)CC[*]` | 6/5 | C5H11 | high |
| `i-Bu` | isobutyl | `CC(C)C[*]` | 5/4 | C4H9 | high |
| `i-BuO` | isobutoxy | `[*]OCC(C)C` | 6/5 | C4H9O | high |
| `i-BuO(CAS)` | 2-methyl-1-oxopropyl | `*C(=O)C(C)C` | 6/5 | C4H7O | high |
| `i-C3H7` | isopropyl | `CC(C)[*]` | 4/3 | C3H7 | high |
| `i-C4H9` | isobutyl | `CC(C)C[*]` | 5/4 | C4H9 | high |
| `i-C5H11` | isopentyl | `CC(C)CC[*]` | 6/5 | C5H11 | high |
| `i-Pr` | isopropyl | `CC(C)[*]` | 4/3 | C3H7 | high |
| `i-Pro` | 1-methylethyl | `*C(C)C` | 4/3 | C3H7 | high |
| `i-Propyl` | 1-methylethyl | `*C(C)C` | 4/3 | C3H7 | high |
| `iAm` | isopentyl (3-methylbutyl) | `CC(C)CC[*]` | 6/5 | C5H11 | high |
| `iBu` | isobutyl | `CC(C)C[*]` | 5/4 | C4H9 | high |
| `Igl` | 2-indanylglycine residue | `*C(=O)C(N)C1Cc2ccccc2C1` | 14/15 | C11H12NO | high |
| `Ile` | L-isoleucine | `CC[C@H](C)[C@H](N)C(=O)[*]` | 9/8 | C6H12NO | high |
| `Im` | imidazol-1-yl | `[*]n1ccnc1` | 6/6 | C3H3N2 | medium |
| `Ioc` | (2-methylpropoxy)carbonyl | `*C(=O)OCC(C)C` | 8/7 | C5H9O2 | high |
| `Ipa` | 7-methyl-1-oxooctyl | `*C(=O)CCCCCC(C)C` | 11/10 | C9H17O | high |
| `IPDMS` | dimethylisopropylsilyl | `*[Si](C)(C)C(C)C` | 7/6 | C5H13Si | high |
| `iPr` | isopropyl | `CC(C)[*]` | 4/3 | C3H7 | high |
| `Ips` | (4-iodophenyl)sulfonyl | `*S(=O)(=O)c1ccc(I)cc1` | 11/11 | C6H4IO2S | high |
| `Iva` | isovaleryl (3-methylbutanoyl) | `CC(C)CC(=O)[*]` | 7/6 | C5H9O | high |
| `ivDde` | 1-(4,4-dimethyl-2,6-dioxocyclohex-1-ylidene)-3-methylbutyl; attaches to the protected amine N | `[*]C(CC(C)C)=C1C(=O)CC(C)(C)CC1=O` | 16/16 | C13H19O2 | high |
| `Kpc` | (6-oxo-2-piperidinyl)carbonyl | `*C(=O)C1CCCC(=O)N1` | 10/10 | C6H8NO2 | high |
| `Lac` | lactyl (2-hydroxypropanoyl) | `CC(O)C(=O)[*]` | 6/5 | C3H5O2 | medium |
| `lBop` | 2-benzyloxyphenoxy | `*Oc1ccccc1OCc1ccccc1` | 16/17 | C13H11O2 | high |
| `Leu` | L-leucine residue | `N[C@@H](CC(C)C)C(=O)[*]` | 9/8 | C6H12NO | high |
| `Li` | lithium (organolithium, Li-bound) | `[*][Li]` | 2/1 | Li | high |
| `Lys` | L-lysine | `N[C@@H](CCCCN)C(=O)[*]` | 10/9 | C6H13N2O | high |
| `m-Tolyl` | 3-methylphenyl | `[*]c1cccc(C)c1` | 8/8 | C7H7 | high |
| `Maa` | mercaptoacetyl | `*C(=O)CS` | 5/4 | C2H3OS | high |
| `Mal` | 3-carboxy-1-oxo-2-propenyl | `*C(=O)C=CC(=O)O` | 8/7 | C4H3O3 | high |
| `Mbh` | bis(4-methoxyphenyl)methyl | `*C(c1ccc(OC)cc1)c1ccc(OC)cc1` | 18/19 | C15H15O2 | high |
| `Mbs` | 4-methoxybenzenesulfonyl | `*S(=O)(=O)c1ccc(OC)cc1` | 12/12 | C7H7O3S | high |
| `MDIPS` | methyldiisopropylsilyl | `*[Si](C)(C(C)C)C(C)C` | 9/8 | C7H17Si | high |
| `MDPS` | methyldiphenylsilyl | `*[Si](C)(c1ccccc1)c1ccccc1` | 15/16 | C13H13Si | high |
| `Me` | methyl | `C[*]` | 2/1 | CH3 | high |
| `MeAla` | N-methyl-L-alanine residue | `CN[C@@H](C)C(=O)[*]` | 7/6 | C4H8NO | medium |
| `MEM` | 2-methoxyethoxymethyl | `COCCOC[*]` | 7/6 | C4H9O2 | high |
| `MeOe` | 2-methoxy-2-oxoethyl | `*CC(=O)OC` | 6/5 | C3H5O2 | high |
| `Mes` | mesityl (2,4,6-trimethylphenyl) | `[*]c1c(C)cc(C)cc1C` | 10/10 | C9H11 | high |
| `Mes*` | 2,4,6-tri-tert-butylphenyl; star is part of the nickname, not a radical | `[*]c1c(C(C)(C)C)cc(C(C)(C)C)cc1C(C)(C)C` | 19/19 | C18H29 | high |
| `Mesityl` | mesityl (2,4,6-trimethylphenyl) | `*c1c(C)cc(C)cc1C` | 10/10 | C9H11 | high |
| `Mesyl` | methylsulfonyl | `*S(C)(=O)=O` | 5/4 | CH3O2S | high |
| `Met` | L-methionine | `CSCC[C@H](N)C(=O)[*]` | 9/8 | C5H10NOS | high |
| `Methoxy` | methoxy | `*OC` | 3/2 | CH3O | high |
| `MgBr` | magnesium bromide (Grignard, Mg-bound) | `[*][Mg]Br` | 3/2 | BrMg | high |
| `MgCl` | magnesium chloride (Grignard, Mg-bound) | `[*][Mg]Cl` | 3/2 | ClMg | high |
| `MgI` | magnesium iodide (Grignard, Mg-bound) | `[*][Mg]I` | 3/2 | IMg | high |
| `Mhoc` | [(1-methylcyclohexyl)oxy]carbonyl | `*C(=O)OC1(C)CCCCC1` | 11/11 | C8H13O2 | high |
| `Mhp` | 3-hydroxy-4-methylproline residue | `*C(=O)C1NCC(C)C1O` | 10/10 | C6H10NO2 | high |
| `Mmt` | 4-methoxytrityl (monomethoxytrityl) | `[*]C(c1ccccc1)(c1ccccc1)c1ccc(OC)cc1` | 22/24 | C20H17O | high |
| `MMTr` | 4-methoxytrityl | `COc1ccc(C(c2ccccc2)(c2ccccc2)[*])cc1` | 22/24 | C20H17O | medium |
| `MOB` | (4-methoxyphenyl)methyl | `*Cc1ccc(OC)cc1` | 10/10 | C8H9O | high |
| `MOM` | methoxymethyl | `COC[*]` | 4/3 | C2H5O | high |
| `MOS` | (4-methoxyphenyl)sulfonyl | `*S(=O)(=O)c1ccc(OC)cc1` | 12/12 | C7H7O3S | high |
| `Moz` | 4-methoxybenzyloxycarbonyl | `COc1ccc(COC(=O)[*])cc1` | 13/13 | C9H9O3 | medium |
| `Mpa` | 3-mercaptopropionyl | `SCC(=O)[*]` | 5/4 | C2H3OS | high |
| `MPM` | 4-methoxybenzyl | `COc1ccc(C[*])cc1` | 10/10 | C8H9O | high |
| `MPM(PDF)` | methoxy(phenylthio)methyl | `*C(OC)Sc1ccccc1` | 11/11 | C8H9OS | high |
| `Mpt` | dimethylphosphinothioyl | `*P(C)(C)=S` | 5/4 | C2H6PS | high |
| `Ms` | methanesulfonyl | `CS(=O)(=O)[*]` | 5/4 | CH3O2S | high |
| `Msc` | 2-(methylsulfonyl)ethyloxycarbonyl | `CS(=O)(=O)CCOC(=O)[*]` | 10/9 | C4H7O4S | medium |
| `Msi` | methylsulfinyl | `*S(C)=O` | 4/3 | CH3OS | high |
| `Msp` | 4-(methylsulfonyl)phenyl | `*c1ccc(S(C)(=O)=O)cc1` | 11/11 | C7H7O2S | high |
| `MTM` | methylthiomethyl | `CSC[*]` | 4/3 | C2H5S | high |
| `Mtos` | (2,4,6-trimethoxyphenyl)sulfonyl | `*S(=O)(=O)c1c(OC)cc(OC)cc1OC` | 16/16 | C9H11O5S | high |
| `Mtp` | 4-(methylthio)phenyl | `*c1ccc(SC)cc1` | 9/9 | C7H7S | high |
| `Mts` | mesitylenesulfonyl (2,4,6-trimethylbenzenesulfonyl) | `[*]S(=O)(=O)c1c(C)cc(C)cc1C` | 13/13 | C9H11O2S | high |
| `Mtt` | 4-methyltrityl | `Cc1ccc(C(c2ccccc2)(c2ccccc2)[*])cc1` | 21/23 | C20H17 | medium |
| `Myr` | myristoyl (n-tetradecanoyl) | `CCCCCCCCCCCCCC(=O)[*]` | 16/15 | C14H27O | high |
| `Mz` | [[4-[(4-methoxyphenyl)azo]phenyl]methoxy]carbonyl | `*C(=O)OCc1ccc(N=Nc2ccc(OC)cc2)cc1` | 21/22 | C15H13N2O3 | high |
| `n-Am` | n-pentyl | `CCCCC[*]` | 6/5 | C5H11 | high |
| `n-Bu` | n-butyl | `CCCC[*]` | 5/4 | C4H9 | high |
| `n-But` | sec-butyl | `*CCCC` | 5/4 | C4H9 | high |
| `n-Butyl` | sec-butyl | `*CCCC` | 5/4 | C4H9 | high |
| `n-C3H7` | n-propyl | `CCC[*]` | 4/3 | C3H7 | high |
| `n-C4H9` | n-butyl | `CCCC[*]` | 5/4 | C4H9 | high |
| `n-C5H11` | n-pentyl | `CCCCC[*]` | 6/5 | C5H11 | high |
| `n-Heptyl` | n-heptyl, linear isomer assumed | `*CCCCCCC` | 8/7 | C7H15 | high |
| `n-Hexyl` | n-hexyl | `*CCCCCC` | 7/6 | C6H13 | high |
| `n-Pentyl` | n-pentyl | `*CCCCC` | 6/5 | C5H11 | high |
| `n-Pr` | n-propyl | `CCC[*]` | 4/3 | C3H7 | high |
| `n-Pro` | n-propyl | `*CCC` | 4/3 | C3H7 | high |
| `n-Propyl` | n-propyl | `*CCC` | 4/3 | C3H7 | high |
| `N3` | azide | `[N-]=[N+]=N[*]` | 4/3 | N3 | high |
| `Nabs` | [4-[(4-hydroxy-1-naphthalenyl)azo]phenyl]sulfonyl | `*S(=O)(=O)c1ccc(N=Nc2ccc(O)c3ccccc23)cc1` | 23/25 | C16H11N2O3S | high |
| `Nal` | L-3-(1-naphthyl)alanine residue | `N[C@@H](Cc1cccc2ccccc12)C(=O)[*]` | 16/17 | C13H12NO | high |
| `nAm` | n-pentyl | `CCCCC[*]` | 6/5 | C5H11 | high |
| `nBu` | n-butyl | `CCCC[*]` | 5/4 | C4H9 | high |
| `nButyl` | sec-butyl | `*CCCC` | 5/4 | C4H9 | high |
| `neo-Am` | neopentyl (2,2-dimethylpropyl) | `CC(C)(C)C[*]` | 6/5 | C5H11 | medium |
| `neo-C5H11` | neopentyl | `CC(C)(C)C[*]` | 6/5 | C5H11 | high |
| `NEt2` | diethylamino | `CCN(CC)[*]` | 6/5 | C4H10N | high |
| `Nf` | nonafluorobutanesulfonyl | `*S(=O)(=O)C(F)(F)C(F)(F)C(F)(F)C(F)(F)F` | 17/16 | C4F9O2S | high |
| `Ng` | 2-methoxy-4-nitrophenyl | `*c1ccc([N+](=O)[O-])cc1OC` | 12/12 | C7H6NO3 | high |
| `Ngu` | [imino(nitroamino)methyl]amino | `*NC(=N)N[N+](=O)[O-]` | 8/7 | CH3N4O2 | high |
| `NH2` | Common functional group; attachment at * | `*N` | 2/1 | H2N | authored |
| `NHAc` | acetamido | `CC(=O)N[*]` | 5/4 | C2H4NO | high |
| `NHBoc` | tert-butoxycarbonylamino | `[*]NC(=O)OC(C)(C)C` | 9/8 | C5H10NO2 | high |
| `NHCbz` | benzyloxycarbonylamino | `[*]NC(=O)OCc1ccccc1` | 12/12 | C8H8NO2 | high |
| `NHCO` | N-linked formamido group | `*NC=O` | 4/3 | CH2NO | high |
| `nHeptyl` | n-heptyl, linear isomer assumed | `*CCCCCCC` | 8/7 | C7H15 | high |
| `NHEt` | Common functional group; attachment at * | `*NCC` | 4/3 | C2H6N | authored |
| `nHexyl` | n-hexyl | `*CCCCCC` | 7/6 | C6H13 | high |
| `NHFmoc` | Fmoc-amino (NHFmoc) | `O=C(N[*])OCC1c2ccccc2c2ccccc21` | 19/21 | C15H12NO2 | high |
| `NHMe` | Common functional group; attachment at * | `*NC` | 3/2 | CH4N | authored |
| `NHNH2` | hydrazino | `NN[*]` | 3/2 | H3N2 | high |
| `NHTs` | Common functional group; attachment at * | `*NS(=O)(=O)c1ccc(C)cc1` | 12/12 | C7H8NO2S | authored |
| `NHZ` | [(phenylmethoxy)carbonyl]amino | `*NC(=O)OCc1ccccc1` | 12/12 | C8H8NO2 | high |
| `Nis` | (4-nitrophenyl)sulfonyl | `*S(=O)(=O)c1ccc([N+](=O)[O-])cc1` | 13/13 | C6H4NO4S | high |
| `Nle` | L-norleucine residue | `N[C@@H](CCCC)C(=O)[*]` | 9/8 | C6H12NO | high |
| `Nm` | 3-nitrophenyl | `*c1cccc([N+](=O)[O-])c1` | 10/10 | C6H4NO2 | high |
| `NMe` | Common functional group | `*NC` | 3/2 | CH4N | high |
| `NMe2` | dimethylamino | `CN(C)[*]` | 4/3 | C2H6N | high |
| `NO` | Common functional group; attachment at * | `*N=O` | 3/2 | NO | authored |
| `No` | 2-nitrophenyl | `*c1ccccc1[N+](=O)[O-]` | 10/10 | C6H4NO2 | high |
| `NO2` | Common functional group; attachment at * | `*[N+](=O)[O-]` | 4/3 | NO2 | authored |
| `Nos` | 4-nitrobenzenesulfonyl; distinct from ortho-Ns; supplied Zhihu abbreviation table | `[*]S(=O)(=O)c1ccc([N+](=O)[O-])cc1` | 13/13 | C6H4NO4S | high |
| `Np` | 1-naphthyl | `[*]c1cccc2ccccc12` | 11/12 | C10H7 | medium |
| `Np(CAS)` | 4-nitrophenyl | `*c1ccc([N+](=O)[O-])cc1` | 10/10 | C6H4NO2 | high |
| `Npe` | 2-nitro-1-phenylethyl | `*C(C[N+](=O)[O-])c1ccccc1` | 12/12 | C8H8NO2 | high |
| `nPentyl` | n-pentyl | `*CCCCC` | 6/5 | C5H11 | high |
| `Nphe` | N-benzylglycine residue (C-terminal attachment) | `*C(=O)CNCc1ccccc1` | 12/12 | C9H10NO | high |
| `NPPOC` | 2-(2-nitrophenyl)propoxycarbonyl; stereochemistry unspecified | `[*]C(=O)OCC(C)c1ccccc1[N+](=O)[O-]` | 16/16 | C10H10NO4 | high |
| `nPr` | n-propyl | `CCC[*]` | 4/3 | C3H7 | high |
| `nPropyl` | n-propyl | `*CCC` | 4/3 | C3H7 | high |
| `Nps` | 2-nitrophenylsulfenyl | `[O-][N+](=O)c1ccccc1S[*]` | 11/11 | C6H4NO2S | medium |
| `Ns` | 2-nitrobenzenesulfonyl (nosyl) | `[*]S(=O)(=O)c1ccccc1[N+](=O)[O-]` | 13/13 | C6H4NO4S | high |
| `Ns(CAS)` | 2-nitro-4-sulfophenyl | `*c1ccc(S(=O)(=O)O)cc1[N+](=O)[O-]` | 14/14 | C6H4NO5S | high |
| `Nva` | L-norvaline | `CCC[C@H](N)C(=O)[*]` | 8/7 | C5H10NO | high |
| `NVOC` | 4,5-dimethoxy-2-nitrobenzyloxycarbonyl | `[*]C(=O)OCc1cc(OC)c(OC)cc1[N+](=O)[O-]` | 18/18 | C10H10NO6 | high |
| `o-Tolyl` | 2-methylphenyl | `[*]c1ccccc1C` | 8/8 | C7H7 | high |
| `OAc` | acetoxy | `CC(=O)O[*]` | 5/4 | C2H3O2 | high |
| `OBn` | benzyloxy | `c1ccccc1CO[*]` | 9/9 | C7H7O | high |
| `OBz` | benzoyloxy | `O=C(O[*])c1ccccc1` | 10/10 | C7H5O2 | high |
| `OCF3` | Common functional group; attachment at * | `*OC(F)(F)F` | 6/5 | CF3O | authored |
| `OCOMe` | Common functional group; attachment at * | `*OC(=O)C` | 5/4 | C2H3O2 | authored |
| `Oct` | n-octyl | `CCCCCCCC[*]` | 9/8 | C8H17 | high |
| `Oct(CAS)` | 1-oxooctyl | `*C(=O)CCCCCCC` | 10/9 | C8H15O | high |
| `OEt` | ethoxy | `CCO[*]` | 4/3 | C2H5O | high |
| `OH` | Common functional group; attachment at * | `*O` | 2/1 | HO | authored |
| `OiBu` | isobutoxy | `*OCC(C)C` | 6/5 | C4H9O | high |
| `Oic` | L-octahydroindole-2-carboxyl residue; ring-fusion stereocentres unspecified | `[*]C(=O)[C@@H]1CC2CCCCC2N1` | 12/13 | C9H14NO | medium |
| `OiPr` | Common functional group; attachment at * | `*OC(C)C` | 5/4 | C3H7O | authored |
| `Ole` | oleoyl, Z (cis) double bond | `CCCCCCCC/C=C\CCCCCCCC(=O)[*]` | 20/19 | C18H33O | medium |
| `Ole(CAS)` | 1-oxo-9-octadecenyl | `*C(=O)CCCCCCCC=CCCCCCCCC` | 20/19 | C18H33O | high |
| `OMe` | methoxy | `CO[*]` | 3/2 | CH3O | high |
| `OMeRNAA(3'-O)` | OMeRNAA, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](OC)[C@H](n2cnc3c(N)ncnc32)O[C@@H]1CO` | 21/23 | C11H14N5O4 | high |
| `OMeRNAA(5'-O)` | OMeRNAA, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2cnc3c(N)ncnc32)[C@H](OC)[C@@H]1O` | 21/23 | C11H14N5O4 | high |
| `OMeRNAC(3'-O)` | OMeRNAC, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](OC)[C@H](n2ccc(N)nc2=O)O[C@@H]1CO` | 19/20 | C10H14N3O5 | high |
| `OMeRNAC(5'-O)` | OMeRNAC, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2ccc(N)nc2=O)[C@H](OC)[C@@H]1O` | 19/20 | C10H14N3O5 | high |
| `OMeRNAG(3'-O)` | OMeRNAG, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](OC)[C@H](n2cnc3c(=O)[nH]c(N)nc32)O[C@@H]1CO` | 22/24 | C11H14N5O5 | high |
| `OMeRNAG(5'-O)` | OMeRNAG, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2cnc3c(=O)[nH]c(N)nc32)[C@H](OC)[C@@H]1O` | 22/24 | C11H14N5O5 | high |
| `OMeRNAT(3'-O)` | OMeRNAT, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](OC)[C@H](n2cc(C)c(=O)[nH]c2=O)O[C@@H]1CO` | 20/21 | C11H15N2O6 | high |
| `OMeRNAT(5'-O)` | OMeRNAT, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2cc(C)c(=O)[nH]c2=O)[C@H](OC)[C@@H]1O` | 20/21 | C11H15N2O6 | high |
| `OMeRNAU(3'-O)` | OMeRNAU, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](OC)[C@H](n2ccc(=O)[nH]c2=O)O[C@@H]1CO` | 19/20 | C10H13N2O6 | high |
| `OMeRNAU(5'-O)` | OMeRNAU, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2ccc(=O)[nH]c2=O)[C@H](OC)[C@@H]1O` | 19/20 | C10H13N2O6 | high |
| `OMs` | mesyloxy | `CS(=O)(=O)O[*]` | 6/5 | CH3O3S | high |
| `ONs` | 2-nitrobenzenesulfonate (nosylate, O-linked) | `[*]OS(=O)(=O)c1ccccc1[N+](=O)[O-]` | 14/14 | C6H4NO5S | high |
| `OPh` | Common functional group; attachment at * | `*Oc1ccccc1` | 8/8 | C6H5O | authored |
| `OPiv` | pivalate (O-linked pivaloyl) | `[*]OC(=O)C(C)(C)C` | 8/7 | C5H9O2 | high |
| `Orn` | L-ornithine residue | `N[C@@H](CCCN)C(=O)[*]` | 9/8 | C5H11N2O | high |
| `OTBDMS` | Common functional group; attachment at * | `*O[Si](C)(C)C(C)(C)C` | 9/8 | C6H15OSi | authored |
| `OTBDPS` | Common functional group; attachment at * | `*O[Si](c1ccccc1)(c1ccccc1)C(C)(C)C` | 19/20 | C16H19OSi | authored |
| `OTBS` | Common functional group; attachment at * | `*O[Si](C)(C)C(C)(C)C` | 9/8 | C6H15OSi | authored |
| `OtBu` | Common functional group; attachment at * | `*OC(C)(C)C` | 6/5 | C4H9O | authored |
| `OTES` | Common functional group; attachment at * | `*O[Si](CC)(CC)CC` | 9/8 | C6H15OSi | authored |
| `OTf` | trifluoromethanesulfonyloxy (triflate) | `FC(F)(F)S(=O)(=O)O[*]` | 9/8 | CF3O3S | high |
| `OTIPS` | Common functional group; attachment at * | `*O[Si](C(C)C)(C(C)C)C(C)C` | 12/11 | C9H21OSi | authored |
| `OTMS` | Common functional group; attachment at * | `*O[Si](C)(C)C` | 6/5 | C3H9OSi | authored |
| `OTs` | tosyloxy | `Cc1ccc(S(=O)(=O)O[*])cc1` | 12/12 | C7H7O3S | high |
| `p-Tolyl` | 4-methylphenyl | `[*]c1ccc(C)cc1` | 8/8 | C7H7 | high |
| `p-Tos` | tosyl (4-methylbenzenesulfonyl) | `*S(=O)(=O)c1ccc(C)cc1` | 11/11 | C7H7O2S | high |
| `p-Tosyl` | tosyl (4-methylbenzenesulfonyl) | `*S(=O)(=O)c1ccc(C)cc1` | 11/11 | C7H7O2S | high |
| `Pa` | 1-oxononyl | `*C(=O)CCCCCCCC` | 11/10 | C9H17O | high |
| `Pal` | 1-oxohexadecyl | `*C(=O)CCCCCCCCCCCCCCC` | 18/17 | C16H31O | high |
| `PBB` | 4-bromobenzyl | `[*]Cc1ccc(Br)cc1` | 9/9 | C7H6Br | high |
| `Pbf` | 2,2,4,6,7-pentamethyldihydrobenzofuran-5-sulfonyl | `CC1(C)Cc2c(C)c(S(=O)(=O)[*])c(C)c(C)c2O1` | 18/19 | C13H17O3S | medium |
| `Pbp` | pentabromophenyl | `*c1c(Br)c(Br)c(Br)c(Br)c1Br` | 12/12 | C6Br5 | high |
| `PCB` | 4-chlorobenzyl | `[*]Cc1ccc(Cl)cc1` | 9/9 | C7H6Cl | high |
| `Pcp` | pentachlorophenyl | `[*]c1c(Cl)c(Cl)c(Cl)c(Cl)c1Cl` | 12/12 | C6Cl5 | medium |
| `Pen` | L-penicillamine residue | `N[C@@H](C(C)(C)S)C(=O)[*]` | 9/8 | C5H10NOS | high |
| `Pf` | 9-phenylfluoren-9-yl | `*C1(c2ccccc2)c2ccccc2-c2ccccc21` | 20/23 | C19H13 | high |
| `Pfp` | pentafluorophenyl | `[*]c1c(F)c(F)c(F)c(F)c1F` | 12/12 | C6F5 | high |
| `pGlu` | L-pyroglutamic acid (5-oxoproline) | `O=C([*])[C@@H]1CCC(=O)N1` | 9/9 | C5H6NO2 | high |
| `Ph` | phenyl | `[*]c1ccccc1` | 7/7 | C6H5 | high |
| `Phe` | L-phenylalanine | `N[C@@H](Cc1ccccc1)C(=O)[*]` | 12/12 | C9H10NO | high |
| `Phenyl` | phenyl | `[*]c1ccccc1` | 7/7 | C6H5 | high |
| `Phg` | L-phenylglycine | `N[C@@H](c1ccccc1)C(=O)[*]` | 11/11 | C8H8NO | high |
| `PhSO2` | benzenesulfonyl | `*S(=O)(=O)c1ccccc1` | 10/10 | C6H5O2S | high |
| `Pht` | phthaloyl | `[*]N1C(=O)c2ccccc2C1=O` | 12/13 | C8H4NO2 | high |
| `Pht(CAS)` | 2-carboxybenzoyl | `*C(=O)c1ccccc1C(=O)O` | 12/12 | C8H5O3 | high |
| `Pic` | 4-pyridinylmethyl | `*Cc1ccncc1` | 8/8 | C6H6N | high |
| `picryl` | picryl | `*c1c([N+](=O)[O-])cc([N+](=O)[O-])cc1[N+](=O)[O-]` | 16/16 | C6H2N3O6 | high |
| `pin` | pinacol O-ether (pinacolato O-linked); for the boronate use Bpin | `[*]OC(C)(C)C(C)(C)O` | 9/8 | C6H13O2 | medium |
| `Pip` | piperidin-1-yl | `[*]N1CCCCC1` | 7/7 | C5H10N | medium |
| `Pipoc` | (1-piperidinyloxy)carbonyl | `*C(=O)ON1CCCCC1` | 10/10 | C6H10NO2 | high |
| `Piv` | pivaloyl | `CC(C)(C)C(=O)[*]` | 7/6 | C5H9O | high |
| `Pivaloyl` | pivaloyl | `*C(=O)C(C)(C)C` | 7/6 | C5H9O | high |
| `PMB` | 4-methoxybenzyl | `COc1ccc(C[*])cc1` | 10/10 | C8H9O | high |
| `PMBM` | 4-methoxybenzyloxymethyl | `[*]COCc1ccc(OC)cc1` | 12/12 | C9H11O2 | high |
| `Pmc` | 2,2,5,7,8-pentamethylchroman-6-sulfonyl | `CC1(C)CCc2c(C)c(S(=O)(=O)[*])c(C)c(C)c2O1` | 19/20 | C14H19O3S | medium |
| `PMP` | 4-methoxyphenyl; not PMB (4-methoxybenzyl) | `[*]c1ccc(OC)cc1` | 9/9 | C7H7O | high |
| `PNB` | 4-nitrobenzyl | `[O-][N+](=O)c1ccc(C[*])cc1` | 11/11 | C7H6NO2 | high |
| `Pnb` | (4-nitrophenyl)methyl | `*Cc1ccc([N+](=O)[O-])cc1` | 11/11 | C7H6NO2 | high |
| `PNZ` | 4-nitrobenzyloxycarbonyl; supplied Zhihu abbreviation table | `[*]C(=O)OCc1ccc([N+](=O)[O-])cc1` | 14/14 | C8H6NO4 | high |
| `PO2` | phosphono | `*P(=O)(O)O` | 5/4 | H2O3P | high |
| `PO3H2` | phosphono | `[*]P(=O)(O)O` | 5/4 | H2O3P | high |
| `Poc` | (cyclopentyloxy)carbonyl | `*C(=O)OC1CCCC1` | 9/9 | C6H9O2 | high |
| `PPh2` | diphenylphosphino | `[*]P(c1ccccc1)c1ccccc1` | 14/15 | C12H10P | high |
| `PPi` | pyrophosphate (triply charged P-linked form) | `*P(=O)([O-])OP(=O)([O-])[O-]` | 9/8 | O6P2-3 | high |
| `Ppt` | diphenylphosphinothioyl | `*P(=S)(c1ccccc1)c1ccccc1` | 15/16 | C12H10PS | high |
| `Pr` | n-propyl | `CCC[*]` | 4/3 | C3H7 | high |
| `Pro` | L-proline | `O=C([*])[C@@H]1CCCN1` | 8/8 | C5H8NO | high |
| `pS` | O-phospho-L-serine | `N[C@@H](COP(=O)(O)O)C(=O)[*]` | 11/10 | C3H7NO5P | medium |
| `pSer` | O-phospho-L-serine residue | `N[C@@H](COP(=O)(O)O)C(=O)[*]` | 11/10 | C3H7NO5P | high |
| `Ptc` | (phenylamino)thioxomethyl | `*C(=S)Nc1ccccc1` | 10/10 | C7H6NS | high |
| `pThr` | O-phospho-L-threonine | `C[C@@H](OP(=O)(O)O)[C@H](N)C(=O)[*]` | 12/11 | C4H9NO5P | medium |
| `PTMSE` | 2-phenyl-2-trimethylsilylethyl | `*CC(c1ccccc1)[Si](C)(C)C` | 13/13 | C11H17Si | high |
| `pTyr` | O-phospho-L-tyrosine residue | `N[C@@H](Cc1ccc(OP(=O)(O)O)cc1)C(=O)[*]` | 17/17 | C9H11NO5P | high |
| `Pv` | pivaloyl | `CC(C)(C)C(=O)[*]` | 7/6 | C5H9O | high |
| `Py(CAS)` | 2-pyridinyl | `*c1ccccn1` | 7/7 | C5H4N | high |
| `Pyl` | L-Pyrrolysine residue (C-terminal attachment) | `*C(=O)[C@@H](N)CCCCNC(=O)[C@@H]1N=CC[C@H]1C` | 18/18 | C12H20N3O2 | high |
| `pyrazol-1-yl` | pyrazol-1-yl | `*n1nccc1` | 6/6 | C3H3N2 | high |
| `pyrazol-3-yl` | pyrazol-3-yl | `*c1cc[nH]n1` | 6/6 | C3H3N2 | high |
| `Pz` | [[4-(phenylazo)phenyl]methoxy]carbonyl | `*C(=O)OCc1ccc(N=Nc2ccccc2)cc1` | 19/20 | C14H11N2O2 | high |
| `QC` | 5-chloro-8-quinolinyl | `*c1ccc(Cl)c2cccnc12` | 12/13 | C9H5ClN | high |
| `Qu` | 8-quinolinyl | `*c1cccc2cccnc12` | 11/12 | C9H6N | high |
| `Qxc` | 2-quinoxalinylcarbonyl | `*C(=O)c1cnc2ccccc2n1` | 13/14 | C9H5N2O | high |
| `RNAA(3'-O)` | RNAA, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](O)[C@H](n2cnc3c(N)ncnc32)O[C@@H]1CO` | 20/22 | C10H12N5O4 | high |
| `RNAA(5'-O)` | RNAA, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2cnc3c(N)ncnc32)[C@H](O)[C@@H]1O` | 20/22 | C10H12N5O4 | high |
| `RNAC(3'-O)` | RNAC, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](O)[C@H](n2ccc(N)nc2=O)O[C@@H]1CO` | 18/19 | C9H12N3O5 | high |
| `RNAC(5'-O)` | RNAC, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2ccc(N)nc2=O)[C@H](O)[C@@H]1O` | 18/19 | C9H12N3O5 | high |
| `RNAG(3'-O)` | RNAG, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](O)[C@H](n2cnc3c(=O)[nH]c(N)nc32)O[C@@H]1CO` | 21/23 | C10H12N5O5 | high |
| `RNAG(5'-O)` | RNAG, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2cnc3c(=O)[nH]c(N)nc32)[C@H](O)[C@@H]1O` | 21/23 | C10H12N5O5 | high |
| `RNAU(3'-O)` | RNAU, 3'-O attachment; nucleoside, no phosphate | `*O[C@H]1[C@@H](O)[C@H](n2ccc(=O)[nH]c2=O)O[C@@H]1CO` | 18/19 | C9H11N2O6 | high |
| `RNAU(5'-O)` | RNAU, 5'-O attachment; nucleoside, no phosphate | `*OC[C@H]1O[C@@H](n2ccc(=O)[nH]c2=O)[C@H](O)[C@@H]1O` | 18/19 | C9H11N2O6 | high |
| `rT(3'-O)` | rT, 3'-O attachment; no phosphate | `*O[C@H]1[C@@H](O)[C@H](n2cc(C)c(=O)[nH]c2=O)O[C@@H]1CO` | 19/20 | C10H13N2O6 | high |
| `rT(5'-O)` | rT, 5'-O attachment; no phosphate | `*OC[C@H]1O[C@@H](n2cc(C)c(=O)[nH]c2=O)[C@H](O)[C@@H]1O` | 19/20 | C10H13N2O6 | high |
| `s-Am` | sec-pentyl (pentan-2-yl) | `[*]C(C)CCC` | 6/5 | C5H11 | medium |
| `s-Bu` | sec-butyl | `[*]C(C)CC` | 5/4 | C4H9 | high |
| `s-Butyl` | sec-butyl | `C(C)CC[*]` | 5/4 | C4H9 | high |
| `s-C4H9` | sec-butyl | `[*]C(C)CC` | 5/4 | C4H9 | high |
| `s-C5H11` | sec-pentyl (pentan-2-yl) | `C(C)CCC[*]` | 6/5 | C5H11 | medium |
| `sAm` | sec-amyl (pentan-2-yl) | `[*]C(C)CCC` | 6/5 | C5H11 | medium |
| `Sar` | sarcosine (N-methylglycine) | `CNCC(=O)[*]` | 6/5 | C3H6NO | high |
| `sBu` | sec-butyl | `[*]C(C)CC` | 5/4 | C4H9 | high |
| `Sbz` | 2-sulfobenzoyl | `*C(=O)c1ccccc1S(=O)(=O)O` | 13/13 | C7H5O4S | high |
| `SCF3` | Common functional group; attachment at * | `*SC(F)(F)F` | 6/5 | CF3S | authored |
| `Scm` | (carboxymethyl)thio | `*SCC(=O)O` | 6/5 | C2H3O2S | high |
| `Sec` | L-selenocysteine residue | `N[C@@H](C[SeH])C(=O)[*]` | 7/6 | C3H6NOSe | medium |
| `SEM` | 2-(trimethylsilyl)ethoxymethyl | `C[Si](C)(C)CCOC[*]` | 9/8 | C6H15OSi | high |
| `SePh` | phenylselenyl | `[*][Se]c1ccccc1` | 8/8 | C6H5Se | high |
| `Ser` | L-serine | `N[C@@H](CO)C(=O)[*]` | 7/6 | C3H6NO2 | high |
| `SES` | 2-(trimethylsilyl)ethanesulfonyl | `C[Si](C)(C)CCS(=O)(=O)[*]` | 10/9 | C5H13O2SSi | high |
| `SEt` | ethylthio | `CCS[*]` | 4/3 | C2H5S | high |
| `SH` | Common functional group; attachment at * | `*S` | 2/1 | HS | authored |
| `Sia` | 1,2-dimethylpropyl (secondary isoamyl); visually checked against supplied PDF | `[*]C(C)C(C)C` | 6/5 | C5H11 | high |
| `SiPh3` | triphenylsilyl; explicit synonym of TPS | `[*][Si](c1ccccc1)(c1ccccc1)c1ccccc1` | 20/22 | C18H15Si | high |
| `SMe` | methylthio | `CS[*]` | 3/2 | CH3S | high |
| `SnBu3` | tributylstannyl | `[*][Sn](CCCC)(CCCC)CCCC` | 14/13 | C12H27Sn | high |
| `SO2CF3` | Common functional group; attachment at * | `*S(=O)(=O)C(F)(F)F` | 8/7 | CF3O2S | authored |
| `SO2Me` | methylsulfonyl | `CS(=O)(=O)[*]` | 5/4 | CH3O2S | high |
| `SO2NH2` | sulfamoyl | `NS(=O)(=O)[*]` | 5/4 | H2NO2S | high |
| `SO2Ph` | Common functional group; attachment at * | `*S(=O)(=O)c1ccccc1` | 10/10 | C6H5O2S | authored |
| `SO3H` | sulfo | `[*]S(=O)(=O)O` | 5/4 | HO3S | high |
| `SOCH3` | Common functional group; attachment at * | `*S(=O)C` | 4/3 | CH3OS | authored |
| `SOPh` | Common functional group; attachment at * | `*S(=O)c1ccccc1` | 9/9 | C6H5OS | authored |
| `Spg` | 1-aminocyclopentanecarbonyl | `*C(=O)C1(N)CCCC1` | 9/9 | C6H10NO | high |
| `SPh` | phenylthio | `[*]Sc1ccccc1` | 8/8 | C6H5S | high |
| `SPy` | 2-pyridylthio | `[*]Sc1ccccn1` | 8/8 | C5H4NS | medium |
| `Sta` | statine, (3S,4S)-4-amino-3-hydroxy-6-methylheptanoic acid | `CC(C)C[C@H](N)[C@@H](O)CC(=O)[*]` | 12/11 | C8H16NO2 | medium |
| `Su` | succinimidyl (N-hydroxysuccinimidyl) | `[*]N1C(=O)CCC1=O` | 8/8 | C4H4NO2 | high |
| `Suc` | 3-carboxy-1-oxopropyl | `*C(=O)CCC(=O)O` | 8/7 | C4H5O3 | high |
| `t-Am` | tert-amyl | `[*]C(C)(C)CC` | 6/5 | C5H11 | high |
| `t-BOC` | tert-butoxycarbonyl | `CC(C)(C)OC(=O)[*]` | 8/7 | C5H9O2 | high |
| `t-Boc` | (1,1-dimethylethoxy)carbonyl | `*C(=O)OC(C)(C)C` | 8/7 | C5H9O2 | high |
| `t-Bu` | tert-butyl | `CC(C)(C)[*]` | 5/4 | C4H9 | high |
| `t-Butyl` | tert-butyl | `CC(C)(C)[*]` | 5/4 | C4H9 | high |
| `t-C4H9` | tert-butyl | `CC(C)(C)[*]` | 5/4 | C4H9 | high |
| `t-C5H11` | tert-pentyl (2-methylbutan-2-yl) | `C(C)(C)CC[*]` | 6/5 | C5H11 | high |
| `Tac` | [[(4-methylphenyl)sulfonyl]amino]carbonyl | `*C(=O)NS(=O)(=O)c1ccc(C)cc1` | 14/14 | C8H8NO3S | high |
| `tAm` | tert-amyl (2-methylbutan-2-yl) | `[*]C(C)(C)CC` | 6/5 | C5H11 | high |
| `TBDMS` | tert-butyldimethylsilyl | `[*][Si](C)(C)C(C)(C)C` | 8/7 | C6H15Si | high |
| `TBDPS` | tert-butyldiphenylsilyl | `CC(C)(C)[Si](c1ccccc1)(c1ccccc1)[*]` | 18/19 | C16H19Si | high |
| `TBMPS` | tert-butylmethoxyphenylsilyl | `*[Si](OC)(c1ccccc1)C(C)(C)C` | 14/14 | C11H17OSi | high |
| `TBS` | tert-butyldimethylsilyl | `CC(C)(C)[Si](C)(C)[*]` | 8/7 | C6H15Si | high |
| `Tbs` | (1,1-dimethylethyl)dimethylsilyl | `*[Si](C)(C)C(C)(C)C` | 8/7 | C6H15Si | high |
| `TBT` | 1-tert-butyl-1H-tetrazol-5-yl | `*c1nnnn1C(C)(C)C` | 10/10 | C5H9N4 | high |
| `Tbt` | 2,4,6-tris[bis(trimethylsilyl)methyl]phenyl | `[*]c1c(C([Si](C)(C)C)[Si](C)(C)C)cc(C([Si](C)(C)C)[Si](C)(C)C)cc1C([Si](C)(C)C)[Si](C)(C)C` | 34/34 | C27H59Si6 | high |
| `tBu` | tert-butyl | `CC(C)(C)[*]` | 5/4 | C4H9 | high |
| `TBZ` | phenylthioxomethyl | `*C(=S)c1ccccc1` | 9/9 | C7H5S | high |
| `Tcboc` | (2,2,2-trichloro-1,1-dimethylethoxy)carbonyl | `*C(=O)OC(C)(C)C(Cl)(Cl)Cl` | 11/10 | C5H6Cl3O2 | high |
| `Tce` | 2,2,2-trichloroethyl | `ClC(Cl)(Cl)C[*]` | 6/5 | C2H2Cl3 | medium |
| `Tcp` | 2,4,5-trichlorophenyl | `[*]c1c(Cl)cc(Cl)c(Cl)c1` | 10/10 | C6H2Cl3 | medium |
| `TDS` | thexyldimethylsilyl | `C[Si](C)(C(C)(C)C(C)C)[*]` | 10/9 | C8H19Si | high |
| `Tec` | [2-[(4-methylphenyl)sulfonyl]ethoxy]carbonyl | `*C(=O)OCCS(=O)(=O)c1ccc(C)cc1` | 16/16 | C10H11O4S | high |
| `Teoc` | 2-(trimethylsilyl)ethoxycarbonyl | `C[Si](C)(C)CCOC(=O)[*]` | 10/9 | C6H13O2Si | high |
| `Teoc(CAS)` | (2,2,2-trichloroethoxy)carbonyl | `*C(=O)OCC(Cl)(Cl)Cl` | 9/8 | C3H2Cl3O2 | high |
| `TES` | triethylsilyl | `[*][Si](CC)(CC)CC` | 8/7 | C6H15Si | high |
| `Tf` | trifluoromethanesulfonyl | `FC(F)(F)S(=O)(=O)[*]` | 8/7 | CF3O2S | high |
| `TFA` | trifluoroacetyl | `FC(F)(F)C(=O)[*]` | 7/6 | C2F3O | high |
| `Tfe` | 2,2,2-trifluoroethyl | `FC(F)(F)C[*]` | 6/5 | C2H2F3 | high |
| `Tfp` | 2,2,3,3-tetrafluoro-1-oxopropyl | `*C(=O)C(F)(F)C(F)F` | 9/8 | C3HF4O | high |
| `Th` | thiophen-2-yl; supplied Zhihu abbreviation table | `[*]c1cccs1` | 6/6 | C4H3S | high |
| `Thexyl` | thexyl (2,3-dimethylbutan-2-yl) | `CC(C)C(C)(C)[*]` | 7/6 | C6H13 | high |
| `thexyl` | 1,1,2-trimethylpropyl; visually checked against supplied PDF | `[*]C(C)(C)C(C)C` | 7/6 | C6H13 | high |
| `Thi` | thiophen-3-ylalanine residue | `*C(=O)C(N)Cc1ccsc1` | 11/11 | C7H8NOS | high |
| `THP` | tetrahydropyran-2-yl | `[*]C1CCCCO1` | 7/7 | C5H9O | high |
| `Thr` | L-threonine | `C[C@@H](O)[C@H](N)C(=O)[*]` | 8/7 | C4H8NO2 | high |
| `Thz` | Timonacic, (S)- residue (C-terminal attachment) | `*C(=O)[C@H]1CSCN1` | 8/8 | C4H6NOS | high |
| `Ti(OEt)3` | Authored metal-centred alkoxide attachment group | `[*][Ti](OCC)(OCC)OCC` | 11/10 | C6H15O3Ti | high |
| `Ti(Oi-Pr)3` | Authored metal-centred alkoxide attachment group | `[*][Ti](OC(C)C)(OC(C)C)OC(C)C` | 14/13 | C9H21O3Ti | high |
| `Ti(Ot-Bu)3` | Authored metal-centred alkoxide attachment group | `[*][Ti](OC(C)(C)C)(OC(C)(C)C)OC(C)(C)C` | 17/16 | C12H27O3Ti | high |
| `Tic` | L-1,2,3,4-tetrahydroisoquinoline-3-carboxylic acid | `O=C([*])[C@@H]1Cc2ccccc2CN1` | 13/14 | C10H10NO | medium |
| `Tipp` | 2,4,6-triisopropylphenyl | `[*]c1c(C(C)C)cc(C(C)C)cc1C(C)C` | 16/16 | C15H23 | high |
| `TIPS` | triisopropylsilyl | `CC(C)[Si](C(C)C)(C(C)C)[*]` | 11/10 | C9H21Si | high |
| `Tle` | L-tert-leucine residue | `N[C@@H](C(C)(C)C)C(=O)[*]` | 9/8 | C6H12NO | high |
| `Tmb` | (2,4,6-trimethylphenyl)methyl | `*Cc1c(C)cc(C)cc1C` | 11/11 | C10H13 | high |
| `Tmob` | 2,4,6-trimethoxybenzyl | `COc1cc(OC)c(C[*])c(OC)c1` | 14/14 | C10H13O3 | medium |
| `TMS` | trimethylsilyl | `C[Si](C)(C)[*]` | 5/4 | C3H9Si | high |
| `TMSE` | 2-trimethylsilylethyl | `*CC[Si](C)(C)C` | 7/6 | C5H13Si | high |
| `TNP` | 2,4,6-trinitrophenyl (picryl) | `[O-][N+](=O)c1cc([N+](=O)[O-])cc([N+](=O)[O-])c1[*]` | 16/16 | C6H2N3O6 | medium |
| `Tos` | p-toluenesulfonyl (tosyl) | `Cc1ccc(S(=O)(=O)[*])cc1` | 11/11 | C7H7O2S | high |
| `Tosa` | [(4-methylphenyl)sulfonyl]amino | `*NS(=O)(=O)c1ccc(C)cc1` | 12/12 | C7H8NO2S | high |
| `TPS` | triphenylsilyl | `[*][Si](c1ccccc1)(c1ccccc1)c1ccccc1` | 20/22 | C18H15Si | high |
| `Tr` | trityl (triphenylmethyl) | `[*]C(c1ccccc1)(c1ccccc1)c1ccccc1` | 20/22 | C19H15 | high |
| `trans-Cinnamyl` | (E)-cinnamyl | `[*]C/C=C/c1ccccc1` | 10/10 | C9H9 | high |
| `Trit` | trityl | `c1ccccc1C(c1ccccc1)(c1ccccc1)[*]` | 20/22 | C19H15 | high |
| `Troc` | 2,2,2-trichloroethoxycarbonyl | `ClC(Cl)(Cl)CCOC(=O)[*]` | 10/9 | C4H4Cl3O2 | high |
| `Trp` | L-tryptophan | `N[C@@H](Cc1c[nH]c2ccccc12)C(=O)[*]` | 15/16 | C11H11N2O | high |
| `Trs` | (triphenylmethyl)thio | `*SC(c1ccccc1)(c1ccccc1)c1ccccc1` | 21/23 | C19H15S | high |
| `Trt` | trityl | `c1ccccc1C(c1ccccc1)(c1ccccc1)[*]` | 20/22 | C19H15 | high |
| `Ts` | tosyl (4-methylbenzenesulfonyl) | `Cc1ccc(S(=O)(=O)[*])cc1` | 11/11 | C7H7O2S | high |
| `TSE` | 2-trimethylsilylethyl | `*CC[Si](C)(C)C` | 7/6 | C5H13Si | high |
| `Tyr` | L-tyrosine | `N[C@@H](Cc1ccc(O)cc1)C(=O)[*]` | 13/13 | C9H10NO2 | high |
| `Val` | L-valine | `CC(C)[C@H](N)C(=O)[*]` | 8/7 | C5H10NO | high |
| `Vi` | vinyl | `C=C[*]` | 3/2 | C2H3 | high |
| `Vinyl` | vinyl | `*C=C` | 3/2 | C2H3 | high |
| `Xan` | 9H-xanthen-9-yl | `*C1c2ccccc2Oc2ccccc21` | 15/17 | C13H9O | high |
| `Xyl` | 2,6-dimethylphenyl | `*c1c(C)cccc1C` | 9/9 | C8H9 | high |
| `Xyl(alpha-D)` | alpha-D-xylopyranosyl, attached at C1 | `*[C@H]1OC[C@@H](O)[C@H](O)[C@H]1O` | 10/10 | C5H9O4 | high |
| `Xyl(beta-D)` | beta-D-xylopyranosyl, attached at C1 | `*[C@@H]1OC[C@@H](O)[C@H](O)[C@H]1O` | 10/10 | C5H9O4 | high |
| `Z` | (phenylmethoxy)carbonyl | `*C(=O)OCc1ccccc1` | 11/11 | C8H7O2 | high |
| `Za` | [(phenylmethoxy)carbonyl]amino | `*NC(=O)OCc1ccccc1` | 12/12 | C8H8NO2 | high |
| `Zae` | 2-[(benzyloxycarbonyl)amino]ethyl | `*CCNC(=O)OCc1ccccc1` | 14/14 | C10H12NO2 | high |
| `ZNO2` | 4-nitrobenzyloxycarbonyl | `[O-][N+](=O)c1ccc(COC(=O)[*])cc1` | 14/14 | C8H6NO4 | high |
| `Zoa` | [[(phenylmethoxy)carbonyl]oxy]acetyl | `*C(=O)COC(=O)OCc1ccccc1` | 15/15 | C10H9O4 | high |
| `Zr(Oi-Pr)3` | Authored metal-centred alkoxide attachment group | `[*][Zr](OC(C)C)(OC(C)C)OC(C)C` | 14/13 | C9H21O3Zr | high |
| `Zr(Ot-Bu)3` | Authored metal-centred alkoxide attachment group | `[*][Zr](OC(C)(C)C)(OC(C)(C)C)OC(C)(C)C` | 17/16 | C12H27O3Zr | high |

<!-- generated by studio/tools/build_functional_groups_doc.py; catalog-count=663; source-count=671 -->
