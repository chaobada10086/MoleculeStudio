# MoleculeStudio Linux 原生移植交接

交接日期为 2026-11-21。当前有可安装、可运行的 Ubuntu 开发包，Windows 正式版的完整功能移植仍在进行。用户的验收目标是原生 Linux 应用、实际操作验证、安装回归和可交付的安装包。用户希望持续推进，普通实现选择可以直接处理。

## 新对话先从哪里开始

| 项目 | 实际位置或状态 |
| --- | --- |
| Windows 上的移植工作树（主编辑位置） | C:\Users\ChaobaDa\Documents\GitHub\MoleculeStudio-linux |
| Git 分支及交接时的 HEAD | linux-port，45ba865a928307d14ec6c92434336330d1d2f233 |
| Windows 版参考工作树 | C:\Users\ChaobaDa\Documents\GitHub\MoleculeStudio |
| Ubuntu 测试机 | ssh chaobada@192.168.31.20，Ubuntu 26.04.1 LTS，amd64 |
| Ubuntu 构建目录 | /home/chaobada/MoleculeStudio-linux-port |
| 最近安装并回归过的包 | 构建目录下的 moleculestudio-native_0.2.0~dev8_amd64.deb |
| 已安装程序 | /usr/bin/moleculestudio-native，dpkg 查询版本为 0.2.0~dev8 |
| 更新的源码构建 | 构建目录下的 out/linux-native/moleculestudio，已包含工具名称修改 |
| 测试源文件和样例 | /home/chaobada/MoleculeStudio-regression-qAaAD5（持久副本，含 SHA256SUMS） |

移植改动大量保存在工作树，包括尚未加入 Git 的 studio/linux/、studio/engine_sources.py、studio/conformer_model.hpp 和 command_labels.hpp。新对话直接打开现有移植目录，先运行 git status --short --branch。单独克隆上述提交、切回 Windows 主工作树或清理未跟踪文件都会漏掉当前成果。Ubuntu 为同步副本，使用过按文件传输的方式，开始构建前请核对本轮所需文件。SSH 登录凭据沿用用户之前提供的方式，本文不保存密码。

## 代码入口

- studio/linux/main.cpp 是 GTK 3 原生桌面入口，连接生产版 C++ Session、画布、菜单操作、选区和文件工作流。studio/linux/font.cpp、clipboard.cpp 分别处理字体和系统剪贴板。
- studio/engine_sources.py 定义共享引擎源文件及正式版覆盖项。studio/linux/build_engine.py 构建原生可执行文件。studio/linux/package-native.sh 生成 Debian 安装包。
- studio/linux/editor.py 和 package-deb.sh 属于较早的 Python 预览版。正式移植继续使用上面的 C++ 入口和 package-native.sh。
- studio/studio.cpp 是核对 Windows 用户操作的主要入口。studio/interop.hpp 提供共享文件读写入口；studio/conformer_model.hpp 提取了共享三维构象模型。Linux 的格式转换桥在 studio/linux/convert_structure.py。

## 已有验证记录

| 功能 | 已验证的范围 |
| --- | --- |
| 文档 | CDXML、CDX、R62DOC 保存、覆盖、重新打开；已有中文文件名样例。 |
| 编辑和剪贴板 | 两个独立 GTK 进程通过系统剪贴板传递苯环结构；验证粘贴及撤销、重做。多行文字可编辑、撤销、保存并读回。 |
| 模板 | 已打包试剂和生物模板。读取到 910 个目录条目，并验证了模板插入路径。 |
| 化学功能 | 已检查苯的分子质量为 78.114；已有 SMILES、Molfile 转换和 HELM 导入路径。HELM 导出仍需独立验证。Ubuntu 使用过 RDKit 2025.03.6。 |
| 三维及画布 | 验证过 RDKit 三维构象生成及撤销、重做；验证过缩放锚点、平移和视图边界。 |
| 安装 | 在上述 Ubuntu 机器安装到 dev8，其他发行版及架构仍待实测。 |

测试目录中有 caption.cpp、clip.cpp、formats.cpp、mass.cpp、templates.cpp、three.cpp、nav.cpp 以及文档样例。持久副本共 8 份测试源码和 6 份文档，复制后已逐项核对 SHA-256。早期编译的测试程序位于 /tmp/tmp.u3lRoMb1TP；验证新版本时应使用当前源码重新编译。Ubuntu 构建目录另有 caption-build.log、files-build.log、three.log、nav.log 等，交接时已核实文件存在。上述功能记录是阶段性覆盖。

## 最后一次修改及接手后的第一项验证

GTK 工具下拉框原先直接显示内部命令名。最新修改把中文标签逻辑从 engine/next/integration/ribbon/ribbon_model.cpp 提取到同目录的 command_labels.hpp，并在 GTK 入口复用。列表仍以 Session 条目索引激活绘图命令。本轮已在 Ubuntu 编译、链接，日志为 labels-build.log。已核实最新可执行文件和已安装的 dev8 哈希不同，最新标签改动尚未做 GTK 操作回归，也尚未打入新包。

1. 运行构建目录下的 out/linux-native/moleculestudio，选择“单键”和“苯环”实际绘图，验证显示名称、生成的结构及撤销结果。核对 Windows ribbon 头文件调整后仍能编译。
2. 对照 studio/studio.cpp 按用户工作流整理剩余差异，优先完成原子标签、绘图工具、选择与格式、多文档、设置与快捷键。继续核对 NMR、图像识别、AI 等正式版入口、依赖和语言资源，记录实测及未验证项目。
3. 单独验证 HELM 导出回读、跨版本文件兼容及升级安装。在干净的 Ubuntu 桌面环境安装候选包，验证桌面启动、保存重开、系统剪贴板、模板和运行时依赖。
4. 新安装包使用可区分的开发版本，复制到 Windows 侧可访问的位置，附版本、架构、校验值和验证范围。最终验收按用户功能路径和安装结果判断。

## Ubuntu 构建入口

在同步完所需源码后，从 Ubuntu 构建目录执行：

~~~sh
cd /home/chaobada/MoleculeStudio-linux-port
python3 studio/linux/build_engine.py
./out/linux-native/moleculestudio
~~~

完成 GUI 验证并更新打包脚本中的开发版本后运行：

~~~sh
sh studio/linux/package-native.sh
dpkg-query -W moleculestudio-native
~~~

第一条生成安装包，第二条只查询已安装版本。安装新生成的具体 deb 后再执行版本查询，并从桌面入口重测。现有 dev8 包可以保留作升级回归基线。

## 可直接发给新对话

请读取当前工作树根目录的《Linux移植交接.md》，在现有 linux-port 工作树继续完成 MoleculeStudio 的 Linux 原生移植。先在 Ubuntu 验证最新工具名称及实际绘图，再继续补齐 Windows 正式版工作流、打包和安装回归。请持续执行并报告真实验证结果，直到交付完整安装包，或遇到确实需要我处理的阻塞。
