# 在线发布清单

安装器从以下官方 HTTPS 地址读取发布信息：

https://raw.githubusercontent.com/chaobada10086/MoleculeStudio/main/updates/stable.json

文件需要提交并推送到 main 后才会对用户生效。清单不可用时，安装器明确显示使用内置发布版本、未确认最新；不会把旧版说成最新版。包下载仍然镜像优先，版本清单只读取官方地址，防止第三方镜像自行替换校验值。

## 发布步骤

1. 在 GitHub Releases 上传完三个经过测试的 ZIP。新版本使用新的标签和文件名，不覆盖原 ZIP。
2. 确认 Core、AI、OCSR 的版本组合兼容。各 ZIP 保持 `MoleculeStudio/` 根目录。
3. 运行生成脚本，例如当前版本：

```powershell
python tools/generate_update_manifest.py --tag v0.9.5.0.0 --core-asset MoleculeStudio-0.95-issue2-hotfix-Core-win64.zip --core-version 0.95-issue2-hotfix --ai-asset MoleculeStudio-0.95-AI-addon-win64.zip --ai-version 0.95 --ocsr-asset MoleculeStudio-0.95-OCSR-addon-win64-hotfix2.zip --ocsr-version 0.95-hotfix2
```

4. 检查 `updates/stable.json` 的版本与链接，提交并推送。安装器下次启动获取这份清单，无需为正常软件版本更新重新编译安装器。

脚本从 GitHub API 获取已发布附件的大小、URL 和 SHA-256；拒绝草稿、预发布、缺失校验值及上传未完成的附件。要求明确传入三个附件名，防止选错历史包。模组的 `requires_core_version` 固定指向本清单的核心版本；维护者必须先测试这套组合，脚本不会猜测二进制兼容性。

## 协议和更新

`schema=1` / `min_installer_protocol=1`。未来若需要改变包格式或安装规则，提高协议要求并另发新安装器。正常版本、文件名、URL、哈希变化不需要重编译。

安装器根据组件哈希识别可更新状态。更新仅替换清单管理且未被用户修改的旧文件；发现修改冲突则停止并回滚本批次。更新前记录备份，下载、解压或核心自检失败会恢复，异常退出后下次自动恢复。用户文档和未知文件不在替换范围内。保持上一版 Release 供排查和恢复使用。

当前仓库内清单已根据实际线上发布附件生成。只有推送清单后，线上地址才存在；本地创建清单不等于已经发布。
