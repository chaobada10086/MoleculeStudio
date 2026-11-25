# MoleculeStudio — 认证说明与受限能力理由

这份文件有两个用途，内容可直接粘贴到 Partner Center：

1. **提交选项 → 受限能力**：填写 `runFullTrust` 的用途
2. **提交选项 → 认证说明（Notes for certification）**：审查员按此验证，含 OLE 测试矩阵

英文在前，便于审查；中文在后。发布包版本与清单里的能力声明必须与本文件一致。

---

## 1. Restricted capabilities — justification (paste as-is)

### runFullTrust

> MoleculeStudio is a native Win32 C++ molecular drawing application (no UWP/XAML UI). It
> renders the canvas with Direct3D 11 / GDI+, reads and writes ChemDraw CDXML documents,
> and launches bundled helper processes: a Python 3.10 runtime for local chemistry
> calculations and a bundled local optical-structure-recognition model for image to
> structure conversion. None of this can run inside an AppContainer: it needs normal
> desktop file access, direct GPU access (Direct3D 11), and the ability to start its own
> local helper executables. No elevation is required and the app never requests
> administrator rights. Declaring `runFullTrust` is the documented way to package such a
> desktop app; the app runs at medium integrity level (uap10:TrustLevel="mediumIL").

### 中文对照（同上）

- **runFullTrust**：原生 Win32 分子绘图程序，Direct3D 11 渲染、读写 CDXML、需要启动随包的
  Python 3.10 与本地识别模型进程。不请求管理员权限，以 mediumIL 运行。
---

## 2. Notes for certification (paste as-is)

### English

```
MoleculeStudio — notes for certification

WHAT TO TEST (no account, no key, no network needed)
1. Launch the app. Draw a short carbon chain with the single-bond tool, then use the
   benzene/template panel once.
2. Save as CDXML, close the tab, reopen the saved file. The structure must round-trip.
3. Draw a reaction arrow and a curved arrow (Graphics > Curves), drag a control point.
4. Open Settings and switch the interface language between Chinese/English, then restart.
   Settings must persist (they are stored under %LOCALAPPDATA%\MoleculeStudio, not next to
   the EXE, because the package directory is read-only).
5. Uninstall the package and reinstall: it must start cleanly with default settings.

AI FEATURES (optional, not needed for certification)
The online AI assistant requires the tester to supply their own third-party API endpoint,
model name and API key; there is no shared demo key because the provider bills per call.
The app is fully usable without them and never contacts the network unless the user
configures a provider and presses send. The bundled local image-to-structure recognition
(OCSR) needs no network and no key: import a PNG/PDF of a structure to test it.

OLE EMBEDDING
Windows registers the packaged OLE server through the windows.comServer manifest extension.
Test matrix in Microsoft Word:
  T1 copy a structure in MoleculeStudio, paste-special into Word as an object, double-click
     to edit, save both files                     -> expected: editable, round-trips
  T2 close and reopen the Word document, double-click the object again -> server re-launched
  T3 Insert > Object > Create New: "Molecule Studio 2D Structure" should be listed
  T4 Insert > Object > Create from File, choose a .cdxml                       -> embeds
  T5 same as T1 in PowerPoint
  T6 repeat T1 after uninstalling and reinstalling the package (class id is stable)
  T7 install Core only (no optional add-ons)                                   -> T1 still works
PRIVACY / NETWORK
No telemetry. The app contacts the network only when the user enables an online AI provider
or downloads an optional component. The privacy policy URL is provided in the listing.

FULL TRUST JUSTIFICATION
Native Win32 app: Direct3D 11 rendering, CDXML document I/O, bundled Python 3.10 runtime and
a bundled local OCSR model, launched as local helper processes. No elevation, no services,
no drivers, no HKLM writes.
```

### 中文

```
MoleculeStudio 认证说明

建议验证路径（不需要账号、密钥或联网）
1. 启动程序，用单键工具画一段碳链，再从苯环/模板面板插入一次模板。
2. 另存为 CDXML，关闭标签页，再打开刚保存的文件，结构应完整往返。
3. 画反应箭头；在「图形 → 曲线」画一条曲线并拖动控制点。
4. 打开设置切换界面语言（中/英）后重启，设置应保留。设置写在
   %LOCALAPPDATA%\MoleculeStudio，因为包目录只读，不能写在 exe 旁边。
5. 卸载后重装，应以默认设置正常启动。

AI 功能（可选，认证不需要）
在线 AI 助手要求测试者自备第三方服务地址、模型名和 API Key；没有共享演示密钥，因为
服务商按调用计费。不配置时程序完全可以离线使用，且不会主动联网。随包的本地图片识别
（OCSR）不需要联网也不需要密钥，导入结构图片或 PDF 即可。

OLE 嵌入
Windows 按清单登记 OLE 类。Word 和 PowerPoint 的复测步骤见英文部分。

隐私与联网
无遥测。仅当用户启用在线 AI 服务商或下载可选组件时联网。隐私政策链接见商店列表。

完全信任理由
原生 Win32：Direct3D 11 渲染、CDXML 读写、随包 Python 3.10 与本地识别模型以本地子进程方式
启动。不请求提权、不装服务、不装驱动、不写 HKLM。
```

---

## 3. 提交时逐项对照

| Partner Center 位置 | 本仓库对应材料 |
| --- | --- |
| 提交选项 → 受限能力 | 本文件第 1 节，两段理由各贴一次 |
| 提交选项 → 认证说明 | 本文件第 2 节英文（推荐）或中文 |
| 属性 → 隐私政策 URL | `installer/store/PRIVACY.zh-CN.md` 托管后的公开地址 |
| 属性 → 产品声明 | 勾选「此产品包含生成式 AI 功能」 |
| 商店列表 → 描述/简介/截图 | `installer/store/LISTING.zh-CN.md`、`LISTING.en-US.md` |
| 年龄分级 | IARC 问卷，AI 聊天与第三方传输对应 12+ 及以上 |
| 程序包 | `tools/package_store.py` 生成的 `.msix` |

## 4. 已知差异（必须主动写进认证说明，不要等审查发现）

1. 商店版**没有**手动更新下载器：商店版由 Store 更新，政策 §10.2.2 禁止运行时下载并执行代码。
2. 商店版设置保存在 `%LOCALAPPDATA%\MoleculeStudio`，与绿色版/安装器版不共享，首次安装为默认设置。
3. 商店版不注册机器级文件关联以外的系统集成；文件关联由包清单声明，用户在「默认应用」里选择。
4. 在仅安装商店版的环境验证 Office 嵌入与重新激活。
