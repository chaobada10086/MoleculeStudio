# MoleculeStudio
Windows x64 分子结构绘图工具，支持化学式、CDXML、模板、本地化学计算，以及可选 AI 和图片转结构（OCSR）。

## 下载与安装
Release 手工上传且仅上传以下三个 ZIP。三个 ZIP 都包含同名 `MoleculeStudio/` 根目录。
1. **Core-win64**：必选。解压后运行 `MoleculeStudio.exe`。含基础绘图和本地化学运行时。
2. **AI-addon-win64**：可选。解压合并到核心包的同名目录，获得 AI 助手与高级绘图；需自行配置服务地址、模型和 API Key。服务商可能收费。AI 读取的提示和附件可能发送到配置的服务商。
3. **OCSR-addon-win64**：可选。解压合并到核心包的同名目录，获得本地图片/PDF转结构；无需 AI 包。AI 识别图片功能需要同时安装 AI 与 OCSR。

不要把插件解压成 `MoleculeStudio/MoleculeStudio/`。三个包版本必须一致。模型识别会出错，应核对键级、手性和电荷。
`legal/<包名>/third-party-sources/` 是随包的第三方对应源码，运行时不加载，可归档但再分发时必须保留获取方式及许可资料。

## 从源码构建
安装 Python 3 和 MinGW-w64 g++ / windres（包含 Direct3D 11 开发头文件）。
运行 `python studio/build.py --dx11 --compiler C:/tools/mingw64/bin/g++.exe --exe MoleculeStudio.exe`。
输出位于 `out/Studio/`。构建编辑器不需要 API Key，也不下载 OCR 权重。
运行时依赖可从同版本三个发布包提取。`chemistry_backend.ini` 使用相对路径 `Python=python\python.exe`，不要提交个人配置。
源码布局：`studio/` 为产品界面和后端；`studio/engine/` 为引擎覆盖实现；`engine/` 为基础引擎及第三方源码。构建脚本按清单选择链接文件。

## 开源与安全
自有代码沿用 Apache-2.0；第三方组件、运行时、字体、模型保留原许可证。参见 LICENSE、NOTICE、licenses/、PROVENANCE.md 和 docs/RELEASE.md。
历史来源说明保留，不作独立净室实现认证。不要把整个已使用的绿色版目录上传到 GitHub。
安全反馈请按 SECURITY.md 操作。贡献说明见 CONTRIBUTING.md。

## 外部 AI 绘图 skill
不使用内置 AI 时，可使用 [moleculestudio-draw](skills/moleculestudio-draw/SKILL.md) 生成可编辑 CDXML。基础文件绘图只需 Core；参见 [安装与使用说明](docs/EXTERNAL_AI_SKILL.md)。
