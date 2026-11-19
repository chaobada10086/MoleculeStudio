# MSIX 本地签名失败的排查记录

状态：**未结案，但不阻塞提交**。记录在这里，避免以后重复踩。

## 现象

在本机用自签名证书给打包好的 .msix 签名时报：

    SignTool Error: SignedCode::Sign returned error: 0x800700C1
    For more information, please see https://aka.ms/badexeformat

证书本身没问题：同样的证书、同样的 signtool 能给 10MB 的小包正常签名并安装。
签名失败的包仍然可以正常 Add-AppxPackage -Register（开发者模式松散注册），程序运行、
OLE 注册、数据目录都正常——说明**包结构是合法的**，只是过不了 AppxSip 这一步。

## 已排除的因素

| 假设 | 实验 | 结论 |
| --- | --- | --- |
| 包体积超限（解压） | 人造 926 / 1026 / 1086 MB 单文件包 | 全部签名成功 |
| 包体积超限（压缩后） | 人造 1070 MB 压缩包 | 成功 |
| 载荷体积超限 | 2226 MB 解压载荷（全零，压缩后 12.4 MB） | 成功 |
| 文件数超限 | 人造 8000 / 12000 / 20000 个文件 | 全部成功 |
| 空文件 | 1 个 / 1000 个 0 字节文件 | 成功 |
| ZIP64 | 检查 EOCD，失败包是普通 ZIP（22k 条目 / 1.09 GB） | 不适用 |
| signtool 版本 | 换 10.0.22621.3233 与 10.0.28000.2957 两个版本 | 都失败（同一个 AppxSip） |
| 清单 schema | makeappx 校验 + 迷你包可签可装 | 通过 |
| 某个 exe 有问题 | 包里 67 个 exe 逐个单独打包签名 | 0 个失败 |
| pip 启动器 stub | 4 个（torchrun / torchfrtrace / convert-*-caffe2） | **确实全部失败**，已从包中剔除 |
| 剔除上述 4 个后 | 重打整包 | **仍然失败** |

## 已定位的层级

    Core 整包                                     失败
     ├─ chemistry_runtime（Java + torch，690MB）  成功
     └─ chemistry_python（766MB）                 失败
          └─ Lib/site-packages 的 205 个条目对半分：两半各自成功，
             合起来失败 —— 不是单一文件，而是某种组合效应

## 结论与后续

1. **本地签名失败不等于包不合法**：同一份暂存目录可以松散注册、运行、写注册表、写数据目录。
2. **商店提交不需要我方签名**：微软用自家证书重签，所以这条不阻塞上架。
3. 真正的判定点是 Partner Center 上传：接受则本机这个坑只是测试便利问题；
   如果它也拒，再按上面的层级继续缩小范围，或改走政策 10.2.9 的 EXE/MSI 分发路线。
4. 顺手的真实收益：打包脚本剔除了 pip 生成的启动器 stub，这类文件在别的 Python 依赖里同样常见，
   属于通用的 MSIX 打包踩坑点。

## 复现方式

    python tools/package_store.py --release <四个 ZIP 所在目录> --output out.msix ^
      --name Test --publisher "CN=Test" --publisher-display-name Test ^
      --version 0.9.7.0.0 --makeappx <makeappx.exe> --dry-run

    signtool sign /fd SHA256 /f cert.pfx /p <密码> out.msix

## 测试时的替代办法

签名只是为了让 Add-AppxPackage 接受，测试时可以用松散注册：

    Add-AppxPackage -Register <暂存目录>/AppxManifest.xml

需要开发者模式（注册表 AppModelUnlock 下的 AllowDevelopmentWithoutDevLicense = 1）。
松散注册的包身份、注册表虚拟化豁免、COM 服务器登记与正式安装一致，
只有安装目录是可写的（不是 WindowsApps），因此"只读目录"相关的验证要用签名包做。
