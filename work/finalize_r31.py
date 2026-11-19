from pathlib import Path
import json,hashlib,shutil
r=Path(__file__).resolve().parents[1];out=r/'out/Studio'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
m=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
assert Path(m['exe']).name=='MoleculeStudio-Progress-20261117-r31.exe' and sha(m['exe'])==m['exe_sha256']
for row in m['engine_sources'].values():assert sha(row['path'])==row['sha256'],row['path']
for name,value in m['studio_sources'].items():assert sha(r/'studio'/name)==value,name
for name,value in m['backend_sources'].items():assert sha(r/'studio'/name)==value,name
dest=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');dest.mkdir(parents=True,exist_ok=True)
for name in ['calculator','table','current']:
    source=r/f'work/test-r31-{name}'
    assert not (source/'failure.txt').exists(),name
    assert not (source/'calculator-ui-failure.txt').exists(),name
numeric=json.loads((r/'work/test-r31-calculator/calculator-math-checks.json').read_text())
ui=json.loads((r/'work/test-r31-calculator/calculator-ui-checks.json').read_text())
tlc=json.loads((r/'work/test-r31-table/tlc-persistence-checks.json').read_text())
assert numeric['numeric_reference_cases']==18 and all(ui.values())
assert tlc['native_cdxml_cdx'] and tlc['repaired_user_boards']==4 and tlc['repaired_user_spots']==19
shutil.copyfile(out/'manifest.json',dest/'r31-build-manifest.json')
for source in [r/'work/test-r31-calculator/calculator-math-checks.json',r/'work/test-r31-calculator/calculator-ui-checks.json',r/'work/test-r31-table/tlc-persistence-checks.json']:
    shutil.copyfile(source,dest/('r31-'+source.name))
copies=[]
for name in ['asf','asdf']:
    source=r/f'work/test-r31-table/{name}-repaired.cdxml'
    target=Path('C:/Users/ChaobaDa/Desktop')/f'{name}-修复副本-r31.cdxml'
    if target.exists():assert sha(target)==sha(source),'Do not replace a changed repair copy'
    else:shutil.copyfile(source,target)
    copies.append(str(target))
shutil.copyfile(r/'work/manual-r30/interface-before.ini',out/'interface.ini')
report=f'''MoleculeStudio r31 修复记录
程序：{m['exe']}
SHA256：{m['exe_sha256'].upper()}

计算器：删除起始物来源下拉框，保留手动输入起始物物质的量。删除质谱列表平均 m/z 列。平均分子量、精确分子量、当前选中加合物精确 m/z 各自使用“复制”按钮，只复制对应数值。产率页数字软键盘放大，质谱页隐藏软键盘，显示随所选化合物变化的结构预览。
TLC：所选斑点范围可再次点击取消，并切回全部斑点；直径仅按当前范围应用。导出时将位于负坐标的 TLC 与同页内容统一平移到纸张内，不改动当前编辑文档。保存重开保留按不同顺序添加的斑点 ID、位置、颜色和拖尾透明度。CDX ObjectTag Value 按 SDK 的 String/Long/Double 类型读写，兼容属性顺序变化。
原桌面文件保留，另存修复副本：
{chr(10).join(copies)}

验证：最终 r31 计算器专项、TLC/表格专项、当前修复回归均退出 0。计算器 18 组数值参考及真实控件操作通过；TLC 真实勾选/取消及所选/全部范围检查通过；原生、CDXML、CDX 往返通过。桌面文件副本合计 4 块 TLC 板、19 个斑点均保留。独立 CDX 检查覆盖 String/Double/Long、空字符串、属性顺序及无效长度拒绝。
文件关联：原 .cdxml UserChoice 指向 Applications/MoleculeStudio.exe，其命令曾指向 AppData 安装目录旧版本。保留 Windows UserChoice，仅更新该应用的打开命令与图标至本轮 r31，并用 Windows AssocQueryString 验证。桌面快捷方式同步指向 r31。安装目录程序未替换。
SDK 类型依据：https://chemapps.stolaf.edu/iupac/cdx/sdk/properties/ObjectTag_Type.htm
'''
(dest/'修复验收-r31.txt').write_text(report,encoding='utf-8')
print(json.dumps({'verified':True,'exe':m['exe'],'sha256':m['exe_sha256'],'repaired_copies':copies},ensure_ascii=False))
