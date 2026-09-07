# Bob's Toy Renderer

## 关于此仓库
本仓库是我学习图形学过程中开发的一个玩具渲染器，支持基本的实时渲染算法及框架功能。

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

**通过 xmake 管理的依赖：**

[ImGui v1.92.7](https://github.com/ocornut/imgui)：即时模式GUI库，用于编辑器面板、属性编辑与各类调试工具的搭建。本仓库在 xmake 包的基础上，于 thirdparty/imgui 中自行维护了 SDL3/Vulkan 的 backend 集成。

[SDL3 3.4.12](https://github.com/libsdl-org/SDL)：跨平台窗口与输入库，负责窗口创建、事件循环和键鼠输入处理，是引擎 HAL 层的平台入口。

[Vulkan SDK 1.4.341.1](https://vulkan.lunarg.com/)：Khronos 官方 Vulkan SDK，提供头文件、加载器与验证层等。本仓库的 RHI 层完全基于 Vulkan 构建。

[assimp v6.0.5](https://github.com/assimp/assimp)：Open Asset Import Library，支持数十种模型格式的导入，用于加载 glTF/FBX/OBJ 等网格、材质与骨骼数据。

[Eigen 5.0.1](https://gitlab.com/libeigen/eigen)：C++ 线性代数模板库，提供矩阵与向量运算，用于 CPU 侧的数学计算。

[spdlog v1.17.0](https://github.com/gabime/spdlog)：高性能 C++ 日志库，用于引擎运行时的日志输出。

[cereal v1.3.2](https://github.com/USCiLab/cereal)：C++ 序列化库，用于场景、资产等数据的二进制序列化。

[stb 2026.03.18](https://github.com/nothings/stb)：单头文件工具集，主要使用 stb_image 完成纹理图像的加载。

[meshoptimizer v1.2](https://github.com/zeux/meshoptimizer)：网格优化库，提供网格简化、顶点重索引与 meshlet 构建等算法，用于 Virtual Geometry 管线的网格预处理。

[METIS v5.2.1](https://github.com/KarypisLab/METIS)：图分割库，可将大规模图切分为规模均衡的子图，用于网格等数据的分区与聚类。

[MikkTSpace 2020.03.26](https://github.com/mmikk/MikkTSpace)：切线空间生成的参考实现，用于为导入的网格计算切线基。

[eventpp v0.1.3](https://github.com/wqking/eventpp)：C++ 事件分发与回调库，用于引擎各模块间的事件机制。

[stduuid v1.2.3](https://github.com/mariusbancila/stduuid)：C++ UUID 生成与解析库，用于资产和实体的唯一标识。

**源码直接引入（thirdparty/）：**

[NRD 4.16.0](https://github.com/NVIDIA-RTX/NRD)：NVIDIA Ray Tracing Denoiser，提供多种时域降噪与信号重建算法，用于光追信号（SSR/GI/阴影等）的降噪。

[Vulkan Memory Allocator 2.3.0](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)：AMD 开源的 Vulkan 显存分配器，负责显存的 sub-allocation 与内存池管理。

[volk（Vulkan headers 1.4.274）](https://github.com/zeux/volk)：Vulkan loader 的轻量元加载器，在运行时加载 Vulkan 函数指针，避免静态链接 loader。

[MathLib（源码快照）](https://github.com/NVIDIA-RTX/MathLib)：NVIDIA 开源的单头文件数学库（ml.h），SSE/AVX/NEON 加速且与 HLSL 语法兼容，用于 CPU 侧的图形数学计算。

[ShaderMake（源码快照）](https://github.com/NVIDIA-RTX/ShaderMake)：NVIDIA 开发的着色器批量编译前端，兼容 FXC/DXC/Slang，用于着色器编译管线。

[SPIRV-Reflect（源码快照）](https://github.com/KhronosGroup/SPIRV-Reflect)：Khronos 的 SPIR-V 反射库，用于解析编译后着色器的资源与描述符布局。

[implot 1.0](https://github.com/epezent/implot)：ImGui 的绘图扩展，用于性能面板中的曲线绘制（帧时间、GPU pass 耗时等）。

[imgui-node-editor 0.9.4](https://github.com/thedmd/imgui-node-editor)：ImGui 的节点编辑器扩展，用于 RDG 的可视化图面板。

[ImGuizmo（源码快照）](https://github.com/CedricGuillemet/ImGuizmo)：ImGui 的 3D Gizmo 扩展，用于编辑器中物体的平移/旋转/缩放操纵。

[imgui-flame-graph（源码快照）](https://github.com/bwrsandman/imgui-flame-graph)：ImGui 的火焰图 widget，用于 CPU 性能分析的可视化。

[SMHasher（源码快照）](https://github.com/aappleby/smhasher)：MurmurHash 系列非加密哈希函数及其测试套件，用于引擎内的快速哈希计算。

[LEMON（源码快照）](https://lemon.cs.elte.hu/trac/lemon)：图论与线性规划 C++ 库，提供图算法与 LP 求解基础设施，用于RDG图的基层构建。

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


