# 首次发布操作
1. 将本源码目录作为新 Git 仓库根目录，不要上传工作区、安装目录或旧 Git 历史。
2. 安装 Git 后执行 `git init`、`git add .`，先检查 `git status` 和 `git diff --cached`，再提交。
3. 创建空 GitHub 仓库并按 GitHub 提示连接 remote、推送；推送前检查敏感信息扫描报告。
4. 使用版本标签（如 v0.9.6.0.0）创建 Release，上传四个 ZIP：Core、AI 基础（AIBasic）、AI 高级（AIAdvanced）、图片识别（OCSR）。
5. 代码签名（如需消除“未知发布者”）：按 [CODE_SIGNING.md](CODE_SIGNING.md) 使用 `tools/sign_release.ps1` 对 `MoleculeStudio.exe` 与 `MoleculeStudio-OnlineSetup.exe` 签名并确认 `Valid`，再上传。自签名只在本机受信任；公开发行请使用受信任 CA 或 SignPath/Certum 开源证书。
6. 把交付目录的 RELEASE_BODY.md 内容粘贴进 Release 描述；校验值写在描述里，不必增加额外附件。
GitHub 自动生成的 Source code (zip/tar.gz) 属于平台源码下载，不是额外手工上传的功能包。

.gitignore 只防止新文件误加入 Git，不会移除已跟踪文件，也不会清理已有历史。此交付不含旧 .git。
不自动发布，不创建远程仓库，不上传私人工作目录。

许可证范围：项目 Apache-2.0 不覆盖所有第三方二进制。各包 legal/ 保留许可与原始来源记录；新版 SOURCE_FILES.json 是实际随附源码清单。旧文件中单独 ThirdPartySources.zip 的说明属于历史记录，以本次包内源码为准。
原始来源记录是证据，并非当前版本已获法律保证。再发布新增组件需重新核查。

## 四组件包

| 组件 | 目录 | 内容 | 依赖 |
| --- | --- | --- | --- |
| Core | `staging/Core/MoleculeStudio/` | 主程序、基础绘图、化学/核磁/三维运行时、内置 AI 绘图 Skill、手动更新下载器 | 无 |
| AIBasic | `staging/AIBasic/MoleculeStudio/` | AI 助手、检索、对话画图；除 `ai_plugins/` 外的全部 AI 文件 | Core |
| AIAdvanced | `staging/AIAdvanced/MoleculeStudio/` | 仅 `ai_plugins/**` 高级绘图插件 | AIBasic |
| OCSR | `staging/OCSR/MoleculeStudio/` | 图片/PDF 转结构、本地模型与运行时 | Core |

规则：每个 ZIP 只保留一个 `MoleculeStudio/` 根目录；四包版本字符串一致；AI 高级包只允许 `MoleculeStudio/ai_plugins/**`；AI 基础包不得包含 `ai_plugins`（由 `tools/split_ai_package.py` 从旧 AI 包拆分）；每个包内重新生成 `MANIFEST-<组件>.json`。

## 打包维护

审核后的运行目录：
```
staging/Core/MoleculeStudio/**
staging/AIBasic/MoleculeStudio/**
staging/AIAdvanced/MoleculeStudio/ai_plugins/**
staging/OCSR/MoleculeStudio/**
```
不要把已使用的安装目录直接作为 staging。运行：
```powershell
python tools/package_release.py --staging ../staging --output ../releases --version 0.95-20261108
```
脚本扫描敏感/运行文件、逐文件计算 SHA-256、生成四个 ZIP 并验证每个 ZIP 的 CRC，输出四包校验清单到 releases 的上一级。源码仓库不提交 staging、模型或运行时。

从旧三组件 AI 包拆分：
```powershell
python tools/split_ai_package.py --source MoleculeStudio-0.95-20261107-canvas-fix-AI-addon-win64.zip --output ../releases --version 0.95-20261108 --revision 20261108
```

## 在线安装器更新发布

正常发版按 [在线版本清单说明](../updates/README.md) 执行：先上传四个完整软件包，再运行 `tools/generate_update_manifest.py` 生成 `schema=2` 清单，检查并推送 `updates/stable.json`。在线安装器读取该清单。首次启用四组件或协议升级需另行上传新版 `MoleculeStudio-OnlineSetup.exe`；此后正常包版本变化无需重新编译安装器。

## 低分辨率屏幕

安装器窗口按工作区自适应，内容可滚动、底部操作栏固定；发布前用
`MoleculeStudio-OnlineSetup.exe --preview out\preview-1024x600.png 1024x600 select`
与 `--small-screen-test` 检查 1024×600 及 125%/150% 缩放布局。
