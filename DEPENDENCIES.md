# 依赖关系与第三方组件

MoleculeStudio 0.95 绿色版（Windows x64，2026-11-01）

本文件说明运行包的模块分层、主要第三方组件、许可证类别和对应源码位置。
完整清单以 `licenses/index.json`（712 项，机器可读）和 `THIRD_PARTY_NOTICES.md`
（逐项许可证原文链接）为准；本文件是面向使用者和再分发者的总览。

## 1. 运行时分层

```
MoleculeStudio.exe                  C++ 主程序（MinGW-w64 静态链接，Apache-2.0）
├─ python\                          CPython 3.12.14 + 绘图/AI/识别桥接依赖
├─ chemistry_runtime\               Temurin JRE 21 + OPSIN + 核磁模型
│  ├─ java\                         Eclipse Temurin 21.0.12.1+1
│  ├─ opsin.jar                     OPSIN 2.9.0 / JNA-InChI 1.3.1 / InChI 1.07.2
│  ├─ nmr-ml-site\                  PyTorch 2.5.1+cpu、scikit-learn、CSP5 等
│  └─ python-site\                  NISPO 0.1.7 等
├─ ocsr_runtime\                   图片转结构（MolScribe 1.1.1 + 独立 Python 3.10 CPU 运行时）
├─ ai_plugins\                      绘图增强插件（纯 Python）
└─ AI 套件\                          AI 功能开关 + 内置 DeepSeek Harness
   └─ dsh_core\                     Node.js 24.21.0 + DSH 0.2.1-rc.2 + npm 依赖
```

各目录缺失时的行为：

| 目录 | 作用 | 缺失时 |
| --- | --- | --- |
| `AI 套件\` | AI 页总开关；内含 DSH | AI 页入口全部隐藏 |
| `ocsr_runtime\` | 图片转结构 / 文献识别 | 右上角“图片转结构”与 AI 识别入口隐藏 |
| `ai_plugins\` | 绘图增强插件 | “开始 AI 绘图”“试试示例”隐藏 |
| `python\` | 绘图后端与 AI 桥接 | AI 绘图与化学后端不可用 |
| `chemistry_runtime\` | 命名、核磁、结构整理 | 对应离线功能不可用 |

`AI 套件` 目录名可用环境变量 `MOLECULE_STUDIO_AI_RUNTIME` 覆盖；
`ocsr_runtime` 可用 `MOLECULE_STUDIO_OCSR_RUNTIME` 覆盖。

## 2. 主要第三方组件

| 组件 | 版本 | 许可证 | 位置 / 用途 |
| --- | --- | --- | --- |
| CPython | 3.12.14 | PSF-2.0 | `python\`，绘图/AI/OCSR 桥接 |
| Eclipse Temurin JRE | 21.0.12.1+1 | GPL-2.0-only WITH Classpath-exception-2.0 | `chemistry_runtime\java\`，运行 OPSIN |
| OPSIN | 2.9.0 | MIT | `chemistry_runtime\opsin.jar`，命名转结构 |
| JNA-InChI / InChI | 1.3.1 / 1.07.2 | LGPL-2.1-or-later AND MIT | `chemistry_runtime\opsin.jar` |
| NISPO | 0.1.7 | BSD-3-Clause | `chemistry_runtime\python-site\`，核磁位移预测 |
| CSP5 | 0.3.1 | MIT | `chemistry_runtime\nmr-ml-site\csp5\` |
| PyTorch | 2.5.1+cpu | BSD-3-Clause | `chemistry_runtime\nmr-ml-site\`、`ocsr_runtime\` |
| scikit-learn | 1.6.1 | BSD-3-Clause | 核磁模型 |
| SciPy / SymPy | 1.15.3 / 1.13.1 | BSD-3-Clause | 数值与符号计算 |
| PyArrow | 19.0.1 | Apache-2.0 | 核磁模型数据 |
| RDKit | 2024.9.6 | BSD-3-Clause | `python\Lib\site-packages\rdkit` |
| NumPy | 2.3.5 / 2.5.4 | BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0 | 科学计算 |
| pandas | 3.0.1 | BSD-3-Clause | 数据处理 |
| OpenCV (opencv-python) | 5.0.0.93 | Apache-2.0 | 图像处理 |
| OpenCV FFmpeg 插件 | 7.1 / OpenCV 5.x 构建 | LGPL-2.1-or-later | `opencv_videoio_ffmpeg500_64.dll` |
| RapidOCR (onnxruntime) | 1.4.4 | Apache-2.0 | OCSR 备选识别 |
| ONNX Runtime | 1.31.0 | MIT | RapidOCR |
| Pillow | 12.3.0 / 13.0.0 | MIT-CMU | 图像读写 |
| pypdfium2 | 5.14.0 | BSD-3-Clause | PDF 渲染（含 PDFium） |
| Shapely / pyclipper | 2.2.0 / 1.4.0 | BSD-3-Clause / MIT | 几何裁剪 |
| tqdm | 4.67.1 / 4.70.1 | MPL-2.0 AND MIT | 进度显示 |
| MolScribe | 1.1.1 | MIT（含模型卡） | `ocsr_runtime\`，结构识别 |
| Node.js | 24.21.0 | MIT | `AI 套件\dsh_core\node\node.exe` |
| DeepSeek Harness (DSH) | 0.2.1-rc.2 | MIT（各子包） | `AI 套件\dsh_core\dsh\` |
| sharp / libvips | 0.35.5 / libvips-42 | Apache-2.0 / LGPL-3.0-or-later | DSH 图像处理 |
| LibreOffice Kit | 0.1.5 | MPL-2.0 | DSH 文档转换 |
| tinyxml2 | 静态链接 | Zlib | 主程序 CDXML 解析 |

OCSR 运行时的组件清单与权重校验值另见
`licenses/notices/OCSR_NOTICES.md` 和 `ocsr_runtime\LICENSES\`。

## 3. 许可证分布（licenses/index.json）

| 分组 | 数量 | 说明 |
| --- | ---: | --- |
| permissive | 559 | MIT、BSD、ISC、Zlib、PSF 等宽松许可 |
| apache | 65 | Apache-2.0 |
| gpl-cpe | 51 | GPL-2.0 + Classpath Exception（Temurin 模块） |
| lgpl | 13 | LGPL-2.1 / LGPL-3.0 |
| mpl | 6 | MPL-1.1 / MPL-2.0 |
| other | 18 | bzip2、XZ、libpng、AOM 专利许可、MIT-CMU 等 |
| **合计** | **712** | 710 项随包分发，2 项 not_applicable |

按生态：npm 560、java 51、native 47、python 39、jar 15。

## 4. 许可证义务摘要

- **MIT / BSD / ISC / Zlib / PSF / MIT-CMU**：保留版权声明和许可证原文，可再分发。
- **Apache-2.0**：保留 LICENSE 与 NOTICE；修改过的文件需注明修改。
- **GPL-2.0 + Classpath Exception（Temurin）**：JRE 以 GPLv2 分发，Classpath
  Exception 允许与自有程序链接；需随包提供对应源码获取方式或可替换的兼容 JRE。
- **LGPL-2.1 / LGPL-3.0**：以动态库/独立进程方式使用；保留许可证，允许用户替换
  兼容 DLL/JAR 并可为调试反向工程。libvips-42.dll、JNA-InChI 属于此类。
- **MPL-1.1 / MPL-2.0**：文件级 copyleft；修改过的 MPL 文件需继续以 MPL 提供，
  未修改文件可与其它代码组合分发。
- **其它**：bzip2、XZ 公共领域声明、libpng、AOM 专利许可等按各自原文执行。

第三方代码保留各自许可证，本项目许可证（Apache-2.0）不覆盖第三方代码。
本项目不包含 ChemDraw 代码、数据或 ActiveX/COM 自动化；CDXML/CDX 仅作为公开
文件格式互操作。

## 5. 对应源码与校验

- 第三方对应源码包：`MoleculeStudio-0.9.4.1-ThirdPartySources.zip`（与绿色包同一下载页）。
- 源码地址、版本、校验值：`SOURCE_AVAILABILITY.json`；使用说明：`SOURCES.txt`。
- 许可证总索引：`licenses/index.json`；许可证原文：`licenses/{common,jar,java,native,npm,python}\`。
- 离线校验：

  ```
  python\python.exe tools\verify_release_materials.py
  ```

  该脚本校验 712 项许可证文件的存在性与 SHA-256、占位符清零情况，以及
  `SOURCES.txt`、`NOTICE`、`THIRD_PARTY_NOTICES.md` 的完整性。
