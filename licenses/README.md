# 许可证与来源清单

本目录是 MoleculeStudio 0.9.4.2 绿色版的第三方许可证清单，2026-11-01 整理。

## 结构

| 路径 | 内容 |
| --- | --- |
| index.json | 712 项组件总索引：名称、版本、SPDX、打包位置、许可证原文路径与 SHA-256 |
| common/ | GPL / LGPL / MPL / MIT 等通用许可证全文 |
| jar/ | OPSIN、JNA-InChI 等 JAR 内组件 |
| java/ | Eclipse Temurin JRE 21 各模块 |
| native/ | CPython、libvips、ffmpeg、bzip2、XZ 等原生库 |
| npm/ | DeepSeek Harness 及 sharp / LibreOffice Kit 的 npm 依赖（560 项） |
| python/ | 随包 Python 3.12 环境中的组件 |
| notices/ | OCSR 与开放化学组件的补充通知 |
| resolution-20261030.json | 39 项历史许可证缺项的补齐记录 |

## 使用

- 查找某个组件：在 index.json 中搜索名称或 bundled_path，按 license_text 打开原文。
- 人读清单：包根目录 THIRD_PARTY_NOTICES.md 按许可证分组列出全部组件及原文链接。
- 依赖分层与义务摘要：包根目录 DEPENDENCIES.md。
- 对应源码：SOURCES.txt 与 SOURCE_AVAILABILITY.json（第三方源码另包下载）。

## 校验

```
python\python.exe tools\verify_release_materials.py
```

校验每一项 license_text 是否存在且 SHA-256 与索引一致、占位符是否清零，
以及 SOURCES.txt / NOTICE / THIRD_PARTY_NOTICES.md 是否含未填占位。

本清单不改变上游许可证条款；第三方代码保留其各自许可证。
