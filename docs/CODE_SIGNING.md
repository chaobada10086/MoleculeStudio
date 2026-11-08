# 代码签名（Code Signing）说明

当前发布包未做商业代码签名，因此 Windows SmartScreen 可能提示“未知发布者”，Windows 11 的 Smart App Control 也可能直接拦截安装器或主程序。

## 结论

- **本机/自用可以立刻解决**：用自签名证书签名，并把证书加入当前用户的“受信任的根证书颁发机构 / 受信任的发布者”，本机 `Get-AuthenticodeSignature` 显示 Valid，发布者名称正常显示。
- **对所有用户真正解决**：必须使用 Microsoft 信任链中的代码签名证书（OV/EV 商业证书，或 Certum 开源证书 / SignPath 等面向开源项目的免费签名服务）。自签名证书在其他电脑上仍然是“未知发布者”，不能解决 SmartScreen/SAC。
- 签名只保护 EXE/DLL 等 PE 文件；ZIP 包本身不能 Authenticode 签名，ZIP 的完整性依靠 SHA-256 清单。

## 本机自签名（已可用）

```powershell
pwsh -File tools\sign_release.ps1 -SelfSigned -NoTimestamp `
  -Files out\Studio\MoleculeStudio.exe,dist\MoleculeStudio-OnlineSetup.exe
```

脚本会：创建/复用 `CN=MoleculeStudio Local Code Signing`（CurrentUser\My，5 年有效）→ 导出证书到 CurrentUser\Root 与 CurrentUser\TrustedPublisher → 用 SHA-256 签名 → 打印签名状态。

验证：

```powershell
Get-AuthenticodeSignature dist\MoleculeStudio-OnlineSetup.exe | Format-List Status,SignerCertificate
```

注意：这只影响本机；删除证书（Cert:\CurrentUser\My / Root / TrustedPublisher 中对应指纹）即可恢复未信任状态。

## 公开发行的可选证书

| 方案 | 适用对象 | 成本 | 说明 |
| --- | --- | --- | --- |
| **Certum Open Source Code Signing** | 公开开源项目（含个人） | 约 €30–40/年 | 需提交仓库地址与项目说明；面向 OSS 的 Authenticode 证书，适合 MoleculeStudio 这类 Apache-2.0 项目 |
| **SignPath.io（Open Source）** | 公开开源项目 | 免费（需审核） | 由 SignPath 签发项目证书并集成 CI；适合 GitHub Actions 自动签名 |
| OV 商业代码签名证书（Sectigo/DigiCert/GlobalSign 等） | 个人/企业 | 约 $200–400/年 | 需要实名/组织验证；SmartScreen 信誉需随下载量累积 |
| EV 代码签名证书 | 企业/组织 | 约 $300–600/年 + 硬件/云 HSM | 身份验证最严格；通常可较快获得 SmartScreen 信誉，但个人申请门槛高 |
| Azure Trusted Signing | 有 3 年以上历史的企业 | 约 $9.99/月 | 微软云签名；需要组织验证，个人项目通常不满足 |

推荐顺序：**SignPath（免费、适合 OSS）→ Certum OSS（便宜、支持个人）→ OV 商业证书（需要正式商业信誉时）**。EV 仅在必须快速获得 SmartScreen 信誉且具备企业资质时考虑。

## 拿到证书后的发版流程

1. 构建：`studio\build.py` 生成 `out\Studio\MoleculeStudio.exe`。
2. 签名主程序：
   ```powershell
   pwsh -File tools\sign_release.ps1 -Thumbprint <证书指纹> `
     -Files out\Studio\MoleculeStudio.exe
   ```
   PFX 文件也可用 `-PfxPath <file> -PfxPassword <pwd>`（脚本会临时导入 CurrentUser\My）。
3. 重新构建 Core 包（让 Core 内的 exe 是已签名版本），再生成 `stable.json`。
4. 签名安装器（安装器不在 stable.json 哈希中，最后签即可）：
   ```powershell
   pwsh -File tools\sign_release.ps1 -Thumbprint <证书指纹> `
     -Files dist\MoleculeStudio-OnlineSetup.exe
   ```
5. 用 `Get-AuthenticodeSignature` 确认状态为 `Valid` 且带时间戳，再上传 Release。

> Core 包内的安装器副本可能早于最终签名的安装器（引导用）；在线更新会读取 `stable.json` 中的真实哈希，不影响正常安装。

## SmartScreen / Smart App Control 注意事项

- 签名不等于立刻没有提示：OV 证书的 SmartScreen 信誉需要下载量累积；EV 通常更快。
- Smart App Control 只信任 Microsoft 判定的信誉；即使自签名证书在本机受信任，SAC 仍可能拦截。正式分发建议使用受信任 CA 的证书，并提交 Microsoft 误报/信誉反馈。
- 未签名版本的正确引导方式：从 ZIP 解压后右键属性勾选“解除锁定”，或校验 `SHA256SUMS.txt`；不要关闭系统安全功能来强制安装。
