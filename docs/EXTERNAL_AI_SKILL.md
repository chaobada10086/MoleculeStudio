# 用外部 AI 绘图

源码附带 `skills/moleculestudio-draw/`。它可让具有本地文件与命令执行能力的 AI 客户端，调用 Core 化学引擎生成可编辑的 CDXML；不要求使用软件内置 AI，也不要求安装 AI addon 或配置 MoleculeStudio 的 API Key。

## 安装

下载源码，找到 skills/moleculestudio-draw 整个文件夹。将整个文件夹放入客户端支持的 skills 目录，保留 SKILL.md、scripts 和 references 的相对位置。当前 Codex 用户目录通常是 `~/.codex/skills/`；设置了 CODEX_HOME 时使用其 skills 子目录。其他客户端按各自 skill 导入方式安装；只有网页聊天、不能运行本地命令的客户端不能自动在电脑上绘图。

打开一个能发现该 skill 的新会话，告诉 AI 软件安装路径，例如：

> 使用 $moleculestudio-draw。软件在 D:/MoleculeStudio。请把我提供的 SMILES CCO 和 CC=O 画成左右排列的结构，中间加箭头并写上 [O]，保存到桌面的 reaction.cdxml。

随后在 MoleculeStudio 中打开输出文件。文件中的分子、标签和箭头可继续编辑。

## 范围

随附脚本支持结构、普通文字和单向反应箭头。没有公开的实时画布 HTTP/MCP 服务；它不会直接修改当前打开的文档。完整内部绘图函数的参数与调用说明也放在 skill 中，方便开发者另接宿主适配器，但不能把暂存成功理解为已在画布完成。

该 skill 可随源码单独下载，无需把它变为第四个运行时 Release 包。首次分发也可单独分享 skill ZIP。已有 Core、AI、OCSR ZIP 不需要重新上传。
