# installer/store — 微软商店提交材料

这些文件都供 Partner Center 提交使用，不是运行时代码。完整操作流程见 [docs/STORE_PUBLISH.md](../../docs/STORE_PUBLISH.md)。

| 文件 | 用途 | 贴到哪里 |
| --- | --- | --- |
| [AppxManifest.xml](AppxManifest.xml) | MSIX 清单模板（身份 / 能力 / 文件关联 / COM 服务器） | 不直接提交，由 tools/package_store.py 渲染进包 |
| [PRIVACY.md](PRIVACY.md) | 中英隐私声明源文件 | 发布到公开 HTTPS 页面，地址填进「属性 → 隐私政策 URL」 |
| [../../docs/privacy.html](../../docs/privacy.html) | 可直接部署的隐私声明页面 | GitHub Pages 等静态托管 |
| [CERTIFICATION-NOTES.md](CERTIFICATION-NOTES.md) | 受限能力理由 + 认证说明（含 OLE 测试矩阵 T1–T7） | 「提交选项 → 受限能力」与「认证说明」 |
| [LISTING.zh-CN.md](LISTING.zh-CN.md) | 中文商店文案、截图清单、年龄分级答案 | 「商店列表」与「年龄分级」 |
| [LISTING.en-US.md](LISTING.en-US.md) | 英文商店文案（同上） | 同上 |

## 打包命令

    python tools/package_store.py ^
      --release "D:/Release" ^
      --output  "D:/Store/MoleculeStudio-0.9.7.0-x64.msix" ^
      --name "Partner Center 的 Package/Identity/Name" ^
      --publisher "CN=..., O=..., C=CN" ^
      --publisher-display-name "发布者显示名" ^
      --version "0.9.7.0.0"

需要 Windows SDK 的 makeappx.exe、Python 3.10+ 与 Pillow，以及同版本的四个发布 ZIP。
加 --dry-run 可以在没装 SDK 的机器上检查暂存内容与渲染后的清单。
脚本还会校验 MANIFEST-组件.json 里的 SHA-256、剔除自更新器与用户配置、把
chemistry_backend.ini 改成相对 Python 路径、生成 25 张商店图标。

## 清单里的两个受限能力

| 能力 | 为什么需要 | 审核风险 |
| --- | --- | --- |
| runFullTrust | 原生 Win32 + Direct3D 11 + 随包 Python 与本地识别模型，必须以 mediumIL 运行 | 低（桌面打包应用的标准做法） |

## 必须保持同步的身份值

清单里的 COM 登记必须与 [studio/ole_embed.hpp](../../studio/ole_embed.hpp) 一致，改一处就要改另一处：

- CLSID C000BC8C-8C39-4722-B93C-D40D3319E7B5（kDocClsid）
- ProgID MoleculeStudio.Document.1（kProgId）与 MoleculeStudio.Document（kProgIdVerInd）
- 服务器启动参数 -Embedding
