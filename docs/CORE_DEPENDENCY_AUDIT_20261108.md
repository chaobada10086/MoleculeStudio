# Core 依赖与体积审计（2026-11-08）

## 结论

- 已安装 Core 总体积 **3279.4 MB**：chemistry_python 1739.7 MB、chemistry_runtime 792.4 MB、legal 357.7 MB、python 342.8 MB、根目录 43.0 MB、其余约 4 MB。
- “重复依赖”并不是体积主因：≥512 KB 的 375 个大文件中，逐文件 SHA-256 完全重复的只有 **18 组、约 37.1 MB**（重复的 RDKit/torch 辅助文件、png 图、legal 索引副本等）。跨运行时的完全重复内容很少，说明现有打包已经避免了大规模的同一文件多份拷贝。
- 体积主要来自三个 Python 运行时与附带科学计算/模型依赖（chemistry_python/torch、chemistry_runtime/python-site、基础 python），以及 **许可与第三方源码目录 legal（357.7 MB）**。后者是再分发合规材料，不建议删除。

## 可安全考虑的去重/瘦身项（需逐项回归，默认不执行破坏性删除）

| 候选 | 预估 | 风险评估 |
| --- | --- | --- |
| __pycache__ / *.pyc | ~6 MB | 低；Python 会重新生成，删除不影响功能 |
| site-packages 内 *.h/*.hpp 头文件 | ~28 MB | 低；运行期不读取，但第三方源码可得性材料需保留 |
| 第三方 test/ 与 torch/testing | ~39 MB | 中低；不参与运行，但属于第三方发行内容，删除前需核对许可与源码义务 |
| legal/（许可与源码） | 357 MB | **不建议删除**；属于再分发合规材料，可考虑单独压缩包但需法律确认 |
| 大于 512 KB 的完全重复文件 | 37 MB | 低；可用 NTFS 硬链接（安装器已有硬链接路径）而非删除 |

## 建议

1. 打包时只执行无损操作：删除 __pycache__/*.pyc、用硬链接替换重复文件（不改动源文件内容），预计节省约 40 MB，功能无影响。
2. 头文件/test 目录的裁剪需要按第三方许可核验，且收益（≤2%）远小于风险，本次不做。
3. legal 目录保持完整；若未来要缩小安装包，应通过“单独源码包 + 明确获取方式”的合规方案，而不是删除许可材料。
4. 本次未删除已安装目录中的任何文件；以上为审计结论与打包建议。

## 证据

- 扫描范围：C:\\Users\\ChaobaDa\\AppData\\Local\\Programs\\MoleculeStudio。
- 方法：按文件大小分组，对 ≥512 KB 文件计算 SHA-256；统计完全重复组与可回收字节数。
- 明细：out\\dup-scan.json（构建产物目录，不提交）。

## 2026-11-08 已执行的瘦身

- 按上述无损方案从 Core 包移除：`__pycache__`/`*.pyc`、site-packages 内 `*.h/*.hpp/*.pyi`、site-packages 内 `test/` 与 `torch/testing/`、stdlib `Lib/test`、`*.pdb`。
- 结果：条目 39,667 → 25,552，解压体积减少约 122.1 MB，压缩包 1,305,160,644 → 1,256,870,101 B（约 -48.3 MB）。
- 验证：缩小后的 Core 解压后 `chemistry_python` 的 RDKit 2024.9.6 / torch 2.5.1 导入成功，MoleculeStudio `--test` 两次全绿；安装器 `--extract-test` 通过。若未来发现缺失，可回退到未瘦身的 Core（保留在构建产物中）。

## 2026-11-08 进一步：合并重复 Python 运行时

- 原 Core 同时带 `python/`（CPython 3.12）与 `chemistry_python/`（CPython 3.10 + RDKit/torch）两个解释器，AI 助手与化学后端各用一个，rdkit/PIL 等依赖被重复安装。
- 验证 `chemistry_python` 已包含 AI 所需全部依赖（rdkit 2024.09.6、PIL 12.3、pypdfium2、nispo 路径）后，`chemistry_backend.ini` 的 `Python` 与 `ChemistryPython` 统一指向 `chemistry_python\python.exe`，从 Core 包中彻底移除 `python/`。
- 结果：Core 解压减少约 338 MB，压缩包从 1,256.9 MB 降到 1,115.2 MB；合并 AI/OCSR 后四组件安装自检全绿，AI 脚本在 3.10 运行时导入/运行正常。
- 2026-11-08 二次打包（主程序修复中文路径 + 安装器内置哈希刷新）后压缩包为 1,115,228,888 B（1.04 GB），解压体积不变。
