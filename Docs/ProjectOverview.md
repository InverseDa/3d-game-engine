# Limitless Engine 项目总览

## 1. 项目简介

**Limitless Engine** 是一款开源 3D 游戏引擎，基于 **C++17** 与 **Vulkan** 构建，面向现代高性能游戏开发。引擎采用模块化 Runtime 架构，代码按职责划分为独立模块，通过 Sharpmake 元构建系统生成跨平台工程文件。

| 属性 | 说明 |
|------|------|
| 语言标准 | C++17 |
| 图形 API | Vulkan (跨平台) |
| 构建系统 | Sharpmake + .NET 8 |
| 目标平台 | Windows (win64) / macOS (mac, arm64) |
| 构建类型 | Editor / Game |
| 优化配置 | Debug / Release |
| 开源协议 | MIT |

## 2. 顶层目录结构

```
E:\Projects\3d-game-engine
├── docs/                       # 项目文档
├── Engine/
│   ├── binaries/               # 构建输出 (Shader 缓存等)
│   ├── Builder/                # Sharpmake 构建脚本
│   │   ├── LimitlessBuilder.cs # 入口主脚本
│   │   ├── Solution.cs         # Solution 规则
│   │   ├── LimitlessProject.cs # 主可执行工程规则
│   │   ├── Module.cs           # 模块基类 (ModuleRule / ThirdPartyModuleRule)
│   │   └── ...
│   ├── Content/                # 引擎内容资源
│   └── Source/
│       ├── Runtime/            # 引擎运行时模块 (核心)
│       └── ThirdParty/         # 第三方库
│           ├── Glm/
│           ├── Spdlog/
│           └── Vulkan/
├── External/                   # 外部工具链 (Sharpmake 子模块)
├── GenerateProject.bat         # Windows 工程生成脚本
├── GenerateProject.sh          # macOS/Linux 工程生成脚本
├── GenerateIncludes.ps1        # 模块自动发现脚本
├── LICENSE
└── README.md
```

## 3. 运行时模块一览表

`Engine/Source/Runtime` 为引擎核心源码目录，各模块职责如下：

| 模块 | 全称 | 职责 | 源文件数 | 依赖 |
|------|------|------|----------|------|
| **Core** | Core | 基础类型、日志、宏定义、引擎通用工具 (`FNonCopyable`、数值类型等) | 5 | Glm, Spdlog |
| **RAL** | Render Abstraction Layer | 渲染抽象层，封装 Vulkan 底层对象 (Buffer、Texture、Pipeline、Swapchain、CommandList 等) | 27 | Core, Spdlog, Vulkan |
| **RFG** | Render Frame Graph | 渲染帧图系统，负责 Pass 编排、资源屏障规划、依赖分析与执行调度 | 42 | Core, RAL |
| **Renderer** | Renderer | 高层渲染器，定义 RenderPass / RenderPipeline / RenderView，对接 Frame Graph 与 RAL | 18 | Core, RAL, RFG |
| **World** | World | 场景世界管理，负责从 World 提取渲染场景 (`WorldRenderSceneExtractor`) | 4 | Core, RAL, Renderer |
| **Launch** | Launch | 程序入口与平台抽象层，包含 `WinMain` / `main` 及平台窗口实现 (Windows/Mac) | 9 | Core, Spdlog, RAL, RFG, Renderer, World |

> **注**：文件数统计包含 `.cpp` / `.h` / `.hpp` / `.mm` 等源码文件。

## 4. 构建系统说明

### 4.1 Sharpmake 元构建

引擎使用 [Sharpmake](https://github.com/ubisoft/Sharpmake) 作为元构建系统。开发者不直接维护 `.sln` / `.vcxproj`，而是通过 C# 脚本描述工程规则，由 Sharpmake 生成对应 IDE 的工程文件。

### 4.2 支持的构建配置

配置维度为 **Platform × Optimization × TargetType**，共 8 种组合：

| 维度 | 可选值 |
|------|--------|
| Platform | `win64` (VS2022)、`mac` (Xcode, arm64) |
| Optimization | `Debug`、`Release` |
| TargetType | `Editor`、`Game` |

生成的 Solution 文件名格式：
```
Limitless_[DevEnv]_[Platform].sln
```

配置名称格式：
```
Debug Editor / Debug Game / Release Editor / Release Game
```

### 4.3 模块编译输出规则

- **Launch 模块**：始终编译为静态库 (`Lib`)。
- **Editor (win64)**：模块编译为动态库 (`Dll`)，并导出 `__declspec(dllexport/dllimport)` 形式的模块 API 宏。
- **Game / mac**：模块编译为静态库 (`Lib`)，API 宏展开为空。

## 5. 如何生成项目

### Windows

双击或在终端执行：

```batch
GenerateProject.bat
```

脚本执行流程：
1. **自动发现模块** —— 调用 `GenerateIncludes.ps1` 扫描所有 `.Build.cs`。
2. **验证工具链** —— 检查 Sharpmake 子模块与 .NET SDK。
3. **编译 Sharpmake** —— `dotnet build Sharpmake.Application.csproj`。
4. **执行生成** —— 运行 Sharpmake，以 `Engine/Builder/LimitlessBuilder.cs` 为入口生成 VS2022 工程。

### macOS

```bash
./GenerateProject.sh
```

> 首次使用前请确保已初始化子模块：
> ```bash
> git submodule update --init --recursive
> ```

## 6. 模块依赖拓扑图

```text
                    +---------+
                    |  Glm    |
                    +----+----+
                         |
                    +----v----+
                    | Spdlog  |
                    +----+----+
                         |
       +-----------------+------------------+
       |                 |                  |
  +----v----+       +----v----+      +------v------+
  |  Core   |<------+  RAL    |      |   Vulkan    |
  +----+----+       +----+----+      +-------------+
       |                 |
       |            +----v----+
       |            |   RFG   |
       |            +----+----+
       |                 |
       |            +----v------+
       +----------->| Renderer  |
       |            +----+------+
       |                 |
       |            +----v------+
       +----------->|   World   |
       |            +----+------+
       |                 |
       +-----------------+------------------+
                         |
                    +----v----+
                    | Launch  |
                    +----+----+
                         |
                    +----v-----------+
                    | LimitlessEngine |  <-- 主可执行文件 (Exe)
                    |  (Editor/Game)  |
                    +-----------------+

图例说明：
  --->  表示 public dependency (模块链接依赖)
  LimitlessEngine 在 Editor 模式下输出 LimitlessEditor.exe
  LimitlessEngine 在 Game 模式下输出 LimitlessGame.exe
```

### 依赖明细

| 模块 | 直接依赖 |
|------|----------|
| Core | Glm, Spdlog |
| RAL | Core, Spdlog, Vulkan |
| RFG | Core, RAL |
| Renderer | Core, RAL, RFG |
| World | Core, RAL, Renderer |
| Launch | Core, Spdlog, RAL, RFG, Renderer, World |
| LimitlessEngine (主工程) | Core, Launch, RAL, RFG*(仅依赖不链接), Renderer, World |

## 7. 命名规范

引擎采用 **Unreal Engine (UE) 风格** 命名规范，核心规则如下：

| 前缀 | 含义 | 示例 |
|------|------|------|
| `F` | **Class** (普通类 / 结构体) | `FNonCopyable`、`FPlatformWindow`、`FRALBuffer` |
| `T` | **Template** (模板类 / 泛型) | `TVulkanResourceBase` |
| `E` | **Enum** (枚举类型) | `ERALPlatform`、`EPixelFormat`、`EQueueType` |
| `I` | Interface (接口，项目中暂未广泛使用) | — |

### 其他约定

- 类型别名：使用 `int8`、`int32`、`uint32`、`float32` 等明确位宽的别名。
- 宏命名：全大写 + 下划线，如 `LE_SMALL_NUMBER`、`LE_KINDA_SMALL_NUMBER`。
- 模块 API 宏：由构建系统自动生成，格式为 `[MODULE_NAME]_API` (如 `CORE_API`、`RAL_API`)。
- 文件/目录：模块内按 `Public/` 与 `Private/` 分离接口与实现；头文件按子系统分目录存放 (如 `RAL/`、`Renderer/`)。
