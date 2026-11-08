# OCSR 模块审核报告（0.95-20261108 A+C 精简包）

审核日期：2026-11-08　|　审核对象：`OCSR模块_A+C_2026-11-08.zip`（850,074,033 B）
仓库基线：`38613d7`　|　模块版本：ocsr_service 1.3.1 + `ocsr-finetune-v3` 补丁（A+C）

## 1. 结论摘要

**可以替换旧 OCSR 模块，但必须使用第 9 节重打包后的 ZIP，不能直接发布输入 ZIP。**

| 结论项 | 结果 |
| --- | --- |
| 内容可用性 | 通过。自带 CPython 3.10.22 + venv，torch 2.5.1+cpu / RDKit 2024.9.6 / scipy / numpy / Pillow / OpenCV / MolScribe 1.1.1 均可导入；XML 行协议 ping/status/recognize 全部 `ok="1"`，苯环测试图识别为 `c1ccccc1`（6 原子 / 6 键，置信度 0.875） |
| 与 app 协议兼容 | 兼容。旧→新 `ocsr_service.py` 仅新增字段与内部逻辑（fine-tune 补丁 + 大选区缩放），未改任何 op 名称、请求属性或响应必需属性；`studio/ocsr_dialog.hpp`、`studio/ai_ocsr_bridge.py` 无需改动 |
| 安装器兼容 | 重打包后兼容。所有条目位于 `MoleculeStudio/` 根下；`MANIFEST-OCSR.json` 已按真实文件重生成（32,829 条），字段与旧包/安装器 legacy Verify 路径一致；ZIP 内无重复名、无绝对/越界路径、无 `.installer-` 保留路径 |
| 许可证材料 | 输入 ZIP 缺失 `legal/OCSR/**`（752 个文件、6.6 MB）。已于重打包时从上一发布包原样补回，并逐文件按旧包清单校验 SHA-256（752/752 一致）；仍有 2 个 Python 组件缺许可文本、legal 元数据版本陈旧（见 6.2） |
| 阻断项 | 无 |

## 2. 输入指纹

| 文件 | 大小（B） | SHA-256 | 条目 |
| --- | --- | --- | --- |
| 新模块 `OCSR模块_A+C_2026-11-08.zip` | 850,074,033 | `ab2318ed4b82891abf3c145b779b7627ba713f567a71c027fd502ffeba8f2288` | 34,751（文件 32,078 + 目录 2,673），解压 2,270,699,338 B（2.115 GiB） |
| 旧模块 `MoleculeStudio-0.95-20261107-canvas-fix-OCSR-addon-win64.zip` | 1,504,763,253 | `4f0acc98a2c692779e02ac6d53fca6a1f62342e817b73c9a7d84bf9801334c33` | 29,454（全部为文件），解压 2,973,623,724 B（2.769 GiB） |
| 仓库既有 `studio/ocsr_service.py`（= 旧包内文件） | 129,855 | `3d442919394587e7…`（与旧包逐字节一致） | — |

新包为 **flat 结构**（无 `MoleculeStudio/` 前缀）：根目录 `MANIFEST-OCSR.json`、`ocsr_service.py`、`ocsr_client.py`、`README-OCSR.txt`、`recognize.cmd`、`启动OCSR服务.cmd`、`识别图片.cmd`、`调用说明.md`、`recognition_records/`、`ocsr_runtime/{python,venv,models,LICENSES}/`。

## 3. 审核范围与方法

- ZIP 层：CRC（`testzip`）、条目名安全、重复项、符号链接、加密位、压缩方式、可执行文件清单、PE 头一致性、脚本内容、凭据扫描、`.pth`/sitecustomize 自启动代码、文件类型与大小异常。
- 内容层：MODELS.json / `.sha256` / 实际权重三方对照；venv 与内嵌 CPython 版本；venv 可移动性；协议行为与 app 端代码对照。
- 功能层：真实启动服务，按 app 相同协议发送 ping/status/recognize，并解析记录 JSON 与 CDXML；另用重打包产物解压安装目录做端到端复测。
- 仓库层：`studio/ocsr_service.py` 同步、构建、两轮 `--test`、Python 回归用例。
- 证据文件：`out/ocsr-audit/`（`inputs-summary.json`、`security-summary.json`、`diff-license-summary.json`、`functional-result.json`、`package-verification.json`、`repack-result.json`；`out/` 已被 gitignore，不入库）。

## 4. 安全与完整性扫描

| 检查 | 结果 |
| --- | --- |
| ZIP CRC | 通过（`testzip() is None`） |
| 路径穿越 / 绝对路径 / 反斜杠 / 重复名 | 0 / 0 / 0 / 0 |
| 符号链接、加密条目 | 0 / 0 |
| 压缩方式 | deflate 30,566 条 + store 4,185 条 |
| 可执行文件 | 627 个（`.exe` 29 / `.dll` 149 / `.pyd` 449），全部带合法 `MZ` 头，无伪装 PE；全部位于 `ocsr_runtime/python/` 与 `venv/site-packages/`，根目录无 exe |
| 脚本 | 根目录仅 3 个 `.cmd` + `ocsr_client.py` + `ocsr_service.py`；`.cmd` 只做「定位本包 python → 调 client/service」，无下载、无注册表、无计划任务 |
| 解释器启动注入 | 仅 `distutils-precedence.pth`（setuptools 标准文件）；无 `sitecustomize.py` / `usercustomize.py` |
| 凭据/密钥扫描 | 0 命中（根脚本、配置、文档、legal 文本） |
| 运行时自联网 | 入口脚本静态审查无 `urllib/requests/huggingface_hub` 下载调用；模型为包内本地文件（权重 + MODELS.json + `.sha256`），识别过程不联网 |
| 权重完整性 | `swin_base_char_aux_1m680k.pth` 384,186,873 B，SHA-256 `dc832a7a0f303ac3c9f6e114397b01df1e94b36a714888034833e12a439303bf`，与 `MODELS.json` 及 `.pth.sha256` 三方一致；服务实际加载成功 |
| 版本 | 内嵌 CPython 3.10.22（MSC v.1944 64bit）；torch 2.5.1+cpu、torchvision 0.20.1+cpu、RDKit 2024.9.6、numpy 1.26.4、scipy 1.13.1、Pillow 12.3.0、opencv-python-headless 4.5.5.64、timm 0.4.12、MolScribe 1.1.1 |
| 噪音 | 包含 3,373 个 `.pyc`（469 个 `__pycache__` 目录）与 746 个 0 字节文件，属运行残留/正常空 `__init__.py`，不影响功能，见 11 节建议 |

可疑点复核：根 `调用说明.md` 含 `https://`（模型/上游来源说明）与 PowerShell 示例字样；`ocsr_client.py` 含 `subprocess`（启动本包服务）；均为正常用途，无外联/提权行为。

## 5. venv 可移动性与环境变量

- ~~输入包 `ocsr_runtime/venv/pyvenv.cfg` 的 `home =` 指向打包机绝对路径~~ → **已于 2026-11-08 二次打包彻底移除**：包内删除 `ocsr_runtime/venv/Scripts` 与 `pyvenv.cfg`，运行时只保留 `venv/Lib/site-packages`；原生 UI、AI 桥、`ocsr_client.py` 与三个 `.cmd` 统一改为“包内基础 Python + `PYTHONPATH`”，不再需要任何路径修正。
- 附带修复：原生 UI `startService()` 的运行时存在性检查由窄字符 `std::ifstream` 改为 `GetFileAttributesW`，安装路径含中文/非 ASCII 字符时也能启动服务（这是“图片转结构连不上”的真正原因）。
- 本次功能验证按安装器语义把 `home` 重写为解压目录后运行，ping/status/recognize 全部通过。

## 6. 许可证与来源材料

### 6.1 已补齐（重打包时）

输入 ZIP **完全缺失** `legal/OCSR/**`。已从上一发布包（`…20261107-canvas-fix…`）原样补回 **752 个文件 / 6,933,466 B**，并逐文件按旧包 `MANIFEST-OCSR.json` 的 `bytes`/`sha256` 校验：**752/752 一致**。结构：
- `legal/OCSR/licenses/` 729（npm 560、java 51、native 49、python 40、jar 15、common 8、notices 2、根 4）
- `legal/OCSR/build-recipes/` 11、`legal/OCSR/msvc-runtime/` 3（MSVC 运行库许可，与包内 `venv/Scripts/*.dll`、`python/**/msvcr*.dll` 对应）
- `legal/OCSR/{LICENSE,NOTICE,PROVENANCE.md,THIRD_PARTY_NOTICES.md,DEPENDENCY_INVENTORY.json,SOURCE_FILES.json,HISTORICAL_SOURCE_INSTRUCTIONS.txt,TRADEMARKS.md,CURRENT_DISTRIBUTION.txt}`

包内 `ocsr_runtime/LICENSES/` 19 个文件与旧包一致（MolScribe MIT、torch/torchvision、numpy、scipy、pandas、matplotlib、Pillow、pypdfium2、rdkit、albumentations、OpenNMT-py、SmilesPE、timm、scikit-image、opencv×2、huggingface-hub 等）。

### 6.2 仍未闭合（需发布负责方处理，本次未伪造任何许可）

1. **2 个 Python 组件缺许可文本**：`sentencepiece 0.2.2`（METADATA 声明 `License-Expression: Apache-2.0`）与 `tensorboard_data_server 0.7.2`（METADATA 声明 `License: Apache 2.0`）——其 `dist-info` 内无 LICENSE 文件，`ocsr_runtime/LICENSES/` 与 `legal/OCSR/licenses/index.json` 也未收录。其余 **91/93** 个 dist-info 组件自带许可文件（`dist-info/licenses/` 等）。修复方式：补 Apache-2.0 全文 + 组件归属与来源（上游 URL 已知），不建议删除组件（torch 生态依赖）。
2. **legal 元数据版本陈旧**：`legal/OCSR/licenses/index.json` 的 `package.version = 0.9.4.1`、`counts.total = 712`、`by_ecosystem.python = 39`，`THIRD_PARTY_NOTICES.md` 亦标注 `0.9.4.2`；`DEPENDENCY_INVENTORY.json`（117 条）虽与实际 OCSR 运行时 **完全对应**（93 个 `dist-info` 无缺项/多项），但其版本号、index 的组件集合未随 0.95 更新。
3. **对应源码材料**：`legal/OCSR/SOURCE_FILES.json` 内容为 `[]`，`HISTORICAL_SOURCE_INSTRUCTIONS.txt` 指向历史的 `MoleculeStudio-0.9.4.1-ThirdPartySources.zip`（SHA-256 `cdca01bc…`），`CURRENT_DISTRIBUTION.txt` 又声称源材料在包内 `third-party-sources/`（本包并不存在该目录）。三方表述不一致，属**既有**合规缺口（旧包同样存在），本次仅原样补回、未改动。

结论：本次补齐了 0.95 发布所需的 addon legal 目录；但完整合规仍需处理 6.2 的 3 项（尤其 Apache-2.0 文本与源码可得性表述）。

## 7. 新旧模块差异与精简原因

逐文件比对（以旧包清单的 `sha256` 与输入 ZIP 实际字节计算，忽略 `__pycache__`/`.pyc` 噪音）：

| 类别 | 数量 | 说明 |
| --- | --- | --- |
| 完全一致 | 28,694 | 运行时与依赖主体未变 |
| 变更 | 5 | `ocsr_service.py`、`ocsr_runtime/models/{MODELS.json,pth,pth.sha256}`、`ocsr_runtime/venv/pyvenv.cfg` |
| 新增 | 3,379 | 3,373 个 `.pyc`（469 个 `__pycache__`，运行残留）+ 6 个根文件（`ocsr_client.py`、3 个 `.cmd`、`调用说明.md`、新 `MANIFEST-OCSR.json`） |
| 删除 | 754 | `legal/OCSR/**` 752、`OCSR-HOTFIX.txt`、`ocsr_runtime/README.txt` |

体积：解压后 2.769 GiB → 2.115 GiB（−654 MiB）。主因：权重由 **1.13 GB 全量 checkpoint** 变为 **366 MB weights-only 重存版**（去掉优化器状态，−715 MiB）；其次是缺 legal（−6.6 MB）；新增 `.pyc` 约 +57 MB（`added_bytes = 59,833,834`）。代码/依赖主体（28,694 个文件）逐字节相同，故不存在“删掉 app 运行必需文件”的情况：旧包 `OCSR-HOTFIX.txt` 是 hotfix2 历史说明（其内容已被新包 `调用说明.md` 取代），`ocsr_runtime/README.txt` 的运行时说明同样被 `调用说明.md` 覆盖，二者均非运行必需（重打包时未补回，见第 9 节）。

app 必需项核对（均在）：`ocsr_service.py`（根，app 以 `appDir\ocsr_service.py` 启动）、`ocsr_runtime/python/cpython-3.10-windows-x86_64-none/python.exe`、`ocsr_runtime/venv/Lib/site-packages/`、`ocsr_runtime/models/swin_base_char_aux_1m680k.pth`、`ocsr_runtime/models/MODELS.json`、`ocsr_runtime/models/*.sha256`、`ocsr_runtime/LICENSES/`、`recognition_records/`（安装器/服务会创建）。

`ocsr_service.py` 变更要点（旧 129,855 B → 新 142,973 B，+223/−12 行）：新增 `FINETUNE_PATCH_ID=ocsr-finetune-v3`、`resolve_finetune_input_size()`（stock 384 / 微调 768，`MOLSCRIBE_INPUT_SIZE` 优先）、`MOLSCRIBE_MAX_LEN` 默认 2000、`finetuned_*.pth` 候选、芳香原子符号归一化；新增 `MAX_SELECTION_PX=4096`（超出自动缩图，硬上限 `MOLSCRIBE_HARD_MAX_SELECTION_PX=20000`，旧版 >5000 直接报错）；状态/记录增加 `input_size/max_len/model_source/input_size_source/finetune_patch/selection_downscaled` 字段。**协议 op 与请求属性未变。**

## 8. 功能验证（证据）

环境：模块解压目录，按下述命令启动；无网络；CPU。

```powershell
# 1) 按安装器语义修正 venv（安装器/原生 UI/AI 桥都会做）
#    pyvenv.cfg: home = <解压目录>\ocsr_runtime\python\cpython-3.10-windows-x86_64-none
# 2) 启动服务
& .\ocsr_runtime\venv\Scripts\python.exe .\ocsr_service.py --serve `
  --models .\ocsr_runtime\models --cache .\out\ocsr-audit\run\cache `
  --records .\out\ocsr-audit\run\records
# 3) 逐行 XML
<request op="ping" id="p1"/>
<request op="status" id="s1" hash="1"/>
<request op="recognize" id="r1" page_image="<cache>\page-benzene.png"
         crop_x="0" crop_y="0" crop_w="127" crop_h="143"
         source_kind="image" source_path="<repo>\out\selftest-png\copy-as-png-benzene.png"
         dpi="300" vector="0"/>
```

| 请求 | 耗时 | 关键返回 |
| --- | --- | --- |
| ping | 0.21 s | `ok="1" service="1.3.1" pong="1"` |
| status（hash=1，未加载模型） | 0.10 s | `ok="1" device="cpu" torch="2.5.1+cpu" model_loaded="0" model_size="384186873" model_sha256="dc832a7a…" input_size="384" max_len="2000" model_source="pending" finetune_patch="ocsr-finetune-v3" dependency_check="deferred"` |
| recognize（冷启动，含加载） | 8.75 s | `ok="1" atoms="6" bonds="6" fragments="1" confidence="0.875066" insertable="1" cdxml=… record=…`；记录内 `result.smiles = "c1ccccc1"`、`model.load_seconds=1.1`、`inference_seconds=0.517`、`model.input_size=384`、`max_len=2000`、`model_source=baseline`、`validation.levels.model_molfile_to_candidate=passed`、`candidate_to_cdxml=passed`、`max_normalized_distance=4.95e-06` |
| recognize（热启动复测） | 0.55 s / 0.43 s | 同一图片结果一致（`atoms=6 bonds=6 confidence=0.875066`） |
| 从**重打包 ZIP** 解压出的 `MoleculeStudio/` 目录端到端复测 | ping/status 同上；recognize 0.43 s | 结果同上，证明“安装器解压后的目录布局”可直接工作 |

补充回归（用模块自带 venv 执行仓库用例）：

```powershell
& <module>\ocsr_runtime\venv\Scripts\python.exe studio\tools\ocsr_runtime_regression.py   # 7 tests OK
& <module>\ocsr_runtime\venv\Scripts\python.exe tools\test_ocsr_mapping_rejection.py       # 2 tests OK
```

## 9. 重打包产物

| 项目 | 值 |
| --- | --- |
| 路径 | `C:\Users\ChaobaDa\Documents\GitHub\MoleculeStudio\out\repack-20261108\MoleculeStudio-0.95-20261108-OCSR-addon-win64.zip`（构建产物，不提交） |
| 大小 | **833,407,453 B**（794.8 MiB / 0.83 GB），旧包 1,504,763,253 B，**−44.6%**（2026-11-08 二次打包） |
| SHA-256 | `d9977a679f51852d95b3e4348304ad7d133d851397bc61fbb68dff970b032822` |
| 条目 | 32,812（32,059 新包文件 + 752 legal + 1 `MANIFEST-OCSR.json`；全部位于 `MoleculeStudio/` 下，无目录条目；已移除 18 个 venv 启动器/配置文件） |
| 解压后 | 2,278,351,288 B（2.122 GiB） |
| MANIFEST | `version="0.95-20261108"`、`package="OCSR"`、`package_revision="20261108-portable-runtime"`、`files` 32,811 条（真实文件 → `bytes`+`sha256`，不含自身），6,565,335 B |
| 补回 legal | 752 个文件，逐文件与旧包清单 SHA-256 比对 752/752 一致 |

独立校验（`python out/ocsr-audit/verify_package.py`，26 项全 PASS）：CRC、无重复名、`MoleculeStudio/` 前缀齐全、无绝对/越界路径、无目录条目、MANIFEST 与条目集合完全对应、**逐文件 bytes+sha256 全部一致**、MODELS.json/`.sha256`/权重哈希一致、服务版本 1.3.1 与 7 个 op 齐全、legal 752 一致、`venv/Scripts/python.exe` 为 PE、pyvenv.cfg 可重定位。

## 10. 应用/安装器兼容性结论

- **安装器**（`installer/OnlineInstaller.cs`）：`Extract()` 只接受 `MoleculeStudio/` 前缀条目并重算 SHA-256 写 `.installer-OCSR.json`；本包满足。legacy 路径（无 receipt 时读 `MANIFEST-OCSR.json` 校验 `bytes/sha256`）也满足。二次打包后包内不再包含 `ocsr_runtime/venv/pyvenv.cfg`，安装器的 `GeneratedConfig` 逻辑对该文件变为可选（旧安装收据仍兼容）。
  **已更新（P4 关闭）**：2026-11-08 重新打包时已把安装器内置的 Core/OCSR 哈希、字节数与 `updates/stable.json` 一并刷新为本包计算值，并重建 `MoleculeStudio-OnlineSetup.exe`；安装器 `--feed-test`、`--extract-test 3` 通过。
- **原生 UI**（`studio/ocsr_dialog.hpp`）：启动参数 `--serve --models --cache --records`、op 集合 `status/render/recognize/detect/export_crop/confirm`、响应属性（confidence/atoms/bonds/fragments/insertable/crop/overlay/cdxml/record/warnings…）与新版一致；status 的额外属性被忽略。大选区由“>5000 报错”变为“>4096 自动缩图（硬上限 20000）”，app 端无需改动。
- **AI 桥**（`studio/ai_ocsr_bridge.py`）：只依赖 `ok/id`、`insertable`、`cdxml` 与 `warning` 子节点、各 op 的既有属性，全部保持；`prepare_runtime()` 已能重定位 venv。
- **仓库源码**：`studio/ocsr_service.py` 已更新为 1.3.1（143,436 B，SHA-256 `182c6110…`，含 Pillow 解码修复），新增 `studio/ocsr_client.py`（5,993 B，SHA-256 `81d911c5…`），`studio/build.py` 增加把 `ocsr_client.py` 复制到构建输出（随发布包分发命令行客户端）。

## 11. 问题清单（分级）

**阻断：无**

**需修复（发布前）**
1. 不能直接发布输入 ZIP：flat 结构（安装器要求 `MoleculeStudio/` 前缀）、MANIFEST 过期（754 个登记文件缺失、3,379 个实际文件未登记、`ocsr_service.py` 等 5 个文件哈希不符）、缺 legal。→ 使用第 9 节重打包产物。
2. legal 合规缺口（6.2）：sentencepiece / tensorboard_data_server 缺许可文本；索引与声明版本陈旧；`SOURCE_FILES.json` 为空且源码可得性表述互相矛盾。
3. 安装器/版本清单需同步新包哈希与版本（由 P4 安装器任务完成）。

**建议**
1. 清理包内 3,373 个 `.pyc`（469 个 `__pycache__`）以减少体积/提高可重现性；打包前用 `python -B` 或删除缓存。
2. ~~`recognize.cmd`/`识别图片.cmd` 也加入 pyvenv.cfg 修正~~ → **已解决**：三个 `.cmd` 与 `ocsr_client.py` 统一使用基础 Python + `PYTHONPATH`，不再依赖 pyvenv.cfg。
3. `ocsr_client.py` 第 4/68 行非 raw 字符串含 `\S`、`\l`，Python 3.12 会告 `SyntaxWarning`；建议改 raw 字符串（本次为保持与审核输入一致未改）。
4. 权重为 weights-only 重存版（与旧包权重哈希不同）：本次单图验证识别正确，建议发布前用同一批真实图与旧包做对照识别，确认推理结果一致。
5. 把“legal 补回 + MANIFEST 重生成 + pyvenv 归一化 + 体积检查”固化为打包脚本（可复用本次 `out/ocsr-audit/repack.py` 逻辑）。

## 12. 仓库改动与验证

- `studio/ocsr_service.py`：更新为 1.3.1（含 A+C 补丁）。
- `studio/ocsr_client.py`：新增（包内命令行客户端，随构建输出分发）。
- `studio/build.py`：构建时复制 `ocsr_client.py` 到输出目录。
- `docs/OCSR_AUDIT_20261108.md`：本报告。
- 构建：`studio\build.py --dx11 --exe MoleculeStudio.exe --compiler C:/tools/mingw64/bin/g++.exe` 成功（仅有既有 unused 警告）。
- 自检：清空目录后 `MoleculeStudio.exe --test <dir>` 连续两轮全绿。
- `studio/ai_ring_catalog.json` 构建副作用已还原；工作区最终干净（`dist/`、`out/` 均 gitignore）。

### 自检结果

| 轮次 | 退出码 | 用时 | tests.json | failure.txt |
| --- | --- | --- | --- | --- |
| 1 | 0 | 118.7 s | 50 项：49 项为 true；`old_ui_linked=false`（期望值，旧 UI 未链接） | 无 |
| 2 | 0 | 123.9 s | 50 项：49 项为 true；`old_ui_linked=false`（期望值） | 无 |

### 提交

本次源码同步与报告为同一提交（中文提交信息：**OCSR：更新服务到 1.3.1，审核并重打包 20261108 精简模块**）。提交哈希由交付说明记录；按任务要求未 amend、未 push。
