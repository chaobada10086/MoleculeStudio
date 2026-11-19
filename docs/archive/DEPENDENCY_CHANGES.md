# 本次发布整理
保留既有 Apache-2.0 和第三方版权声明。源目录使用 studio/ 和 engine/，旧 PROVENANCE.md 中 re_round62_viewer/ 路径是历史路径。
OCSR 仅含 MolScribe 生产模型，不含非商业授权的 COMO。去掉无关的视频 FFmpeg 插件 opencv_videoio_ffmpeg455_64.dll，OCSR 使用静态图像/PDF，不提供视频导入。
保留一个 OCSR CPython 基础环境，移除重复版本、包管理缓存、失效的 pip/activate 启动脚本。宿主会在启动识别前修正虚拟环境的 home 路径。
移除 AI home 中会话和状态；删除 overlay 中个人论文和固定工作区引用。没有删除用户原安装目录的数据。
