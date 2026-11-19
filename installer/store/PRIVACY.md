# MoleculeStudio 隐私声明 / Privacy Policy

生效日期：2026-11-20　·　适用于 MoleculeStudio 桌面版（含 Microsoft Store 版本）

> 托管后的公开地址必须填进 Partner Center「属性 → 隐私政策 URL」。
> 可直接使用本目录的 [privacy.html](#) 部署到 GitHub Pages，或把下面内容贴到任意公开页面。

---

## 中文

### 一句话概括

MoleculeStudio 默认完全离线工作：不创建账号、不收集遥测、不向开发者回传任何数据。只有当你主动启用"在线 AI"或"在线检索"功能时，程序才会把你指定的内容发送到你自己配置的第三方服务。

### 本机保存的数据

程序在本机保存以下内容，用于记住你的设置与工作状态。Microsoft Store 版本保存在 %LOCALAPPDATA%\MoleculeStudio，绿色版/安装器版保存在程序目录：

- 界面语言、绘图与箭头偏好、快捷键、自定义分组（interface.ini、drawing.ini、shortcuts.ini、custom_groups.xml）
- 自定义模板文件（custom_templates\）
- 图片识别的缓存与识别记录（OCSRCache\、recognition_records\）
- 渲染状态与诊断日志（renderer-status.txt、molecule_studio_ole.log 等）

这些数据不会离开你的电脑。删除对应文件或卸载程序即可清除；商店版卸载后由 Windows 移除包数据。

### 你的文档

分子结构、反应机理与版面文件保存在你自己选择的位置。程序不会把它们上传到任何地方，除非你在在线 AI 对话里主动发送。

### 可选：在线 AI 功能

当你（且仅当你）在「AI 设置」里填入服务地址、模型名和 API Key 并主动发送消息时，程序会向该地址发送：

- 你的提示词与对话上下文
- 当前画布的结构内容（CDXML/结构摘要）
- 你在对话里附加的截图或文件

接收方是**你自己配置的第三方服务商**，不是开发者。服务商如何使用这些数据取决于它的隐私政策，请在启用前阅读。请求通过 HTTPS 传输。

你的 API Key 使用 Windows DPAPI（CryptProtectData）加密后保存在本机文件中，仅当前 Windows 用户可解密；勾选"记住"才会保存，取消勾选即删除。你也可以随时在设置里清除凭据，并到服务商处吊销密钥。

### 可选：在线检索

使用在线检索时，查询词会发送到你配置的检索服务。同样由你选择服务商。

### 我们不做的事

- 不收集使用统计、崩溃上报或设备标识
- 不创建 MoleculeStudio 账号，不读取你的其他文件
- 不在后台联网：除你主动触发在线功能或可选组件下载外，程序不发起网络请求

### 儿童

本程序面向科研与教学用户。在线 AI 输出不受开发者控制；未成年用户应在监护人指导下使用在线功能。

### 变更与联系

本声明随功能更新。重大变更会写入版本说明。隐私问题请通过项目支持页 <https://github.com/chaobada10086/MoleculeStudio/issues> 联系维护者；公开留言请勿附密钥、私人文档或完整对话记录。

---

## English

### Summary

MoleculeStudio works fully offline by default. It creates no accounts, collects no telemetry, and sends nothing back to the developer. Network traffic happens only when you enable an optional online AI or search feature and configure your own third-party service.

### Data stored on your device

The app stores the following locally, to remember your settings and work state. In the Microsoft Store version this is %LOCALAPPDATA%\MoleculeStudio; in the portable/installer version it is the program folder:

- Interface language, drawing and arrow preferences, shortcuts, custom groups (interface.ini, drawing.ini, shortcuts.ini, custom_groups.xml)
- Custom templates (custom_templates\)
- Image-recognition cache and records (OCSRCache\, recognition_records\)
- Renderer status and diagnostic logs (renderer-status.txt, molecule_studio_ole.log, etc.)

None of it leaves your computer. Delete the files or uninstall the app to remove it; the Store version's package data is removed by Windows on uninstall.

### Your documents

Structure, mechanism and layout files are saved wherever you choose. They are never uploaded unless you explicitly send them in an online AI conversation.

### Optional online AI

Only when you enter a service endpoint, model name and API key in AI settings and send a message does the app transmit:

- your prompts and conversation context;
- the structure content of the current canvas (CDXML / structure summary);
- screenshots or files you attach.

The recipient is **the third-party provider you configured**, not the developer. What that provider does with the data is governed by its own privacy policy; please read it before enabling. Requests are sent over HTTPS.

Your API key is encrypted with Windows DPAPI (CryptProtectData) and stored in a local file that only your Windows user can decrypt; it is saved only if you tick "remember", and deleted when you untick it. You can clear the credential at any time and revoke the key at the provider.

### Optional online search

Search queries are sent to the search service you configure. The provider is your choice.

### What we do not do

- No usage analytics, crash reporting or device identifiers.
- No MoleculeStudio account, no access to your other files.
- No background networking: apart from features you trigger or optional component downloads, the app makes no network requests.

### Children

The app targets research and teaching users. Online AI output is not controlled by the developer; minors should use online features under supervision.

### Changes and contact

This policy is updated with the product; material changes are noted in release notes. Privacy questions: <https://github.com/chaobada10086/MoleculeStudio/issues>. Please do not post keys, private documents or full conversation logs publicly.
