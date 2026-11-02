# 首次发布操作
1. 将本源码目录作为新 Git 仓库根目录，不要上传工作区、安装目录或旧 Git 历史。
2. 安装 Git 后执行 `git init`、`git add .`，先检查 `git status` 和 `git diff --cached`，再提交。
3. 创建空 GitHub 仓库并按 GitHub 提示连接 remote、推送；推送前检查敏感信息扫描报告。
4. 使用版本标签 v0.95 创建 Release，仅上传 Core、AI-addon、OCSR-addon 三个 ZIP。
5. 把交付目录的 RELEASE_BODY.md 内容粘贴进 Release 描述；校验值写在描述里，不必增加第四个附件。
GitHub 自动生成的 Source code (zip/tar.gz) 属于平台源码下载，不是额外手工上传的功能包。

.gitignore 只防止新文件误加入 Git，不会移除已跟踪文件，也不会清理已有历史。此交付不含旧 .git。
不自动发布，不创建远程仓库，不上传私人工作目录。

许可证范围：项目 Apache-2.0 不覆盖所有第三方二进制。各包 legal/ 保留许可与原始来源记录；新版 SOURCE_FILES.json 是实际随附源码清单。旧文件中单独 ThirdPartySources.zip 的说明属于历史记录，以本次包内源码为准。
原始来源记录是证据，并非当前版本已获法律保证。再发布新增组件需重新核查。

## 打包维护
审核后的运行目录分为 staging/Core/MoleculeStudio、staging/AI/MoleculeStudio、staging/OCSR/MoleculeStudio。不要把已使用的安装目录直接作为 staging。
运行 `python tools/package_release.py --staging ../staging --output ../releases`。脚本扫描敏感/运行文件、计算每个文件的 SHA-256、生成三个 ZIP 并验证 ZIP CRC。
校验清单输出到 releases 的上一级，Release 附件目录保持三个文件。源码仓库不提交 staging、模型或运行时。
