# AI 平铺目录发布

运行目录从 `AI 套件/dsh_core/dsh/node_modules` 移到 `ai/node_modules`，Node 从 `AI 套件/dsh_core/node/node.exe` 移到 `ai/node.exe`。配置、package.json、desktop-runtime.json 一并移到 ai 根目录。保留依赖包内部相对目录、第三方许可证及配套源码，不做无依据删减。

原生 UI 同时识别新旧 AI 目录；ai_dsh.py 优先使用新目录，兼容旧目录以及 MOLECULE_STUDIO_DSH_CORE。新核心与新 AI 包一起发布，避免旧核心无法识别新布局。OCSR 已恢复为坐标映射偏差过大时停止识别，不输出结构。

先按 studio/build.py 的 DX11 配置构建核心，再执行 `python installer/build_offline.py`。用 `python tools/package_compact_ai.py --source 旧三包发布目录 --output 新发布目录` 生成三包、校验文件和离线安装器。打包器保留所有依赖内容，更新清单，并逐文件校验。

离线安装器读取同目录 RELEASE_CHECKSUMS.json 和三个 ZIP，核心先安装，AI/OCSR 可选。使用扩展长度路径，拒绝越界路径、重复路径和目录链接；校验 ZIP 哈希及每个文件的哈希。遇到内容不同的现有文件会停止，避免覆盖；已完成的相同文件可在重试时跳过。建议新目录安装，不承担旧版本组件迁移或卸载管理。

`python tools/test_offline_install.py` 验证超 260 字符路径、重复安装、保护修改文件和拒绝路径越界。联网安装器的 stable.json 需在新包正式上传后另外更新，本次仅生成本地发布文件。

2026-11-06 本机验证：三个包逐文件哈希通过；新旧运行时路径发现通过；长路径离线解压、重复安装、修改文件保护及越界拒绝通过。完整 AI 包在长安装目录下解压成功；新布局的 Node CLI --help 和 headless 配合 overlay.yml 的 --dump-config 均返回 0。核心编译及 formula-check 通过。未调用联网 AI 服务，未将这些检查表述为完整绘图任务验收。
