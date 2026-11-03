# 0.95 OCSR hotfix 1

交付文件：MoleculeStudio-0.95-OCSR-addon-win64-hotfix1.zip。它是完整 OCSR addon，替换旧 OCSR ZIP；Core 和 AI ZIP 不变。解压到现有 MoleculeStudio 目录并覆盖，先关闭软件。

变更：补充 app-local C++ DLL 并在服务内显式预加载；status 握手不导入 torch，首次识别才加载；将 stderr 保存为本地服务日志。dependency_check=deferred 不表示推理依赖已验证。

新增 DLL 来自 PyPI msvc-runtime 14.42.34433 的 cp310-win_amd64 wheel，保留其 LICENSE/METADATA/SOURCE.json。基础 CPython 原有的较新 vcruntime140、vcruntime140_1、vcruntime140_threads 保持不变，并复制到 venv/Scripts。不要从开发机 System32 随意复制 DLL。

打包工具 tools/package_ocsr_hotfix.py 接收 --original 旧发布 ZIP、--staging 修订后的 MoleculeStudio 目录、--output 新 ZIP。它保留旧包其他内容，更新修改文件的清单与 SHA256，校验 ZIP CRC，并生成压缩包校验文件。

验证：从旧发布 ZIP 独立解压到中文和空格路径，去除 Python 环境变量和自定义 PATH 后启动成功。修订服务启动握手约 0.24 秒；完整模型加载通过；GetModuleFileNameW 确认 MSVCP140 从 addon 的 venv/Scripts 加载。完成一次图片推理；该测试只检查流程，原模型仍出现额外片段，不能声称识别准确率通过。报告故障的另一台电脑尚未实测。

缺少 VC++ 运行库是发现的跨机器依赖缺口，并非已证明为该用户截图的唯一原因。原生 exe 的 pyvenv.cfg 写入权限与 Python 启动前错误捕获不属于仅替换 OCSR 包可全面修复的范围。
