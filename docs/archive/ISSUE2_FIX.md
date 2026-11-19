# 问题二修复说明

基于 chaobada10086/MoleculeStudio v0.9.5.0.0 源码与官方发布包。

报告所述 rdForceField / rdDistGeom 失败是目标机器的 Windows Smart App Control 对特定 RDKit 二进制的拦截。此包不改变系统安全策略，而是为 Core 附带独立 CPython 3.10 运行时，采用报告验证过的 RDKit 3.10 构建组合。

chemistry_backend.ini 新增 ChemistryPython=chemistry_python\python.exe，化学、核磁、三维使用此项；AI 仍使用 Python=python\python.exe。未设置 ChemistryPython 的旧配置保持兼容。核磁 nmr-ml-site 改为追加搜索，优先使用解释器自带的 ABI 匹配依赖。运行时来自官方 OCSR hotfix2 包，但 Core 不需要安装 OCSR 插件；没有改变问题一的代码。

源码核查发现：分子式/分子量卡片由 C++ 本地计算，不经过 RDKit。主画布必须选中结构才显示；核磁页使用独立顶部布局。因此报告关于“RDKit 导致分子信息整条链路失败”的解释不适用于当前源码，不能据此承诺修复所有空白情况。

验证：新编译 EXE 的 --formula-check 返回 C2H6O 46.069 PASS；实际 --chem-service 调用阿司匹林氢谱覆盖 8/8、碳谱 9/9、命名 2-acetyloxybenzoic acid；三维构象返回非零 Z 坐标。RDKit 力场、距离几何、绘图模块导入正常。旧的完整 GUI 自测在快捷键迁移断言处失败，未将其标记为通过。

本机测试不能替代报告中开启 Smart App Control 的目标机器验收；3.10 二进制能否被另一台电脑放行仍由其策略决定，本次不是代码签名修复。

安装：将 Core 修复包解压到新目录，运行 MoleculeStudio.exe。不要用旧 chemistry_backend.ini 覆盖新配置。可继续按需合并同版本 AI/OCSR 插件。本地提供发布包，未推送或发布到 GitHub。

重建：按 README 编译；从官方 Core ZIP 解压到 staging/Core/MoleculeStudio；运行 tools/stage_chemistry310.py --ocsr 官方OCSR包.zip --core staging/Core/MoleculeStudio；复制编译出的 EXE 和修改后的 chemistry_open.py；调用 tools/package_release.py 中的 package(staging,output,'0.95-issue2-hotfix','Core') 生成清单并进行审计及 CRC 校验。保留原 Core 许可和新运行时的 legal/Chemistry310 来源资料。
