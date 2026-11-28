# docs 目录索引

本目录只放当前仍然有效、需要查阅的文档。历史交接、旧版发布说明和阶段性审计已移入 [archive/](archive/)，不要再从 archive 里取结论，以根目录 README 与当前源码为准。

## 当前有效

| 文件 | 用途 |
| --- | --- |
| [RELEASE.md](RELEASE.md) | 发版流程、包组成与在线清单（stable.json）规则 |
| [STORE_PUBLISH.md](STORE_PUBLISH.md) | 发布到 Microsoft Store 的完整流程：账号、打包、政策对照、提交清单、过审风险 |
| [privacy.html](privacy.html) | 中英隐私声明页面，可直接部署（商店必填） |
| [MSIX_SIGNING.md](MSIX_SIGNING.md) | MSIX 本地签名失败的排查记录与测试替代办法 |
| [CODE_SIGNING.md](CODE_SIGNING.md) | 代码签名现状、自签名与商业证书选型 |
| [EXTERNAL_AI_SKILL.md](EXTERNAL_AI_SKILL.md) | 外部 AI 绘图 skill 的安装与使用 |
| [AI_COMPACT.md](AI_COMPACT.md) | 给 AI 协作用的精简上下文 |
| [MECHANISM_ROUTING.md](MECHANISM_ROUTING.md) | 机理绘制路由设计 |
| [MECHANISM_NEXT_STEP.md](MECHANISM_NEXT_STEP.md) | 机理功能的下一步计划 |
| [FUNCTIONAL_GROUPS.md](FUNCTIONAL_GROUPS.md) | 官能团/缩写词库（PROVENANCE 引用） |
| [COMMON_COMPOUNDS.md](COMMON_COMPOUNDS.md) | 常用化合物目录（构建脚本引用） |
| [DEPENDENCY_INVENTORY-*.json](DEPENDENCY_INVENTORY-Core.json) | 三个组件包的依赖清单 |

## 归档

[archive/](archive/) 保存 2026-11 的20261108 版发布说明与正文、依赖/OCSR/性能审计、以及已被后续版本取代的修复记录。归档文件里的路径与结论可能已经过期。

## 仓库约定

- `work/` 是本机临时工作区（测试产物、一次性补丁脚本、r*-backup 快照、构建日志），已在 `.gitignore` 中排除。可重建的中间文件可定期清理。
- 发布产物的源码在 `studio/` 与 `engine/`；`out/`、`dist/`、`licenses/`（第三方许可文本除外）都不入库。