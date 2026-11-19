from pathlib import Path
import json,hashlib,shutil
r=Path(__file__).resolve().parents[1];out=r/'out/Studio';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
assert Path(m['exe']).name=='MoleculeStudio-Progress-20261117-r29.exe' and sha(m['exe'])==m['exe_sha256']
for row in m['engine_sources'].values():assert sha(row['path'])==row['sha256'],row['path']
for name,value in m['studio_sources'].items():assert sha(r/'studio'/name)==value,name
for name,value in m['backend_sources'].items():assert sha(r/'studio'/name)==value,name
feature=r/'work/test-r29-calculator';regression=r/'work/test-r29-current'
assert not (feature/'failure.txt').exists() and not (feature/'calculator-ui-failure.txt').exists() and not (regression/'failure.txt').exists()
numeric=json.loads((feature/'calculator-math-checks.json').read_text());ui=json.loads((feature/'calculator-ui-checks.json').read_text())
assert numeric['numeric_reference_cases']==18 and all(v is True for k,v in numeric.items() if k!='numeric_reference_cases')
assert all(ui.values())
dest=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');dest.mkdir(parents=True,exist_ok=True)
shutil.copyfile(out/'manifest.json',dest/'r29-build-manifest.json')
for name in ['calculator-math-checks.json','calculator-ui-checks.json','calculator-ui-inserted.cdxml','calculator-annotations.png']:
    shutil.copyfile(feature/name,dest/('r29-'+name))
shutil.copyfile(r/'work/manual-r29/interface-before.ini',out/'interface.ini')
report=f'''计算器 — 产率与质谱 — r29
EXE：{m['exe']}
SHA256：{m['exe_sha256'].upper()}

入口：右上角分子式/分子量卡片。窗口名“计算器”，一级标签“产率计算器 / 质谱计算器”，默认产率页。旁边默认展开数字软键盘，支持数字、小数、科学计数法、退格、清空和下一项。

产率：选中单分子或整条反应式。每个连通分子显示独立结构卡片，结构下面可输入质量或物质的量。质量默认 g，可选 mg、µg、kg；物质的量自动在不足100 mmol时显示mmol，其余显示mol，也可手选mmol/mol/µmol。切换单位保留实际用量。起始物用量可手工输入，也可从选定化合物的用量读取；计量系数默认为1，均可修改。产率按 n(产物)/[n(起始物)×产物系数/起始物系数]×100% 计算，不把超过100%的结果强行截断。默认反应箭头前为反应物、箭头后为产物，可通过每张卡片的角色选项修改。
“插入质量和产率”会将勾选且已填有效数量的化合物标注到对应结构下方；产品有有效起始用量时插入两行，如“100.0 mg”及“Yield = 88 %”。没有起始用量时可仅插入质量。所有标注是可编辑文字，居中排版，整次插入为一个撤销记录；在当前文档中重复插入会更新对应标注。原生文档和CDXML均可保存两行文字。标注单位默认自动，也可选g/mg/µg。

质谱：二级标签“正离子 / 负离子”，各默认显示4个常见加合物，分别可展开全部。离子表字体放大到18逻辑像素。反应式中可用顶部化合物列表切换当前分子的质谱数据。“复制精确分子量”直接复制中性分子的单同位素质量数值，保留4位小数，不受当前加合物选择影响。原有“复制结果”继续提供完整加合物结果。

检查：最终 r29 计算器专项和原当前修复回归均退出0。18组数值参考检查覆盖苯100mg/2mmol、单位转换、100mmol阈值、88%产率、计量系数、零分母及非法/科学计数法输入。真实控件检查覆盖默认标签页和键盘、双向换算、单位切换不改变用量、清空无效输入结果、软键盘、切换标签保留数据、正负离子默认四项与独立展开、单击插入。另验证空选择、单分子和6化合物滚动，标注居中、撤销/重做、重复更新和原生/CDXML往返。
computer use已实际从分子信息卡打开计算器，检查产率卡片、默认软键盘与质谱嵌套标签页。原有用户窗口和未保存内容保留；安装目录程序未替换。测试前interface.ini已恢复，构建清单与实际源码/EXE哈希核对通过。
'''
(dest/'计算器验收-r29.txt').write_text(report,encoding='utf-8')
print(json.dumps({'verified':True,'numeric_cases':numeric['numeric_reference_cases'],'ui_checks':len(ui),'sha256':m['exe_sha256']}))
