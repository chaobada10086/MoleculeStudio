from pathlib import Path
import json,hashlib,shutil
root=Path.cwd();out=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs')
m=json.loads((root/'out/Studio/manifest.json').read_text());exe=Path(m['exe']);sha=hashlib.sha256(exe.read_bytes()).hexdigest();assert sha==m['exe_sha256']
for key,want in m['studio_sources'].items():assert hashlib.sha256((root/'studio'/key).read_bytes()).hexdigest()==want,key
for key,data in m['engine_sources'].items():assert hashlib.sha256(Path(data['path']).read_bytes()).hexdigest()==data['sha256'],key
for key,want in m['backend_sources'].items():
    assert hashlib.sha256((root/'studio'/key).read_bytes()).hexdigest()==want,key
    assert hashlib.sha256((root/'out/Studio'/key).read_bytes()).hexdigest()==want,key
checks=root/'work/test-r7-release-current';tables=root/'work/test-r7-release-table'
assert not (checks/'failure.txt').exists() and not (tables/'failure.txt').exists()
p=json.loads((checks/'projection-checks.json').read_text());chem=json.loads((checks/'independent-chemistry.json').read_text());tlc=json.loads((tables/'tlc-checks.json').read_text())
assert p['conversion_pairs']==25 and chem['checks']==92 and (checks/'after-chemdraw.cdxml').exists() and all(tlc.values())
report=f'''MoleculeStudio r7 · 可编辑分子投影
日期：2026-11-16

程序：{exe}
SHA256：{sha.upper()}
构建清单：{root / 'out/Studio/manifest.json'}

本次改动
1. 费歇尔、锯架式、纽曼投影均以真实原子和化学键构建，共用普通分子编辑和分子式/分子量计算。
2. 保留原有模板的几何外观；纽曼的圆环与前点由分子视图生成，前后碳拥有独立编号。
3. 双击端点编辑元素或已定义基团，保留鼠标捕捉提示；支持普通原子/键增删。
4. 右键 → 转换立体投影：费歇尔、锯架交叉/重叠、纽曼交叉/重叠。
5. 转换保留分子连接关系、基团和立体信息；支持一步撤销/重做。
6. 可识别的旧版矢量投影在打开时升级为分子图。CDXML/CDX 保存投影数据，标准 ObjectTag 保留 ChemDraw 另存后的投影信息。

操作
请重新从桌面 MoleculeStudio 快捷方式打开 r7。选中投影后显示分子式与分子量；双击端点填写基团；在投影上右键选择转换。
当前安装目录内的旧 EXE 未覆盖；桌面入口使用上述经过测试的独立版本。

验证（发布 EXE）
--current-fixes-check：退出码 0，17 份回归报告。
投影：16 种模板、25 组转换、36 个糖投影分子式。
独立 RDKit 化学与图形构型校验：92 项通过，覆盖绝对构型及转换前后异构 SMILES。
实际右键操作：C8H18O 从费歇尔转纽曼，分子量保持 130.2 g/mol。
实际 ChemDraw 另存文件重新导入：保留两个投影中心与 C8H18O，无重复圆环/线条/箭头。
--table-check（启用 tlc-check）：退出码 0，表格与 TLC 回归通过。
源码与构建清单、化学后端副本的哈希校验通过。

适用范围
转换轴为饱和四面体碳之间的单键；环连接不能直接展开时保留原结构并提示原因。单中心费歇尔沿末端 C–C 键转换时，可将隐式氢显式化，分子式不变。未定义的 R 基团不虚构分子量。

详细测试：{checks}
TLC 测试：{tables}
源码补丁：{out / 'projection-molecule-r7.patch'}
'''
(out/'立体投影底层重写-r7.txt').write_text(report,encoding='utf-8-sig')
shutil.copyfile(root/'out/Studio/manifest.json',out/'r7-build-manifest.json')
print(json.dumps({'exe':str(exe),'sha256':sha.upper(),'source_and_backend_hashes_verified':True},ensure_ascii=False))
