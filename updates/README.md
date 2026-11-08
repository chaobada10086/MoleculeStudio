# 在线发布清单

安装器从以下官方 HTTPS 地址读取发布信息：

https://raw.githubusercontent.com/chaobada10086/MoleculeStudio/main/updates/stable.json

文件需要提交并推送到 main 后才会对用户生效。清单不可用时，安装器明确显示使用内置发布版本、未确认最新；不会把旧版说成最新版。包下载仍然镜像优先，版本清单只读取官方地址，防止第三方镜像自行替换校验值。

## 四组件与依赖

发布清单使用 `schema=2` / `min_installer_protocol=2`，包含四个组件：

- `Core`：核心工作台，必备。
- `AIBasic`：AI 基础（AI 助手、检索、对话画图），依赖 Core。
- `AIAdvanced`：AI 高级（仅 ai_plugins 高级绘图插件），依赖 AIBasic；安装器会强制先安装 AI 基础。
- `OCSR`：图片识别，依赖 Core。

每个组件行包含 `id`、`version`、`name`、`url`、`sha256`、`bytes`、`requires`、`requires_core_version`。安装器会拒绝：未知/重复组件、缺失依赖、依赖环、`requires_core_version` 与核心版本不一致、非官方 GitHub Release URL、非法文件名或校验值、协议版本高于安装器自身。旧的三组件 `schema=1` 清单仍可读取（AI 视为 AIBasic，AIAdvanced 标记为暂不可用）。

## 发布步骤

1. 在 GitHub Releases 上传四个经过测试的 ZIP（不要覆盖旧 ZIP，使用新标签）：
   - Core：`MoleculeStudio-<版本>-Core-win64.zip`
   - AI 基础：`MoleculeStudio-<版本>-AIBasic-addon-win64.zip`
   - AI 高级：`MoleculeStudio-<版本>-AIAdvanced-addon-win64.zip`
   - 图片识别：`MoleculeStudio-<版本>-OCSR-addon-win64.zip`
2. 确认四个包版本组合兼容；每个 ZIP 都保持单个 `MoleculeStudio/` 根目录。AI 高级包只允许包含 `MoleculeStudio/ai_plugins/**`；AI 基础包不得包含 `ai_plugins`。
3. 运行生成脚本（联网读取已发布附件的大小与 SHA-256；离线自检可用 `--local-dir`）：

```powershell
python tools/generate_update_manifest.py --tag v0.9.6.0.0 --core-asset MoleculeStudio-0.95-20261108-Core-win64.zip --core-version 0.95-20261108 --aibasic-asset MoleculeStudio-0.95-20261108-AIBasic-addon-win64.zip --aibasic-version 0.95-20261108 --aiadvanced-asset MoleculeStudio-0.95-20261108-AIAdvanced-addon-win64.zip --aiadvanced-version 0.95-20261108 --ocsr-asset MoleculeStudio-0.95-20261108-OCSR-addon-win64.zip --ocsr-version 0.95-20261108
```

4. 检查 `updates/stable.json` 的版本、依赖与链接，提交并推送。安装器下次启动获取这份清单；正常包版本/文件名/URL/哈希变化不需要重新编译安装器。

脚本从 GitHub API 获取已发布附件的大小、URL 和 SHA-256；拒绝草稿、预发布、缺失校验值及上传未完成的附件。要求明确传入四个附件名，防止选错历史包。模组的 `requires_core_version` 固定指向本清单的核心版本；维护者必须先测试这套组合，脚本不会猜测二进制兼容性。

## 协议和更新

`schema=2` / `min_installer_protocol=2`。需要改变包格式或安装规则时提高协议要求并另发新安装器；旧安装器会提示更新而不会误装。正常版本、文件名、URL、哈希变化不需要重编译。

安装器根据组件哈希识别可更新状态。更新仅替换清单管理且未被用户修改的旧文件；发现修改冲突则停止并回滚本批次。更新前记录备份，下载、解压或核心自检失败会恢复，异常退出后下次自动恢复。用户文档和未知文件不在替换范围内。保持上一版 Release 供排查和恢复使用。

当前仓库内清单已根据实际线上发布附件生成。只有推送清单后，线上地址才存在；本地创建清单不等于已经发布。
