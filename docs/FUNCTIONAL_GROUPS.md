# 官能团与缩写目录

> 本文件由 `studio/tools/build_functional_groups_doc.py` 从 `studio/nicknames.src.json`
> 与 `studio/nickname_catalog.hpp` 生成，**请勿手工编辑**。修改缩写后依次运行：
>
> `python studio/tools/build_nickname_catalog.py`
> `python studio/tools/build_functional_groups_doc.py`

## 统计

| 项目 | 数量 |
| --- | --- |
| 授权源数据条目（nicknames.src.json） | 369 |
| 编译进编辑器目录（nickname_catalog.hpp） | 361 |
| 未编译条目（独立分子，无连接点） | 8 |

未编译条目（只作为结构参考保留在源数据中，不参与补全）：

`BTC`、`Edc`、`THF`、`dC`、`dT`、`rA`、`rG`、`rU`

每条编译条目都是一段带恰好一个外部连接点的 CDXML 片段：在键末端输入其缩写后，
画布上的原子会被识别为对应官能团，可用“官能团展开”还原为完整结构。

## 自动大小写规则

输入在提交时经过 `chemcompletion::normalize`（原子标签）或
`chemcompletion::chemicalToken`（整段文字），规则按顺序如下：

1. **完全匹配的元素符号原样保留**：`Co`、`Cn`、`N`、`O`、`Fe` 等保持用户键入的大小写，不会被改成缩写或其它大小写。元素符号优先于同名缩写。
2. **已经等于目录标准写法的输入不重写**：`Boc`、`CN`、`i-Bu` 原样保留。
3. **键匹配忽略大小写和连字符**：键由字母数字组成，去掉 `-` 并转小写。
   小写或大小写混写输入匹配到唯一标准写法后替换，例如 `cn` → `CN`、`bpin` → `Bpin`、`boc` → `Boc`、`mgbr` → `MgBr`。
4. **歧义键不猜测**：同一键对应多个仅大小写/连字符不同的写法、且没有经过人工评审时，
   补全不会改写输入（保持用户原文），只在设置页的列表中提供候选。
   当前保留未决的键：`iam`、`nam`、`sam`、`tam`。
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
   完全按元素符号大小写输入时拼写不变，但该标签仍会按缩写目录识别（例如 `Ts` 显示为对甲苯磺酰基）。
   如需把元素本身作为普通元素标签使用，可在“设置 → 基团补齐”中停用对应条目。

## 歧义处理

- `pin` 收录为 **O-连接的频哪醇醚**（pinacolato O-linked）；如需频哪醇硼酸酯请用 `Bpin`。
- `DIBAL` 是试剂简写，片段按 Al 连接的二异丁基铝绘制（不含 Al–H 氢）。
- `Cp` 按 η1 碳连接绘制；与金属的 η5 配位需另行绘制配位键。
- 只收录化学意义明确、文献广泛使用、且能用“一个连接点”表示的结构缩写；
  分子式、溶剂、当量条件（如 THF、NaH、80 °C）不属于本目录。
- 大小写冲突的元素符号（`Ac`、`Np`、`Pr`、`Ts`、`Li`）不做自动大小写改写；
  停用某个条目后，该缩写完全按用户输入保留。

## 全部缩写

| 缩写 | 含义 | SMILES（源数据） | 原子/键 | 分子式 | 置信度 |
| --- | --- | --- | --- | --- | --- |
| `1-Naph` | 1-naphthyl (alpha) | `[*]c1cccc2ccccc12` | 11/12 | C10H7 | high |
| `2-4DCZ` | 2,4-dichlorobenzyloxycarbonyl (2,4-diCl-Z) | `[*]C(=O)OCc1ccc(Cl)cc1Cl` | 13/13 | C8H5Cl2O2 | medium |
| `2-6DCZ` | 2,6-dichlorobenzyloxycarbonyl (2,6-diCl-Z) | `[*]C(=O)OCc1c(Cl)cccc1Cl` | 13/13 | C8H5Cl2O2 | medium |
| `2-Naph` | 2-naphthyl (beta) | `[*]c1ccc2ccccc2c1` | 11/12 | C10H7 | high |
| `2CZ` | 2-chlorobenzyloxycarbonyl (2-Cl-Z) | `[*]C(=O)OCc1ccccc1Cl` | 12/12 | C8H6ClO2 | medium |
| `2OHEt` | 2-hydroxyethyl | `OCC[*]` | 4/3 | C2H5O | high |
| `2OHPh` | 2-hydroxyphenyl | `[*]c1ccccc1O` | 8/8 | C6H5O | high |
| `3Py` | pyridin-3-yl | `[*]c1cccnc1` | 7/7 | C5H4N | high |
| `4CZ` | 4-chlorobenzyloxycarbonyl (4-Cl-Z) | `[*]C(=O)OCc1ccc(Cl)cc1` | 12/12 | C8H6ClO2 | medium |
| `4Py` | pyridin-4-yl | `[*]c1ccncc1` | 7/7 | C5H4N | high |
| `Aad` | L-2-aminoadipic acid residue | `N[C@@H](CCCC(=O)O)C(=O)[*]` | 11/10 | C6H10NO3 | high |
| `Abu` | L-2-aminobutyric acid | `CC[C@H](N)C(=O)[*]` | 7/6 | C4H8NO | high |
| `Abz` | 2-aminobenzoyl (anthraniloyl) | `Nc1ccccc1C(=O)[*]` | 10/10 | C7H6NO | medium |
| `Ac` | acetyl | `CC(=O)[*]` | 4/3 | C2H3O | high |
| `Acm` | acetamidomethyl (Cys protection) | `CC(=O)NC[*]` | 6/5 | C3H6NO | high |
| `AcNH` | acetylamino | `CC(=O)N[*]` | 5/4 | C2H4NO | high |
| `AcO` | acetoxy (attached through O) | `CC(=O)O[*]` | 5/4 | C2H3O2 | high |
| `Ad` | 1-adamantyl | `[*]C12CC3CC(CC(C3)C1)C2` | 11/13 | C10H15 | high |
| `Adoc` | 1-adamantyloxycarbonyl | `[*]C(=O)OC12CC3CC(CC(C3)C1)C2` | 14/16 | C11H15O2 | medium |
| `Ahx` | 6-aminohexanoyl | `NCCCCCC(=O)[*]` | 9/8 | C6H12NO | high |
| `Aib` | 2-aminoisobutyric acid (Aib) | `CC(C)(N)C(=O)[*]` | 7/6 | C4H8NO | high |
| `aIle` | L-allo-isoleucine (2S,3R) | `CC[C@@H](C)[C@H](N)C(=O)[*]` | 9/8 | C6H12NO | medium |
| `Ala` | L-alanine residue | `N[C@@H](C)C(=O)[*]` | 6/5 | C3H6NO | high |
| `All` | allyl | `C=CC[*]` | 4/3 | C3H5 | high |
| `Alloc` | allyloxycarbonyl | `C=CCOC(=O)[*]` | 7/6 | C4H5O2 | high |
| `Allyl` | allyl | `C=CC[*]` | 4/3 | C3H5 | high |
| `Arg` | L-arginine residue | `N[C@@H](CCCNC(=N)N)C(=O)[*]` | 12/11 | C6H13N4O | high |
| `Asn` | L-asparagine | `N[C@@H](CC(N)=O)C(=O)[*]` | 9/8 | C4H7N2O2 | high |
| `Asp` | L-aspartic acid residue | `N[C@@H](CC(=O)O)C(=O)[*]` | 9/8 | C4H6NO3 | high |
| `aThr` | L-allo-threonine (2S,3R) | `C[C@H](O)[C@@H](N)C(=O)[*]` | 8/7 | C4H8NO2 | medium |
| `Aze` | L-azetidine-2-carboxylic acid | `O=C([*])[C@@H]1CCN1` | 7/7 | C4H6NO | high |
| `B(OH)2` | Common functional group; attachment at * | `*B(O)O` | 4/3 | H2BO2 | authored |
| `bAla` | beta-alanine (3-aminopropanoic acid) | `NCCC(=O)[*]` | 6/5 | C3H6NO | high |
| `bArg` | beta3-homoarginine residue | `N[C@@H](CCCNC(=N)N)CC(=O)[*]` | 13/12 | C7H15N4O | medium |
| `bAsn` | beta3-asparagine; C3 stereochemistry left unspecified | `NC(=O)CC(N)CC(=O)[*]` | 10/9 | C5H9N2O2 | medium |
| `bAsp` | beta3-homoaspartic acid residue | `N[C@@H](CC(=O)O)CC(=O)[*]` | 10/9 | C5H8NO3 | medium |
| `bCys` | beta3-cysteine; C3 stereochemistry left unspecified | `NC(CS)CC(=O)[*]` | 8/7 | C4H8NOS | medium |
| `Benzoyl` | benzoyl | `c1ccccc1C(=O)[*]` | 9/9 | C7H5O | high |
| `Benzyl` | benzyl | `[*]Cc1ccccc1` | 8/8 | C7H7 | high |
| `bGln` | beta3-homoglutamine residue | `N[C@@H](CCC(N)=O)CC(=O)[*]` | 11/10 | C6H11N2O2 | medium |
| `bGlu` | beta3-glutamic acid; C3 stereochemistry left unspecified | `OC(=O)CCC(N)CC(=O)[*]` | 11/10 | C6H10NO3 | medium |
| `bHis` | beta3-homohistidine residue | `N[C@@H](Cc1c[nH]cn1)CC(=O)[*]` | 12/12 | C7H10N3O | medium |
| `bIle` | beta3-isoleucine; C3 stereochemistry left unspecified | `CCC(C)C(N)CC(=O)[*]` | 10/9 | C7H14NO | medium |
| `bLeu` | beta3-homoleucine residue | `N[C@@H](CC(C)C)CC(=O)[*]` | 10/9 | C7H14NO | medium |
| `bLys` | beta3-lysine; C3 stereochemistry left unspecified | `NCCCCC(N)CC(=O)[*]` | 11/10 | C7H15N2O | medium |
| `bMet` | beta3-homomethionine residue | `N[C@@H](CCSC)CC(=O)[*]` | 10/9 | C6H12NOS | medium |
| `Bn` | benzyl | `[*]Cc1ccccc1` | 8/8 | C7H7 | high |
| `Boc` | tert-butoxycarbonyl | `CC(C)(C)OC(=O)[*]` | 8/7 | C5H9O2 | high |
| `BOM` | benzyloxymethyl | `c1ccccc1COC[*]` | 10/10 | C8H9O | high |
| `bPhe` | beta3-phenylalanine; C3 stereochemistry left unspecified | `NC(c1ccccc1)CC(=O)[*]` | 12/12 | C9H10NO | medium |
| `Bpin` | pinacolboranyl (4,4,5,5-tetramethyl-1,3,2-dioxaborolan-2-yl, B-bound) | `[*]B1OC(C)(C)C(C)(C)O1` | 10/10 | C6H12BO2 | high |
| `Bpoc` | 2-(biphenyl-4-yl)propan-2-yloxycarbonyl | `[*]C(=O)OC(C)(C)c1ccc(-c2ccccc2)cc1` | 19/20 | C16H15O2 | high |
| `Bs` | 4-bromobenzenesulfonyl (brosyl) | `[*]S(=O)(=O)c1ccc(Br)cc1` | 11/11 | C6H4BrO2S | high |
| `bSer` | beta3-serine; C3 stereochemistry left unspecified | `OCC(N)CC(=O)[*]` | 8/7 | C4H8NO2 | medium |
| `Bt` | benzotriazol-1-yl | `[*]n1nnc2ccccc12` | 10/11 | C6H4N3 | high |
| `bTrp` | beta3-tryptophan; C3 stereochemistry left unspecified | `NC(Cc1c[nH]c2ccccc12)CC(=O)[*]` | 16/17 | C12H13N2O | medium |
| `bTyr` | beta3-homotyrosine residue | `N[C@@H](Cc1ccc(O)cc1)CC(=O)[*]` | 14/14 | C10H12NO2 | medium |
| `Bu` | n-butyl | `CCCC[*]` | 5/4 | C4H9 | high |
| `bVal` | beta3-valine; C3 stereochemistry left unspecified | `CC(C)C(N)CC(=O)[*]` | 9/8 | C6H12NO | medium |
| `Bz` | benzoyl | `O=C([*])c1ccccc1` | 9/9 | C7H5O | high |
| `Bzl` | benzyl | `[*]Cc1ccccc1` | 8/8 | C7H7 | high |
| `BzOM` | benzoyloxymethyl | `c1ccccc1C(=O)OC[*]` | 11/11 | C8H7O2 | medium |
| `C(=O)Me` | acetyl, written as C(=O)Me | `CC(=O)[*]` | 4/3 | C2H3O | high |
| `C(=O)NH2` | carbamoyl | `NC(=O)[*]` | 4/3 | CH2NO | high |
| `C(O)Me` | 1-hydroxyethyl (-CH(OH)CH3) | `CC(O)[*]` | 4/3 | C2H5O | medium |
| `C(O)NH2` | carbamoyl | `NC(=O)[*]` | 4/3 | CH2NO | high |
| `c-C3H5` | cyclopropyl | `[*]C1CC1` | 4/4 | C3H5 | high |
| `c-C4H7` | cyclobutyl | `C1CCC1[*]` | 5/5 | C4H7 | high |
| `c-C5H9` | cyclopentyl | `[*]C1CCCC1` | 6/6 | C5H9 | high |
| `c-C6H11` | cyclohexyl | `C1CCCCC1[*]` | 7/7 | C6H11 | high |
| `c-C7H13` | cycloheptyl | `[*]C1CCCCCC1` | 8/8 | C7H13 | high |
| `c-C8H15` | cyclooctyl | `C1CCCCCCC1[*]` | 9/9 | C8H15 | high |
| `c-Hx` | cyclohexyl | `[*]C1CCCCC1` | 7/7 | C6H11 | high |
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
| `Cap` | caproyl (n-hexanoyl) | `CCCCCC(=O)[*]` | 8/7 | C6H11O | medium |
| `Cbz` | benzyloxycarbonyl | `[*]C(=O)OCc1ccccc1` | 11/11 | C8H7O2 | high |
| `CCl3` | trichloromethyl | `[*]C(Cl)(Cl)Cl` | 5/4 | CCl3 | high |
| `CF3` | Common functional group; attachment at * | `*C(F)(F)F` | 5/4 | CF3 | authored |
| `CH2Cl` | chloromethyl | `[*]CCl` | 3/2 | CH2Cl | high |
| `CH2F` | Common functional group; attachment at * | `*CF` | 3/2 | CH2F | authored |
| `CH2NH2` | aminomethyl | `NCC[*]` | 4/3 | C2H6N | high |
| `CH2OH` | hydroxymethyl | `OC[*]` | 3/2 | CH3O | high |
| `Cha` | L-cyclohexylalanine | `N[C@@H](CC1CCCCC1)C(=O)[*]` | 12/12 | C9H16NO | high |
| `CHF2` | Common functional group; attachment at * | `*C(F)F` | 4/3 | CHF2 | authored |
| `CHO` | formyl | `O=C[*]` | 3/2 | CHO | high |
| `cHx` | cyclohexyl | `C1CCCCC1[*]` | 7/7 | C6H11 | high |
| `Cit` | L-citrulline residue | `N[C@@H](CCCNC(N)=O)C(=O)[*]` | 12/11 | C6H12N3O2 | high |
| `CN` | cyano | `N#C[*]` | 3/2 | CN | high |
| `CO2Et` | ethoxycarbonyl | `CCOC(=O)[*]` | 6/5 | C3H5O2 | high |
| `CO2H` | carboxy | `OC(=O)[*]` | 4/3 | CHO2 | high |
| `CO2Me` | methoxycarbonyl | `COC(=O)[*]` | 5/4 | C2H3O2 | high |
| `COBr` | bromoformyl (acyl bromide) | `BrC(=O)[*]` | 4/3 | CBrO | high |
| `COCF3` | Common functional group; attachment at * | `*C(=O)C(F)(F)F` | 7/6 | C2F3O | authored |
| `COCH3` | Common functional group; attachment at * | `*C(=O)C` | 4/3 | C2H3O | authored |
| `COCl` | chlorocarbonyl | `O=C(Cl)[*]` | 4/3 | CClO | high |
| `CON3` | acyl azide (charge separation required by the azide) | `[N-]=[N+]=NC(=O)[*]` | 6/5 | CN3O | high |
| `CONH2` | carbamoyl | `NC(=O)[*]` | 4/3 | CH2NO | high |
| `CONHMe` | N-methylcarbamoyl | `CNC(=O)[*]` | 5/4 | C2H4NO | high |
| `CONMe2` | N,N-dimethylcarbamoyl | `CN(C)C(=O)[*]` | 6/5 | C3H6NO | high |
| `COOCH3` | methoxycarbonyl | `COC(=O)[*]` | 5/4 | C2H3O2 | high |
| `COOEt` | ethoxycarbonyl | `CCOC(=O)[*]` | 6/5 | C3H5O2 | high |
| `COOH` | carboxy | `OC(=O)[*]` | 4/3 | CHO2 | high |
| `COOMe` | methoxycarbonyl | `COC(=O)[*]` | 5/4 | C2H3O2 | high |
| `COOtBu` | Common functional group; attachment at * | `*C(=O)OC(C)(C)C` | 8/7 | C5H9O2 | authored |
| `Cp` | cyclopentadienyl (eta-1 C-bound; coordination to metals is shown separately) | `[*]C1C=CC=C1` | 6/6 | C5H5 | medium |
| `Cpm` | cyclopropylmethyl | `[*]CC1CC1` | 5/5 | C4H7 | medium |
| `Cy` | cyclohexyl | `C1CCCCC1[*]` | 7/7 | C6H11 | high |
| `cyclobutyl` | cyclobutyl | `C1CCC1[*]` | 5/5 | C4H7 | high |
| `cycloheptyl` | cycloheptyl | `[*]C1CCCCCC1` | 8/8 | C7H13 | high |
| `cyclooctyl` | cyclooctyl | `C1CCCCCCC1[*]` | 9/9 | C8H15 | high |
| `cyclopentyl` | cyclopentyl | `[*]C1CCCC1` | 6/6 | C5H9 | high |
| `cyclopropyl` | cyclopropyl | `C1CC1[*]` | 4/4 | C3H5 | high |
| `Cys` | L-cysteine | `N[C@@H](CS)C(=O)[*]` | 7/6 | C3H6NOS | high |
| `Dab` | L-2,4-diaminobutyric acid residue | `N[C@@H](CCN)C(=O)[*]` | 8/7 | C4H9N2O | high |
| `Dap` | L-2,3-diaminopropionic acid residue | `N[C@@H](CN)C(=O)[*]` | 7/6 | C3H7N2O | high |
| `Ddz` | 2-(3,5-dimethoxyphenyl)propan-2-yloxycarbonyl | `[*]C(=O)OC(C)(C)c1cc(OC)cc(OC)c1` | 17/17 | C12H15O4 | high |
| `DEAE` | 2-(diethylamino)ethyl | `CCN(CC)CC[*]` | 8/7 | C6H14N | high |
| `Dec` | n-decyl | `CCCCCCCCCC[*]` | 11/10 | C10H21 | high |
| `DEIPS` | diethylisopropylsilyl | `CC[Si](CC)(C(C)C)[*]` | 9/8 | C7H17Si | high |
| `Dha` | dehydroalanine | `C=C(N)C(=O)[*]` | 6/5 | C3H4NO | high |
| `DIBAL` | diisobutylaluminium (DIBAL-H without its hydride; Al-bound reagent shorthand) | `[*][Al](CC(C)C)CC(C)C` | 10/9 | C8H18Al | medium |
| `DMB` | 2,4-dimethoxybenzyl | `COc1ccc(C[*])c(OC)c1` | 12/12 | C9H11O2 | high |
| `DMIPS` | dimethylisopropylsilyl | `C[Si](C)(C(C)C)[*]` | 7/6 | C5H13Si | high |
| `DMPS` | dimethylphenylsilyl | `C[Si](C)(c1ccccc1)[*]` | 10/10 | C8H11Si | medium |
| `DMTr` | 4,4'-dimethoxytrityl | `[*]C(c1ccccc1)(c1ccc(OC)cc1)c1ccc(OC)cc1` | 24/26 | C21H19O2 | high |
| `DNP` | 2,4-dinitrophenyl | `[*]c1ccc([N+](=O)[O-])cc1[N+](=O)[O-]` | 13/13 | C6H3N2O4 | high |
| `DNS` | 5-(dimethylamino)naphthalene-1-sulfonyl (dansyl) | `[*]S(=O)(=O)c1cccc2c(N(C)C)cccc12` | 17/18 | C12H12NO2S | high |
| `DPIPS` | diisopropylsilyl | `[*][SiH](C(C)C)C(C)C` | 8/7 | C6H15Si | medium |
| `Dpm` | diphenylmethyl (benzhydryl) | `c1ccccc1C(c1ccccc1)[*]` | 14/15 | C13H11 | high |
| `Dpr` | L-2,3-diaminopropionic acid | `N[C@@H](CN)C(=O)[*]` | 7/6 | C3H7N2O | high |
| `DTBMS` | di-tert-butylmethylsilyl | `[*][Si](C)(C(C)(C)C)C(C)(C)C` | 11/10 | C9H21Si | medium |
| `DTBS` | di-tert-butylsilyl (Si-H retained) | `[*][SiH](C(C)(C)C)C(C)(C)C` | 10/9 | C8H19Si | medium |
| `Et` | ethyl | `CC[*]` | 3/2 | C2H5 | high |
| `Fmoc` | 9-fluorenylmethoxycarbonyl | `O=C(OCC1c2ccccc2c2ccccc21)[*]` | 18/20 | C15H11O2 | high |
| `For` | formyl | `O=C[*]` | 3/2 | CHO | high |
| `Gla` | gamma-carboxy-L-glutamic acid (C-gamma stereochemistry unspecified) | `O=C([*])[C@@H](N)C(C(=O)O)CC(=O)O` | 13/12 | C6H8NO5 | medium |
| `Gln` | L-glutamine | `N[C@@H](CCC(N)=O)C(=O)[*]` | 10/9 | C5H9N2O2 | high |
| `Glp` | L-pyroglutamyl (5-oxo-L-prolyl) residue | `[*]C(=O)[C@@H]1CCC(=O)N1` | 9/9 | C5H6NO2 | high |
| `Glu` | L-glutamic acid residue | `N[C@@H](CCC(=O)O)C(=O)[*]` | 10/9 | C5H8NO3 | high |
| `Gly` | glycine residue | `NCC(=O)[*]` | 5/4 | C2H4NO | high |
| `Har` | L-homoarginine | `N[C@@H](CCCCNC(N)=N)C(=O)[*]` | 13/12 | C7H15N4O | medium |
| `Hcy` | L-homocysteine residue | `N[C@@H](CCS)C(=O)[*]` | 8/7 | C4H8NOS | high |
| `Hex` | n-hexyl | `CCCCCC[*]` | 7/6 | C6H13 | high |
| `Hippuryl` | hippuryl (N-benzoylglycyl) | `O=C(NCC(=O)[*])c1ccccc1` | 13/13 | C9H8NO2 | high |
| `His` | L-histidine | `N[C@@H](Cc1c[nH]cn1)C(=O)[*]` | 11/11 | C6H8N3O | high |
| `Hiv` | 2-hydroxyisovaleryl | `CC(C)C(O)C(=O)[*]` | 8/7 | C5H9O2 | medium |
| `Hph` | L-homophenylalanine | `N[C@@H](CCCc1ccccc1)C(=O)[*]` | 14/14 | C11H14NO | medium |
| `Hse` | L-homoserine residue | `N[C@@H](CCO)C(=O)[*]` | 8/7 | C4H8NO2 | high |
| `Hyl` | L-5-hydroxylysine (2S,5R) residue | `N[C@@H](CC[C@@H](O)CN)C(=O)[*]` | 11/10 | C6H13N2O2 | medium |
| `Hyp` | trans-4-hydroxy-L-proline | `O=C([*])[C@@H]1C[C@@H](O)CN1` | 9/9 | C5H8NO2 | medium |
| `i-Am` | isopentyl (3-methylbutan-1-yl) | `CCC(C)C[*]` | 6/5 | C5H11 | high |
| `i-Bu` | isobutyl | `CC(C)C[*]` | 5/4 | C4H9 | high |
| `i-BuO` | isobutoxy | `[*]OCC(C)C` | 6/5 | C4H9O | high |
| `i-C3H7` | isopropyl | `CC(C)[*]` | 4/3 | C3H7 | high |
| `i-C4H9` | isobutyl | `CC(C)C[*]` | 5/4 | C4H9 | high |
| `i-C5H11` | isopentyl | `CC(C)CC[*]` | 6/5 | C5H11 | high |
| `i-Pr` | isopropyl | `C(C)C[*]` | 4/3 | C3H7 | high |
| `iAm` | isopentyl (3-methylbutyl) | `CC(C)CC[*]` | 6/5 | C5H11 | high |
| `iBu` | isobutyl | `CC(C)C[*]` | 5/4 | C4H9 | high |
| `Ile` | L-isoleucine | `CC[C@H](C)[C@H](N)C(=O)[*]` | 9/8 | C6H12NO | high |
| `Im` | imidazol-1-yl | `[*]n1ccnc1` | 6/6 | C3H3N2 | medium |
| `iPr` | isopropyl | `CC(C)[*]` | 4/3 | C3H7 | high |
| `Iva` | isovaleryl (3-methylbutanoyl) | `CC(C)CC(=O)[*]` | 7/6 | C5H9O | high |
| `Lac` | lactyl (2-hydroxypropanoyl) | `CC(O)C(=O)[*]` | 6/5 | C3H5O2 | medium |
| `Leu` | L-leucine residue | `N[C@@H](CC(C)C)C(=O)[*]` | 9/8 | C6H12NO | high |
| `Li` | lithium (organolithium, Li-bound) | `[*][Li]` | 2/1 | Li | high |
| `Lys` | L-lysine | `N[C@@H](CCCCN)C(=O)[*]` | 10/9 | C6H13N2O | high |
| `m-Tolyl` | 3-methylphenyl | `[*]c1cccc(C)c1` | 8/8 | C7H7 | high |
| `Me` | methyl | `C[*]` | 2/1 | CH3 | high |
| `MeAla` | N-methyl-L-alanine residue | `CN[C@@H](C)C(=O)[*]` | 7/6 | C4H8NO | medium |
| `MEM` | 2-methoxyethoxymethyl | `COCCOC[*]` | 7/6 | C4H9O2 | high |
| `Mes` | mesityl (2,4,6-trimethylphenyl) | `[*]c1c(C)cc(C)cc1C` | 10/10 | C9H11 | high |
| `Met` | L-methionine | `CSCC[C@H](N)C(=O)[*]` | 9/8 | C5H10NOS | high |
| `MgBr` | magnesium bromide (Grignard, Mg-bound) | `[*][Mg]Br` | 3/2 | BrMg | high |
| `MgCl` | magnesium chloride (Grignard, Mg-bound) | `[*][Mg]Cl` | 3/2 | ClMg | high |
| `MgI` | magnesium iodide (Grignard, Mg-bound) | `[*][Mg]I` | 3/2 | IMg | high |
| `Mmt` | 4-methoxytrityl (monomethoxytrityl) | `[*]C(c1ccccc1)(c1ccccc1)c1ccc(OC)cc1` | 22/24 | C20H17O | high |
| `MMTr` | 4-methoxytrityl | `COc1ccc(C(c2ccccc2)(c2ccccc2)[*])cc1` | 22/24 | C20H17O | medium |
| `MOM` | methoxymethyl | `COC[*]` | 4/3 | C2H5O | high |
| `Moz` | 4-methoxybenzyloxycarbonyl | `COc1ccc(COC(=O)[*])cc1` | 13/13 | C9H9O3 | medium |
| `Mpa` | 3-mercaptopropionyl | `SCC(=O)[*]` | 5/4 | C2H3OS | high |
| `MPM` | 4-methoxybenzyl | `COc1ccc(C[*])cc1` | 10/10 | C8H9O | high |
| `Ms` | methanesulfonyl | `CS(=O)(=O)[*]` | 5/4 | CH3O2S | high |
| `Msc` | 2-(methylsulfonyl)ethyloxycarbonyl | `CS(=O)(=O)CCOC(=O)[*]` | 10/9 | C4H7O4S | medium |
| `MTM` | methylthiomethyl | `CSC[*]` | 4/3 | C2H5S | high |
| `Mts` | mesitylenesulfonyl (2,4,6-trimethylbenzenesulfonyl) | `[*]S(=O)(=O)c1c(C)cc(C)cc1C` | 13/13 | C9H11O2S | high |
| `Mtt` | 4-methyltrityl | `Cc1ccc(C(c2ccccc2)(c2ccccc2)[*])cc1` | 21/23 | C20H17 | medium |
| `Myr` | myristoyl (n-tetradecanoyl) | `CCCCCCCCCCCCCC(=O)[*]` | 16/15 | C14H27O | high |
| `n-Am` | n-pentyl | `CCCCC[*]` | 6/5 | C5H11 | high |
| `n-Bu` | n-butyl | `CCCC[*]` | 5/4 | C4H9 | high |
| `n-C3H7` | n-propyl | `CCC[*]` | 4/3 | C3H7 | high |
| `n-C4H9` | n-butyl | `CCCC[*]` | 5/4 | C4H9 | high |
| `n-C5H11` | n-pentyl | `CCCCC[*]` | 6/5 | C5H11 | high |
| `n-Pr` | n-propyl | `CCC[*]` | 4/3 | C3H7 | high |
| `N3` | azide | `[N-]=[N+]=N[*]` | 4/3 | N3 | high |
| `Nal` | L-3-(1-naphthyl)alanine residue | `N[C@@H](Cc1cccc2ccccc12)C(=O)[*]` | 16/17 | C13H12NO | high |
| `nAm` | n-pentyl | `CCCCC[*]` | 6/5 | C5H11 | high |
| `nBu` | n-butyl | `CCCC[*]` | 5/4 | C4H9 | high |
| `neo-Am` | neopentyl (2,2-dimethylpropyl) | `CC(C)(C)C[*]` | 6/5 | C5H11 | medium |
| `neo-C5H11` | neopentyl | `CC(C)(C)C[*]` | 6/5 | C5H11 | high |
| `NEt2` | diethylamino | `CCN(CC)[*]` | 6/5 | C4H10N | high |
| `NH2` | Common functional group; attachment at * | `*N` | 2/1 | H2N | authored |
| `NHAc` | acetamido | `CC(=O)N[*]` | 5/4 | C2H4NO | high |
| `NHBoc` | tert-butoxycarbonylamino | `[*]NC(=O)OC(C)(C)C` | 9/8 | C5H10NO2 | high |
| `NHCbz` | benzyloxycarbonylamino | `[*]NC(=O)OCc1ccccc1` | 12/12 | C8H8NO2 | high |
| `NHEt` | Common functional group; attachment at * | `*NCC` | 4/3 | C2H6N | authored |
| `NHFmoc` | Fmoc-amino (NHFmoc) | `O=C(N[*])OCC1c2ccccc2c2ccccc21` | 19/21 | C15H12NO2 | high |
| `NHMe` | Common functional group; attachment at * | `*NC` | 3/2 | CH4N | authored |
| `NHNH2` | hydrazino | `NN[*]` | 3/2 | H3N2 | high |
| `NHTs` | Common functional group; attachment at * | `*NS(=O)(=O)c1ccc(C)cc1` | 12/12 | C7H8NO2S | authored |
| `Nle` | L-norleucine residue | `N[C@@H](CCCC)C(=O)[*]` | 9/8 | C6H12NO | high |
| `NMe2` | dimethylamino | `CN(C)[*]` | 4/3 | C2H6N | high |
| `NO` | Common functional group; attachment at * | `*N=O` | 3/2 | NO | authored |
| `NO2` | Common functional group; attachment at * | `*[N+](=O)[O-]` | 4/3 | NO2 | authored |
| `Np` | 1-naphthyl | `[*]c1cccc2ccccc12` | 11/12 | C10H7 | medium |
| `nPr` | n-propyl | `CCC[*]` | 4/3 | C3H7 | high |
| `Nps` | 2-nitrophenylsulfenyl | `[O-][N+](=O)c1ccccc1S[*]` | 11/11 | C6H4NO2S | medium |
| `Ns` | 2-nitrobenzenesulfonyl (nosyl) | `[*]S(=O)(=O)c1ccccc1[N+](=O)[O-]` | 13/13 | C6H4NO4S | high |
| `Nva` | L-norvaline | `CCC[C@H](N)C(=O)[*]` | 8/7 | C5H10NO | high |
| `o-Tolyl` | 2-methylphenyl | `[*]c1ccccc1C` | 8/8 | C7H7 | high |
| `OAc` | acetoxy | `CC(=O)O[*]` | 5/4 | C2H3O2 | high |
| `OBn` | benzyloxy | `c1ccccc1CO[*]` | 9/9 | C7H7O | high |
| `OBz` | benzoyloxy | `O=C(O[*])c1ccccc1` | 10/10 | C7H5O2 | high |
| `OCF3` | Common functional group; attachment at * | `*OC(F)(F)F` | 6/5 | CF3O | authored |
| `OCOMe` | Common functional group; attachment at * | `*OC(=O)C` | 5/4 | C2H3O2 | authored |
| `Oct` | n-octyl | `CCCCCCCC[*]` | 9/8 | C8H17 | high |
| `OEt` | ethoxy | `CCO[*]` | 4/3 | C2H5O | high |
| `OH` | Common functional group; attachment at * | `*O` | 2/1 | HO | authored |
| `Oic` | L-octahydroindole-2-carboxyl residue; ring-fusion stereocentres unspecified | `[*]C(=O)[C@@H]1CC2CCCCC2N1` | 12/13 | C9H14NO | medium |
| `OiPr` | Common functional group; attachment at * | `*OC(C)C` | 5/4 | C3H7O | authored |
| `Ole` | oleoyl, Z (cis) double bond | `CCCCCCCC/C=C\CCCCCCCC(=O)[*]` | 20/19 | C18H33O | medium |
| `OMe` | methoxy | `CO[*]` | 3/2 | CH3O | high |
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
| `Pbf` | 2,2,4,6,7-pentamethyldihydrobenzofuran-5-sulfonyl | `CC1(C)Cc2c(C)c(S(=O)(=O)[*])c(C)c(C)c2O1` | 18/19 | C13H17O3S | medium |
| `Pcp` | pentachlorophenyl | `[*]c1c(Cl)c(Cl)c(Cl)c(Cl)c1Cl` | 12/12 | C6Cl5 | medium |
| `Pen` | L-penicillamine residue | `N[C@@H](C(C)(C)S)C(=O)[*]` | 9/8 | C5H10NOS | high |
| `Pfp` | pentafluorophenyl | `[*]c1c(F)c(F)c(F)c(F)c1F` | 12/12 | C6F5 | high |
| `pGlu` | L-pyroglutamic acid (5-oxoproline) | `O=C([*])[C@@H]1CCC(=O)N1` | 9/9 | C5H6NO2 | high |
| `Ph` | phenyl | `[*]c1ccccc1` | 7/7 | C6H5 | high |
| `Phe` | L-phenylalanine | `N[C@@H](Cc1ccccc1)C(=O)[*]` | 12/12 | C9H10NO | high |
| `Phenyl` | phenyl | `[*]c1ccccc1` | 7/7 | C6H5 | high |
| `Phg` | L-phenylglycine | `N[C@@H](c1ccccc1)C(=O)[*]` | 11/11 | C8H8NO | high |
| `Pht` | phthaloyl | `[*]N1C(=O)c2ccccc2C1=O` | 12/13 | C8H4NO2 | high |
| `pin` | pinacol O-ether (pinacolato O-linked); for the boronate use Bpin | `[*]OC(C)(C)C(C)(C)O` | 9/8 | C6H13O2 | medium |
| `Pip` | piperidin-1-yl | `[*]N1CCCCC1` | 7/7 | C5H10N | medium |
| `Piv` | pivaloyl | `CC(C)(C)C(=O)[*]` | 7/6 | C5H9O | high |
| `PMB` | 4-methoxybenzyl | `COc1ccc(C[*])cc1` | 10/10 | C8H9O | high |
| `Pmc` | 2,2,5,7,8-pentamethylchroman-6-sulfonyl | `CC1(C)CCc2c(C)c(S(=O)(=O)[*])c(C)c(C)c2O1` | 19/20 | C14H19O3S | medium |
| `PNB` | 4-nitrobenzyl | `[O-][N+](=O)c1ccc(C[*])cc1` | 11/11 | C7H6NO2 | high |
| `PO3H2` | phosphono | `[*]P(=O)(O)O` | 5/4 | H2O3P | high |
| `PPh2` | diphenylphosphino | `[*]P(c1ccccc1)c1ccccc1` | 14/15 | C12H10P | high |
| `Pr` | n-propyl | `CCC[*]` | 4/3 | C3H7 | high |
| `Pro` | L-proline | `O=C([*])[C@@H]1CCCN1` | 8/8 | C5H8NO | high |
| `pS` | O-phospho-L-serine | `N[C@@H](COP(=O)(O)O)C(=O)[*]` | 11/10 | C3H7NO5P | medium |
| `pSer` | O-phospho-L-serine residue | `N[C@@H](COP(=O)(O)O)C(=O)[*]` | 11/10 | C3H7NO5P | high |
| `pThr` | O-phospho-L-threonine | `C[C@@H](OP(=O)(O)O)[C@H](N)C(=O)[*]` | 12/11 | C4H9NO5P | medium |
| `pTyr` | O-phospho-L-tyrosine residue | `N[C@@H](Cc1ccc(OP(=O)(O)O)cc1)C(=O)[*]` | 17/17 | C9H11NO5P | high |
| `Pv` | pivaloyl | `CC(C)(C)C(=O)[*]` | 7/6 | C5H9O | high |
| `s-Am` | sec-pentyl (pentan-2-yl) | `C(C)CCC[*]` | 6/5 | C5H11 | medium |
| `s-Bu` | sec-butyl | `[*]C(C)CC` | 5/4 | C4H9 | high |
| `s-Butyl` | sec-butyl | `C(C)CC[*]` | 5/4 | C4H9 | high |
| `s-C4H9` | sec-butyl | `[*]C(C)CC` | 5/4 | C4H9 | high |
| `s-C5H11` | sec-pentyl (pentan-2-yl) | `C(C)CCC[*]` | 6/5 | C5H11 | medium |
| `sAm` | sec-amyl (pentan-2-yl) | `[*]C(C)CCC` | 6/5 | C5H11 | medium |
| `Sar` | sarcosine (N-methylglycine) | `CNCC(=O)[*]` | 6/5 | C3H6NO | high |
| `sBu` | sec-butyl | `C(C)CC[*]` | 5/4 | C4H9 | high |
| `SCF3` | Common functional group; attachment at * | `*SC(F)(F)F` | 6/5 | CF3S | authored |
| `Sec` | L-selenocysteine residue | `N[C@@H](C[SeH])C(=O)[*]` | 7/6 | C3H6NOSe | medium |
| `SEM` | 2-(trimethylsilyl)ethoxymethyl | `C[Si](C)(C)CCOC[*]` | 9/8 | C6H15OSi | high |
| `SePh` | phenylselenyl | `[*][Se]c1ccccc1` | 8/8 | C6H5Se | high |
| `Ser` | L-serine | `N[C@@H](CO)C(=O)[*]` | 7/6 | C3H6NO2 | high |
| `SES` | 2-(trimethylsilyl)ethanesulfonyl | `C[Si](C)(C)CCS(=O)(=O)[*]` | 10/9 | C5H13O2SSi | high |
| `SEt` | ethylthio | `CCS[*]` | 4/3 | C2H5S | high |
| `SH` | Common functional group; attachment at * | `*S` | 2/1 | HS | authored |
| `SMe` | methylthio | `CS[*]` | 3/2 | CH3S | high |
| `SnBu3` | tributylstannyl | `[*][Sn](CCCC)(CCCC)CCCC` | 14/13 | C12H27Sn | high |
| `SO2CF3` | Common functional group; attachment at * | `*S(=O)(=O)C(F)(F)F` | 8/7 | CF3O2S | authored |
| `SO2Me` | methylsulfonyl | `CS(=O)(=O)[*]` | 5/4 | CH3O2S | high |
| `SO2NH2` | sulfamoyl | `NS(=O)(=O)[*]` | 5/4 | H2NO2S | high |
| `SO2Ph` | Common functional group; attachment at * | `*S(=O)(=O)c1ccccc1` | 10/10 | C6H5O2S | authored |
| `SO3H` | sulfo | `[*]S(=O)(=O)O` | 5/4 | HO3S | high |
| `SOCH3` | Common functional group; attachment at * | `*S(=O)C` | 4/3 | CH3OS | authored |
| `SOPh` | Common functional group; attachment at * | `*S(=O)c1ccccc1` | 9/9 | C6H5OS | authored |
| `SPh` | phenylthio | `[*]Sc1ccccc1` | 8/8 | C6H5S | high |
| `SPy` | 2-pyridylthio | `[*]Sc1ccccn1` | 8/8 | C5H4NS | medium |
| `Sta` | statine, (3S,4S)-4-amino-3-hydroxy-6-methylheptanoic acid | `CC(C)C[C@H](N)[C@@H](O)CC(=O)[*]` | 12/11 | C8H16NO2 | medium |
| `Su` | succinimidyl (N-hydroxysuccinimidyl) | `[*]N1C(=O)CCC1=O` | 8/8 | C4H4NO2 | high |
| `t-Am` | tert-amyl | `[*]C(C)(C)CC` | 6/5 | C5H11 | high |
| `t-BOC` | tert-butoxycarbonyl | `CC(C)(C)OC(=O)[*]` | 8/7 | C5H9O2 | high |
| `t-Bu` | tert-butyl | `CC(C)(C)[*]` | 5/4 | C4H9 | high |
| `t-Butyl` | tert-butyl | `C(C)(C)C[*]` | 5/4 | C4H9 | high |
| `t-C4H9` | tert-butyl | `CC(C)(C)[*]` | 5/4 | C4H9 | high |
| `t-C5H11` | tert-pentyl (2-methylbutan-2-yl) | `C(C)(C)CC[*]` | 6/5 | C5H11 | high |
| `tAm` | tert-amyl (2-methylbutan-2-yl) | `[*]C(C)(C)CC` | 6/5 | C5H11 | high |
| `TBDMS` | tert-butyldimethylsilyl | `[*][Si](C)(C)C(C)(C)C` | 8/7 | C6H15Si | high |
| `TBDPS` | tert-butyldiphenylsilyl | `CC(C)(C)[Si](c1ccccc1)(c1ccccc1)[*]` | 18/19 | C16H19Si | high |
| `TBS` | tert-butyldimethylsilyl | `CC(C)(C)[Si](C)(C)[*]` | 8/7 | C6H15Si | high |
| `tBu` | tert-butyl | `C(C)(C)C[*]` | 5/4 | C4H9 | high |
| `Tce` | 2,2,2-trichloroethyl | `ClC(Cl)(Cl)C[*]` | 6/5 | C2H2Cl3 | medium |
| `Tcp` | 2,4,5-trichlorophenyl | `[*]c1c(Cl)cc(Cl)c(Cl)c1` | 10/10 | C6H2Cl3 | medium |
| `TDS` | thexyldimethylsilyl | `C[Si](C)(C(C)(C)C(C)C)[*]` | 10/9 | C8H19Si | high |
| `Teoc` | 2-(trimethylsilyl)ethoxycarbonyl | `C[Si](C)(C)CCOC(=O)[*]` | 10/9 | C6H13O2Si | high |
| `TES` | triethylsilyl | `[*][Si](CC)(CC)CC` | 8/7 | C6H15Si | high |
| `Tf` | trifluoromethanesulfonyl | `FC(F)(F)S(=O)(=O)[*]` | 8/7 | CF3O2S | high |
| `TFA` | trifluoroacetyl | `FC(F)(F)C(=O)[*]` | 7/6 | C2F3O | high |
| `Tfe` | 2,2,2-trifluoroethyl | `FC(F)(F)C[*]` | 6/5 | C2H2F3 | high |
| `Thexyl` | thexyl (2,3-dimethylbutan-2-yl) | `CC(C)C(C)(C)[*]` | 7/6 | C6H13 | high |
| `THP` | tetrahydropyran-2-yl | `[*]C1CCCCO1` | 7/7 | C5H9O | high |
| `Thr` | L-threonine | `C[C@@H](O)[C@H](N)C(=O)[*]` | 8/7 | C4H8NO2 | high |
| `Tic` | L-1,2,3,4-tetrahydroisoquinoline-3-carboxylic acid | `O=C([*])[C@@H]1Cc2ccccc2CN1` | 13/14 | C10H10NO | medium |
| `TIPS` | triisopropylsilyl | `CC(C)[Si](C(C)C)(C(C)C)[*]` | 11/10 | C9H21Si | high |
| `Tle` | L-tert-leucine residue | `N[C@@H](C(C)(C)C)C(=O)[*]` | 9/8 | C6H12NO | high |
| `Tmob` | 2,4,6-trimethoxybenzyl | `COc1cc(OC)c(C[*])c(OC)c1` | 14/14 | C10H13O3 | medium |
| `TMS` | trimethylsilyl | `C[Si](C)(C)[*]` | 5/4 | C3H9Si | high |
| `TNP` | 2,4,6-trinitrophenyl (picryl) | `[O-][N+](=O)c1cc([N+](=O)[O-])cc([N+](=O)[O-])c1[*]` | 16/16 | C6H2N3O6 | medium |
| `Tos` | p-toluenesulfonyl (tosyl) | `Cc1ccc(S(=O)(=O)[*])cc1` | 11/11 | C7H7O2S | high |
| `Tr` | trityl (triphenylmethyl) | `[*]C(c1ccccc1)(c1ccccc1)c1ccccc1` | 20/22 | C19H15 | high |
| `trans-Cinnamyl` | (E)-cinnamyl | `[*]C/C=C/c1ccccc1` | 10/10 | C9H9 | high |
| `Trit` | trityl | `c1ccccc1C(c1ccccc1)(c1ccccc1)[*]` | 20/22 | C19H15 | high |
| `Troc` | 2,2,2-trichloroethoxycarbonyl | `ClC(Cl)(Cl)CCOC(=O)[*]` | 10/9 | C4H4Cl3O2 | high |
| `Trp` | L-tryptophan | `N[C@@H](Cc1c[nH]c2ccccc12)C(=O)[*]` | 15/16 | C11H11N2O | high |
| `Trt` | trityl | `c1ccccc1C(c1ccccc1)(c1ccccc1)[*]` | 20/22 | C19H15 | high |
| `Ts` | tosyl (4-methylbenzenesulfonyl) | `Cc1ccc(S(=O)(=O)[*])cc1` | 11/11 | C7H7O2S | high |
| `Tyr` | L-tyrosine | `N[C@@H](Cc1ccc(O)cc1)C(=O)[*]` | 13/13 | C9H10NO2 | high |
| `Val` | L-valine | `CC(C)[C@H](N)C(=O)[*]` | 8/7 | C5H10NO | high |
| `Vi` | vinyl | `C=C[*]` | 3/2 | C2H3 | high |
| `ZNO2` | 4-nitrobenzyloxycarbonyl | `[O-][N+](=O)c1ccc(COC(=O)[*])cc1` | 14/14 | C8H6NO4 | high |

<!-- generated by studio/tools/build_functional_groups_doc.py; catalog-count=361; source-count=369 -->
