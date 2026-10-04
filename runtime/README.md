# Windows 离线翻译运行库

`windows-runtime.zip` 是可直接解压的 Python 运行库，首次翻译自动解压至 `runtime/python`；无需安装 Python。英文选词不加载模型或运行库。Ubuntu 使用 `install.sh` 创建 `runtime/venv`，不使用 Windows 包。

包内包含官方 Python 3.12.10 Windows x64 embedded distribution、CTranslate2 4.4.0、SentencePiece 0.2.0、NumPy 1.26.4、PyYAML 6.0.2、setuptools 70.3.0、msvc-runtime 14.44.35112，以及安装工具 pip 26.2.1。MSVC 文件保持 Microsoft 签名，供翻译运行库使用，不写入系统目录。所有组件保持原有许可证，许可文件保留在解压目录及各包的 `.dist-info` 中；本项目的非商业限制不适用于这些组件。

Windows 运行库 SHA-256：`a23d84bf78f2906c92ed9c215d3520b0f3d0ac15f545e6ca4a4bef77a1eb7bdf`。

可使用 `tools/package-runtime.ps1` 从官方 Python / PyPI 重建。压缩时间戳可能导致归档校验值不同；模型独立校验见 `models/manifest.json`。完整运行库约 58 MiB 压缩、首次使用另需约 200 MiB 解压空间；模型约 79 MB。Windows x64 CPU 需要支持 SSE4.1；Ubuntu 安装脚本针对 x86_64，其他架构需可用的 CTranslate2 wheel。
