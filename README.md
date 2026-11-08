# MoleculeStudio
Windows x64 分子结构绘图工具，支持化学式、CDXML、模板、本地化学计算，以及可选 AI 和图片转结构（OCSR）。

## 下载与安装
Release 手工上传四个 ZIP。四个 ZIP 都包含同名 `MoleculeStudio/` 根目录。
1. **Core-win64**：必选。解压后运行 `MoleculeStudio.exe`。含基础绘图、本地化学运行时、内置 AI 绘图 Skill 与手动更新下载器。
2. **AIBasic-addon-win64**（AI 基础）：可选。解压合并到核心包同名目录，获得 AI 助手、检索和对话画图；需自行配置服务地址、模型和 API Key。服务商可能收费，AI 读取的提示和附件可能发送到配置的服务商。
3. **AIAdvanced-addon-win64**（AI 高级）：可选。仅高级绘图插件，必须已安装 AI 基础与核心；解压合并到同一目录。
4. **OCSR-addon-win64**：可选。解压合并到核心包同名目录，获得本地图片/PDF 转结构；无需 AI 包。AI 识别图片功能需要同时安装 AI 基础与 OCSR。

不要把插件解压成 `MoleculeStudio/MoleculeStudio/`。四个包版本必须一致；AI 高级依赖 AI 基础。模型识别会出错，应核对键级、手性和电荷。
`legal/<包名>/third-party-sources/` 是随包的第三方对应源码，运行时不加载，可归档但再分发时必须保留获取方式及许可资料。

## 从源码构建
安装 Python 3 和 MinGW-w64 g++ / windres（包含 Direct3D 11 开发头文件）。
运行 `python studio/build.py --dx11 --compiler C:/tools/mingw64/bin/g++.exe --exe MoleculeStudio.exe`。
输出位于 `out/Studio/`。构建编辑器不需要 API Key，也不下载 OCR 权重。
运行时依赖可从同版本三个发布包提取。`chemistry_backend.ini` 使用相对路径 `Python=python\python.exe`，不要提交个人配置。
问题二修复包另设 `ChemistryPython=chemistry_python\python.exe`，仅供化学/核磁/三维后端使用；AI 继续使用 `Python`。未设置该项时兼容原配置。修复包的独立 3.10 运行时随 Core 提供，不依赖 OCSR 插件。
源码布局：`studio/` 为产品界面和后端；`studio/engine/` 为引擎覆盖实现；`engine/` 为基础引擎及第三方源码。构建脚本按清单选择链接文件。

## 开源与安全
自有代码沿用 Apache-2.0；第三方组件、运行时、字体、模型保留原许可证。参见 LICENSE、NOTICE、licenses/、PROVENANCE.md 和 docs/RELEASE.md。
历史来源说明保留，不作独立净室实现认证。不要把整个已使用的绿色版目录上传到 GitHub。
安全反馈请按 SECURITY.md 操作。贡献说明见 CONTRIBUTING.md。

## 外部 AI 绘图 skill
不使用内置 AI 时，可使用 [moleculestudio-draw](skills/moleculestudio-draw/SKILL.md) 生成可编辑 CDXML。基础文件绘图只需 Core；参见 [安装与使用说明](docs/EXTERNAL_AI_SKILL.md)。
## 字体与系统 API

软件不内置、不分发任何字体文件。界面与画布文字使用系统已安装字体，通过 GDI+ 按字体族名调用（默认 Arial 10，与 ChemDraw 默认一致），避免字体再分发与侵权风险；用户系统缺少指定字体时由系统字体回退，文档仍保留原字体名。
