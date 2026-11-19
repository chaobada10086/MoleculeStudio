# 发布到 Microsoft Store

本文件是从"决定要上商店"到"过审上架"的完整操作手册，配套材料在 [installer/store/](../installer/store/)。
政策与平台结论均按 2026-11 的微软官方文档核对，链接见文末。

---

## 0. 结论先行：走哪条路

微软商店对桌面程序接受两种包（[应用包要求](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements)）：

| | **A. MSIX（推荐）** | **B. EXE/MSI（政策 10.2.9）** |
| --- | --- | --- |
| 分发 | 商店托管下载（1.2GB 包免自建 CDN） | 自己托管，提交一个固定的 HTTPS 下载地址 |
| 签名 | **微软重签名**，不用买证书 | 必须用 CA 信任链证书签安装器与内部 PE |
| 更新 | Store 原子更新，可回滚 | 自己维护更新器与版本化 URL，商店不管 |
| 改造量 | 需要只读安装目录适配（已完成） | 保持现有安装器形态，但需把在线安装器改成静默离线安装 |
| 审核 | 有内容/技术合规审查 | 同类审查 + 安装器静默安装验证 |

**结论：走 MSIX。** 现有发布物是四个 ZIP，正好可以直接重组成一个 MSIX；\`tools/package_store.py\` 已经把这步自动化。
EXE/MSI 路线留作备选：如果 MSIX 在受限能力上卡住太久，可以先用 EXE/MSI 拿到商店条目，再切回 MSIX（产品名与提交可复用）。

---

## 1. 开发者账号

- 账号类型只有 **Individual / Company** 两种，**注册费都已取消（$0）**；老资料里的 $19/$99 已过时。
- **Individual**：个人身份发布，验证方式是政府证件 + 自拍，不需要信用卡。
  入口必须是 <https://storedeveloper.microsoft.com>；从 Partner Center 直连、Visual Studio 或 Xbox 入口进去走的是旧的收费流程。
- **Company**：需要 DUNS 编号或营业执照/注册证明 + 企业域名邮箱，并且有强制尽职调查，人工审核约 3–5 个工作日。
- 政策 §10.14：如果发布属于"经营行为"，或消费者会把发布者名理解成公司，就必须用 Company 账号。个人开源项目用 Individual 即可。
- **国家/地区在注册时选定后不可更改**，与税务、结算绑定。
- **中国大陆能否注册 Individual：微软文档没有列举可注册地区**（只说"近 200 个市场"），这一点属于未验证项。实际确认只有两个办法：
  1. 打开 <https://storedeveloper.microsoft.com> 看国家/地区下拉里有没有中国大陆；
  2. 开一张 Partner Center 支持工单。
  如果个人身份被拒，备选是 Company 账号（国内主体 + DUNS），或香港等地区主体（注意地区不可改）。不要先假设某条路可行再动手做包。

---

## 2. 时间与成本

| 阶段 | 需要时间 | 成本 |
| --- | --- | --- |
| 注册账号 + 身份验证 | 当天～3 天 | $0 |
| 预留产品名（保留 3 个月，过期释放） | 10 分钟 | $0 |
| 生成 MSIX + 自测 | 半天 | $0 |
| 准备商店文案/截图/隐私政策页 | 1–2 天 | $0 |
| 提交 + 认证 | 官方口径最长 3 个工作日；声明受限能力会更久 | $0 |
| 商店托管下载 | — | $0（1.2GB 包也不额外收费） |

对比：EXE/MSI 路线需要 OV/EV 代码签名证书（约 $200–400/年，Certum OSS 约 €30–40/年），见 [CODE_SIGNING.md](CODE_SIGNING.md)。

---

## 3. 需要准备写什么（材料清单）

| # | 材料 | 来源 | 本仓库现成文件 |
| --- | --- | --- | --- |
| 1 | 包名、Publisher、PublisherDisplayName | Partner Center「产品标识」页复制 | 传给 package_store.py 三个参数 |
| 2 | MSIX 包 | package_store.py 生成 | — |
| 3 | 商店描述 / 简短描述 / 功能 / 关键词 | 本仓库 | [LISTING.zh-CN.md](../installer/store/LISTING.zh-CN.md)、[LISTING.en-US.md](../installer/store/LISTING.en-US.md) |
| 4 | 隐私政策（公开 HTTPS 地址） | 本仓库 | [PRIVACY.md](../installer/store/PRIVACY.md)、[privacy.html](privacy.html)（可挂 GitHub Pages） |
| 5 | 截图 ≥1 张（建议 4–8 张，桌面 ≥1366×768，PNG ≤50MB） | 需实拍 | 拍摄清单见 LISTING 文件末尾 |
| 6 | 年龄分级（IARC 问卷全篇必答） | 本仓库给出建议答案 | LISTING 文件「年龄分级」一节 |
| 7 | 受限能力理由（runFullTrust、unvirtualizedResources） | 本仓库 | [CERTIFICATION-NOTES.md](../installer/store/CERTIFICATION-NOTES.md) 第 1 节 |
| 8 | 认证说明（测试步骤、AI 处理方式、OLE 矩阵） | 本仓库 | 同上第 2 节 |
| 9 | 产品声明：勾选「包含生成式 AI 功能」 | Partner Center 勾选 | 见第 6 节 |
| 10 | 支持/联系信息、系统要求 | 本仓库 | LISTING 文件 |

---

## 4. 打包（可复现命令）

前提：Windows SDK 的 makeappx.exe（或 MSIX Packaging Tool）、Python 3.10+ 与 Pillow、同版本四个发布 ZIP。

```powershell
python tools/package_store.py ^
  --release "D:/Release" ^
  --output  "D:/Store/MoleculeStudio-0.9.7.0-x64.msix" ^
  --name "从产品标识页复制的 Package/Identity/Name" ^
  --publisher "CN=..., O=..., C=CN" ^
  --publisher-display-name "发布者显示名" ^
  --version "0.9.7.0.0"
```

脚本依次做：

1. 按组件名找四个 ZIP，逐个比对 MANIFEST-<组件>.json 里的 SHA-256（校验失败直接退出，不会带着坏包继续）。
2. 只把清单里登记的文件放进 App\，并剔除：MoleculeStudio-OnlineSetup.exe（自更新器，商店政策禁止）、
   interface.ini / drawing.ini / shortcuts.ini / custom_groups.xml、custom_templates\、OCSRCache\、
   recognition_records\、ChemistryJobs\、__pycache__、MANIFEST-*.json。
3. 把 chemistry_backend.ini 里的绝对 Python 路径改写成相对 python\python.exe（构建机路径在用户机器上不存在）。
4. 从 studio/assets/app-icons/moleculestudio-professional.png 生成 25 张商店图标（含 scale-125/150/200/400 与 targetsize）。
5. 用 [installer/store/AppxManifest.xml](../installer/store/AppxManifest.xml) 渲染清单（身份三个值 + 版本）。
6. 调 makeappx pack /h SHA256 出包，并打印包大小与 SHA-256。

本地侧载自测（可选）：加 --sign-pfx <证书.pfx> --sign-password <密码> 用自签名证书签一次，装完测完再卸载。
商店提交**不需要**自己签名，商店会重签并替换掉你的签名。

```powershell
Add-AppxPackage -Path "D:/Store/MoleculeStudio-0.9.7.0-x64.msix"
Get-ChildItem "$env:LOCALAPPDATA\MoleculeStudio"   # 设置应落在这里，而不是安装目录
```

---

## 5. 技术阻塞与已完成的改造

商店版安装目录是 C:\Program Files\WindowsApps\<包>\App，**运行时只读**，与绿色版"所有东西摆在 exe 旁边"的假设直接冲突。逐项现状：

| # | 问题 | 影响 | 处理 | 状态 |
| --- | --- | --- | --- | --- |
| 1 | 设置/缓存/日志写在 exe 目录 | 全部静默失败，设置不保存 | 新增 studio/app_paths.hpp 统一 dataDir()；打包态写 %LOCALAPPDATA%\MoleculeStudio，未打包态不变 | 已完成，编译通过 |
| 2 | Python 子进程往安装目录写缓存/识别记录 | OCSR 缓存与记录丢失 | C++ 传 MOLECULE_STUDIO_DATA_DIR，ai_ocsr_bridge.py 跟随 | 已完成 |
| 3 | Python 写 __pycache__ | 无害（写失败即不缓存，启动略慢） | 打包不带 __pycache__；可选 PYTHONDONTWRITEBYTECODE=1 | 可接受 |
| 4 | 手动更新下载器 | 政策 §10.2.2 禁止运行时下载并执行代码 | 打包态按钮只提示"商店版本由 Store 更新"，launchUpdater() 直接返回 | 已完成 |
| 5 | chemistry_backend.ini 绝对路径 | 用户机器上没有该 Python | 打包时改写为相对路径；exe 旁优先、dataDir 兜底 | 已完成 |
| 6 | OLE 自注册写 HKCU\Software\Classes | 注册表被虚拟化，Word 看不到 | 方案 A+B：清单加 windows.comServer；同时加 unvirtualizedResources 豁免三个键 | 清单已写，待真机验证 |
| 7 | 文件关联靠运行写注册表 | 资源管理器不生效 | 改为清单 uap:FileTypeAssociation（.cdxml/.r62doc/.t01doc） | 已完成 |
| 8 | OLE 日志路径 | 写安装目录失败 | 走 dataDir() | 已完成 |
| 9 | ai_dsh.py 的 DSH_HOME 指向 AI 核心目录 | 可能存在只读写入 | 未处理，属可选组件；建议后续加环境变量覆盖 | 待办 |
| 10 | ocsr_client.py 的 recognition_records | 外部辅助脚本仍写安装目录 | 未处理，属外部工具 | 待办 |

OLE 的完整说明、测试矩阵（T1–T7）与受限能力理由写在 [CERTIFICATION-NOTES.md](../installer/store/CERTIFICATION-NOTES.md)。

---

## 6. 政策对照（提交前逐条自查）

| 政策 | 要求 | 本项目做法 |
| --- | --- | --- |
| §11.16 生成式 AI | 元数据披露；提交时在 Partner Center 声明；输出合规；提供举报渠道 | 描述写明在线 AI 需自备密钥；勾选「包含生成式 AI 功能」；反馈入口指向 GitHub Issues |
| §10.5 个人信息 | Win32 产品**必须**有隐私政策；说明用途/对象类型/撤回手段；存储与传输用现代加密 | DPAPI（CryptProtectData）加密密钥；HTTPS 传输；隐私政策覆盖第三方发送与撤回 |
| §10.2.2 动态代码 | 不得下载并执行改变功能的代码 | 商店版移除自更新下载器；不加载远程脚本 |
| §10.2.4 依赖披露 | 依赖外部服务实现核心功能须在描述**开头**披露 | 描述第一句写 Online AI features require your own third-party API key |
| §10.3 可测性 | 需登录/服务器时必须提供可用测试账号 | 核心功能完全离线可测；AI 可选、无共享密钥（第三方计费），并在认证说明写清 |
| §11.11 年龄分级 | 必答 IARC 问卷；可能展示超龄内容需过滤或门禁 | 建议 12+；在线 AI 默认关闭，需显式启用 |
| §10.1.1 描述准确 | 必须准确描述功能与**重要限制** | 写明识别会出错，需核对键级/手性/电荷；需自备密钥 |
| §10.1.5 安装后获取内容 | 安装后取得的内容也须来自商店 | 不下载可执行内容；可选组件随包分发 |
| 提权 | 需要管理员权限的功能不被接受 | mediumIL 运行，asInvoker；不写 HKLM、不装服务/驱动 |

---

## 7. 提交清单（Partner Center 逐页）

1. **预留名称**：全局唯一，保留 3 个月；可同时预留多个。
2. **定价与可用性**：市场、受众、可发现性、计划、基础价格（免费）——都必填。
3. **属性**：类别（建议 Productivity 或 Education）、隐私政策 URL（必填）、支持联系信息、系统要求。
4. **产品声明**：勾选「此产品包含生成式 AI 功能」。
5. **年龄分级**：完成 IARC 问卷；发布者名称与邮箱会转给 IARC。
6. **程序包**：上传 .msix；提交时会显示声明的两个受限能力。
7. **商店列表**：描述（≤10,000 字符）、简短描述（前 270 字符最关键）、功能要点、截图（≥1 张，建议 4–8 张）、推荐 300×300 方形图标。
8. **提交选项**：受限能力贴 CERTIFICATION-NOTES 第 1 节两段理由；认证说明贴第 2 节；发布方式选手动发布。
9. **提交认证**：安全扫描 → 技术合规（Windows App Certification Kit）→ 内容合规 → 人工审核。

---

## 8. 过审风险与应对

| 风险 | 可能性 | 应对 |
| --- | --- | --- |
| unvirtualizedResources 被要求移除 | 中 | 退回方案 B（comServer 仍在），OLE 主流程可用；T3/T4 在认证说明里如实说明 |
| 认为"AI 需自备密钥"不可测 | 中 | 认证说明写明核心功能离线可测、AI 可选；截图避免以 AI 为主 |
| 1.2GB 包上传/扫描慢 | 低 | 官方上限 25GB/包，无额外流程；首次上传留足时间 |
| 描述未披露 AI 依赖 | 中 | 描述第一句即披露（§10.2.4） |
| OLE 在测试机上失效被当成功能缺陷 | 中 | 认证说明给出 T1–T7 预期结果与依赖关系 |
| 中国大陆账号注册受限 | 未知 | 先验证注册入口；必要时 Company 账号或他地区主体 |

---

## 9. 发布之后

- **更新**：改版本号（四段，第四段必须 0）→ 重新打包 → 提交新包。商店自动更新；程序内不做自更新。
- **版本号规则**：0.9.7.0.0（发布版号）→ MSIX 用 0.9.7.0。脚本会校验第四段为 0。
- **双通道**：商店版与绿色版/安装器版是两条渠道，设置不共享（商店版在 %LOCALAPPDATA%\MoleculeStudio）。发布说明里要写清用户该选哪条。
- **回滚**：商店支持停止分发与回滚；保留每个版本的 MSIX 与 SHA-256 便于对照。

---

## 10. 待办

1. 真机装 MSIX，跑 OLE 测试矩阵 T1–T7（决定 unvirtualizedResources 是否值得保留）。
2. 拍 4–8 张截图（清单见 LISTING 文件）。
3. 把 [privacy.html](privacy.html) 挂到 GitHub Pages，得到公开 HTTPS 地址。
4. 处理 ai_dsh.py 的 DSH_HOME 与 ocsr_client.py 的写路径（可选组件，影响面小）。
5. 确认中国大陆 Individual 注册是否可用。

---

## 参考

- [应用包要求](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements)
- [Microsoft Store 政策（含 §11.16 生成式 AI）](https://learn.microsoft.com/en-us/windows/apps/publish/store-policies)
- [开发者账号](https://learn.microsoft.com/en-us/windows/apps/publish/partner-center/open-a-developer-account)
- [MSIX 容器化与只读安装目录](https://learn.microsoft.com/en-us/windows/msix/msix-containerization-overview)
- [灵活虚拟化：按键豁免注册表写入](https://learn.microsoft.com/en-us/windows/msix/desktop/flexible-virtualization)
- [comServer 扩展](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-com-comserver)
- [桌面应用扩展（文件关联等）](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/desktop-to-uwp-extensions)
- [认证流程](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-certification-process)
