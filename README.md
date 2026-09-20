# Renderer

## 关于此仓库
本仓库是我学习图形学过程中开发的一个简易渲染器，支持基本的实时渲染算法及框架功能。

**与渲染相关的主要目录及功能如下：**
|  目录   | 功能描述  |
|  ----  | ----  |
| Asset/BuildIn/Shader/  | 着色器代码 |
| src/Runtime/Function/Render/RHI/  | 基本的RHI实现, 封装了Vulkan后端 |
| src/Runtime/Function/Render/RDG/  | 基本的RDG实现 |
| src/Runtime/Function/Render/RenderResource/  | 上层绘制资源封装及Bindless管理 |
| src/Runtime/Function/Render/RenderPass/  | 基于RDG构建的渲染管线，包含较完整的绘制流程 |
| src/Test/  | 展示了RHI及RDG层的基本使用(deprecated) |

仓库的其他部分也包含了HAL抽象，资源系统，Component-Entity-Scene等功能的简单实现。

**目前已支持的算法及功能如下：**
- RHI
- RDG
- Virtual Geometry
- GPU-Driven Pipeline
- Surface Cache             
- CSM+PCF
- EVSM
- Cluster-Based Lighting
- IBL
- DDGI
- Volumetric Fog
- SSSR                      
- Bloom
- FXAA
- TAA
- Auto Exposure
- Tone Mapping
- ReSTIR DI
- ReSTIR GI
- SVGF                      *(WIP)*
- NRD
- Path Tracing


## 第三方库

- @docx/第三方库依赖.md


## 构建
本仓库使用xmake作为构建工具。目前HAL抽象仅实现了windows平台，RHI仅支持Vulkan后端。

**构建注意点1**：首次构建前，需要进入 `thirdparty/NRD` 目录，依次运行 `1-Deploy.bat`、`2-Build.bat`、`3-PrepareSDK.bat` 三个脚本，完成 NRD 的 CMake 配置、编译（Release/Debug）及 SDK 目录的生成。脚本依赖 git、cmake 及 MSVC 构建环境（`4-Clean.bat` 用于清理）。

**构建注意点2**：`src/Runtime/Function/Global/EngineContext.cpp` 中的 `fileSystem->Init("renderer")` 会在运行目录的绝对路径中查找 `renderer` 来定位项目根目录（区分大小写）。若你本地的项目文件夹名称与默认不同，需要将该字符串修改为你自己设置的项目文件夹名称，否则启动时会因无法定位资源路径（"Can't init file system base path!"）直接退出。

**方式一：命令行快速构建**
克隆后使用以下命令即可快速构建并运行。目前请以 debug 模式构建，release 模式暂时无法正常运行：

```shell
git clone https://github.com/bigmouseisjarry/Renderer.git
xmake f -m debug
xmake build renderer
xmake run renderer
```

**方式二：生成 VS2026 解决方案**
在完成上述注意点后，可在 renderer 目录下执行以下命令生成 Visual Studio 解决方案，随后直接在 VS2026 中打开并构建：

```shell
xmake project -k vsxmake
```

## 参考链接
本仓库的学习和开发过程参考了许多优秀的开源仓库，十分感谢各位大佬的无私分享。

https://github.com/SakuraEngine/SakuraEngine  
https://github.com/google/filament  
https://github.com/EpicGames/UnrealEngine  
https://github.com/CPJ-BO/ToyRenderer  
https://github.com/NVIDIA-RTX/NRD  


