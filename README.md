# ZW3D MyFirstPlugin

这是一个面向 **ZW3D WuKong 2027** 的 C++ 插件开发仓库，包含 WebView2 嵌入式网页窗口、参数化零件插入、零件/装配参数表达式读写、后台任务目录监听，以及对应的 ZW3D 菜单资源。

> 当前仓库是开发源码仓库，不是可直接分发的安装包。克隆后不能保证开箱即用，使用前需要准备 ZW3D SDK、Visual Studio、WebView2 Runtime 和前端服务。

## 功能概览

- 在 ZW3D 中打开基于 Microsoft Edge WebView2 的嵌入式网页窗口。
- 通过网页消息调用零件导入、装配导入、参数化导入、STEP 导出和预览等功能。
- 注册参数化零件插入命令，包括螺栓和轴承示例。
- 读写当前零件或装配中的参数表达式。
- 监听任务目录，读取 `.Z3PRT` 和 `.json` 任务文件并输出处理结果。
- 提供 ZW3D 菜单、Ribbon 和 Profile 资源配置。

## 当前限制

这个版本需要二次配置后才能真正运行，主要限制如下：

1. 仓库不包含 `MyFirstPlugin.dll`、`MyFirstPlugin.zrc` 或其他预编译安装包。
2. 仓库不包含 ZW3D SDK、ZW3D 安装程序或相关授权文件。
3. 仓库不包含前端项目。默认网页地址是 `http://localhost:5173/`。
4. 项目与 **ZW3D WuKong 2027** 强绑定，其他 ZW3D 版本需要修改项目配置和安装脚本。
5. 部分功能包含开发机硬编码路径，复制到其他电脑后需要修改并重新编译。
6. 当前仓库尚未提供开源许可证。对外分发前应先补充许可证并确认 ZW3D SDK 的使用和再分发条件。

## 目录结构

| 路径 | 说明 |
| --- | --- |
| `MyFirstPlugin.sln` | Visual Studio 2022 解决方案 |
| `MyFirstPlugin/` | 插件 C++ 源码、项目文件和界面资源 |
| `MyFirstPlugin/src/` | 插件核心实现 |
| `MyFirstPlugin/forms/` | ZW3D 界面表单资源 |
| `MyFirstPlugin/commands/` | 命令资源 |
| `Settings/` | ZW3D 菜单、Ribbon、Profile 和资源池配置 |
| `Install-ZW3DMenu.ps1` | 安装菜单资源并注册插件启动项的 PowerShell 脚本 |

## 环境要求

| 组件 | 要求 |
| --- | --- |
| 操作系统 | Windows 10 或 Windows 11，64 位 |
| ZW3D | ZW3D WuKong 2027，64 位 |
| Visual Studio | Visual Studio 2022 17.x |
| C++ 工具集 | MSVC v143，Desktop development with C++ |
| 构建平台 | `x64` |
| NuGet | `Microsoft.Web.WebView2` 1.0.2903.40 |
| 浏览器运行时 | Microsoft Edge WebView2 Runtime |
| PowerShell | Windows PowerShell 5.1 或更高版本 |
| 前端 | 默认要求 `http://localhost:5173/` 可访问 |

### ZW3D SDK

项目通过环境变量 `ZW3D_DIR` 查找 ZW3D 头文件、库文件和资源编译器。该变量必须指向 ZW3D 安装根目录，并保留末尾反斜杠。

示例：

```powershell
$env:ZW3D_DIR = 'C:\Program Files\ZWSOFT\ZW3D WuKong 2027\'
```

设置后应确认以下文件或目录存在：

```powershell
Test-Path "$env:ZW3D_DIR\api\inc"
Test-Path "$env:ZW3D_DIR\ZW3D.lib"
Test-Path "$env:ZW3D_DIR\zrc.exe"
```

如果需要长期保留该配置，请通过 Windows 的“环境变量”设置添加到当前用户环境。

## 克隆仓库

```powershell
git clone https://github.com/duolaalin111/plugin-for-zhongwang3d.git
cd plugin-for-zhongwang3d
```

当前默认分支是 `master`。

## 构建步骤

### 使用 Visual Studio

1. 安装 Visual Studio 2022 的 C++ 桌面开发工作负载。
2. 安装 Microsoft Edge WebView2 Runtime。
3. 安装 ZW3D WuKong 2027，并设置 `ZW3D_DIR`。
4. 使用 Visual Studio 打开 `MyFirstPlugin.sln`。
5. 等待 NuGet 自动还原 `Microsoft.Web.WebView2 1.0.2903.40`。
6. 选择 `Debug | x64` 或 `Release | x64`。
7. 执行“生成解决方案”。

### 使用开发者 PowerShell

```powershell
$env:ZW3D_DIR = 'C:\Program Files\ZWSOFT\ZW3D WuKong 2027\'

nuget restore .\MyFirstPlugin.sln
msbuild .\MyFirstPlugin.sln /m /p:Configuration=Debug /p:Platform=x64
```

Release 构建：

```powershell
msbuild .\MyFirstPlugin.sln /m /p:Configuration=Release /p:Platform=x64
```

如果系统找不到 `msbuild`，请从 Visual Studio 的 **Developer PowerShell for VS 2022** 中执行命令。如果系统没有 `nuget` 命令，可以使用 Visual Studio 的自动还原，或单独安装 `nuget.exe`。

## 构建后安装行为

每次成功构建后，项目会执行以下 Post-Build 操作：

1. 使用 `$(ZW3D_DIR)zrc.exe` 生成 `MyFirstPlugin.zrc`。
2. 将 `MyFirstPlugin.dll` 和 `MyFirstPlugin.zrc` 复制到：

   ```text
   %APPDATA%\ZWSOFT\ZW3D\ZW3D WuKong 2027\custom\apilibs
   ```

3. 执行 `Install-ZW3DMenu.ps1`。
4. 将菜单和 Profile 配置安装到当前用户目录。
5. 写入 `HKCU:\Software\ZWSOFT\ZW3D` 下的插件启动注册项。

`Install-ZW3DMenu.ps1` 会删除一部分旧版 MyFirstPlugin 专用资源，防止旧菜单配置覆盖新版本。运行前应先检查该脚本，避免它与本机其他自定义菜单冲突。

如果关闭了项目的 Post-Build 事件，需要在确认 DLL 已复制到 `apilibs` 后手动执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-ZW3DMenu.ps1
```

## 运行步骤

1. 先启动前端开发服务器，确保 `http://localhost:5173/` 可以访问。
2. 完成后端插件构建和安装。
3. 完全退出并重新启动 ZW3D WuKong 2027。
4. 在 ZW3D 中打开插件菜单或执行 `MyOpenDialog`。
5. 如果网页窗口提示连接失败，说明前端服务未启动或地址不正确。

仓库中没有前端源码。没有前端服务时，WebView2 窗口无法显示完整业务界面，但部分不依赖网页的 ZW3D 命令仍可能可以单独执行。

## 已注册命令

### 网页窗口和示例命令

| 命令 | 说明 |
| --- | --- |
| `MyOpenDialog` | 打开默认网页窗口 |
| `TemplateCmd` | 模板命令示例 |
| `ShowForm` | 显示示例表单 |

### 参数化零件插入

| 命令 | 说明 |
| --- | --- |
| `InsertTestBolt` | 插入默认螺柱测试件 |
| `InsertBoltShape` | 插入螺栓形状 |
| `InsertBearingSmall` | 插入小轴承 |
| `InsertBearingLarge` | 插入大轴承 |

### 参数表达式

| 命令 | 说明 |
| --- | --- |
| `ListPartExpressions` | 列出当前零件表达式 |
| `GetPartExpression` | 读取单个零件表达式 |
| `SetPartExpression` | 设置单个零件表达式 |
| `SetPartExpressions` | 批量设置零件表达式 |
| `ListAsmExpressions` | 列出装配表达式 |

### 后台任务

| 命令 | 说明 |
| --- | --- |
| `ProcessTaskFolder` | 处理任务目录中的待处理文件 |
| `StartTaskWatcher` | 启动任务目录监听 |
| `StopTaskWatcher` | 停止任务目录监听 |

## 需要修改的硬编码配置

### 前端地址

默认地址位于 `MyFirstPlugin/src/CustomCommand.cpp`：

```cpp
static const wchar_t* WEB_PAGE_URL = L"http://localhost:5173/";
```

部署时请改为实际前端地址，或先在本机启动前端开发服务器。

### 后台任务目录

默认目录位于 `MyFirstPlugin/src/FileWatcherCommand.cpp`：

```cpp
static const char INPUT_DIR[]   = "D:\\ZW3D_Plugins\\task_input";
static const char OUTPUT_DIR[]  = "D:\\ZW3D_Plugins\\task_output";
static const char ARCHIVE_DIR[] = "D:\\ZW3D_Plugins\\task_done";
```

其他电脑如果没有 `D:\ZW3D_Plugins`，需要修改这些路径并重新编译。

### 参数化零件模板

`MyFirstPlugin/src/PartInsertCommand_Parameterized.cpp` 和 `MyFirstPlugin/src/WebViewWindow.cpp` 中包含类似以下开发机路径：

```text
D:\BaiduNetdiskDownload\ZWSOFT\ZW3D WuKong 2027
C:\Users\zxcvb\Documents\ZW3D\testparts
```

实际部署前必须替换为本机模板、测试零件和输出目录。

### ZW3D 版本和用户目录

以下文件包含 ZW3D WuKong 2027 专用路径：

- `MyFirstPlugin/MyFirstPlugin.vcxproj`
- `Install-ZW3DMenu.ps1`

升级 ZW3D 版本时，需要同步修改安装目录、`%APPDATA%` Profile 路径和注册表版本判断。

## 常见问题

### 编译时找不到 `zwapi_*.h`

检查 `ZW3D_DIR` 是否指向 ZW3D 安装根目录，并确认变量末尾有反斜杠。

### 链接时找不到 `ZW3D.lib`

确认 `ZW3D.lib` 位于 `$(ZW3D_DIR)` 根目录，并且当前使用的是 `x64` 配置。

### 编译时找不到 `WebView2.h`

执行 NuGet 还原，确认项目目录下存在：

```text
packages\Microsoft.Web.WebView2.1.0.2903.40
```

### WebView2 初始化失败

安装或修复 Microsoft Edge WebView2 Runtime。

### 构建成功但 ZW3D 中没有菜单

检查 Post-Build 是否执行成功，确认 `Install-ZW3DMenu.ps1` 的输出没有错误，然后完全重启 ZW3D。

### 网页窗口打不开或显示连接失败

确认前端服务正在监听 `http://localhost:5173/`。如果前端部署在其他地址，请修改 `WEB_PAGE_URL` 后重新编译。

### 任务监听没有处理文件

确认 `D:\ZW3D_Plugins\task_input` 存在，并且 `.Z3PRT` 文件有同名的 `.json` 文件；切换部署路径后还需要重新编译。

## 仓库未包含的内容

- ZW3D 安装包和 ZW3D SDK。
- 已编译的 DLL、ZRC 和 Release 安装包。
- Web 前端源码和前端构建产物。
- 正式的部署器或安装包。
- ZW3D API PDF、CHM 和其他本地参考文档。
- 开源许可证文件。

因此，目前仓库适合开发人员共用源码和继续开发，不适合直接发给最终用户安装使用。若要让其他人开箱即用，需要补充可配置部署方案、前端发布方式、Release 打包流程和完整的使用文档。
