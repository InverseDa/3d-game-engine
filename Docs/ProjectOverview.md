# Limitless Engine 项目总览

## 1. 项目简介

**Limitless Engine** 是一款开源 3D 游戏引擎，基于 **C++17** 与 **Vulkan** 构建，面向现代高性能游戏开发。引擎采用模块化 Runtime 架构，代码按职责划分为独立模块，通过 LimitlessBuilder（LB）构建。

| 属性 | 说明 |
|------|------|
| 语言标准 | C++17 |
| 图形 API | Vulkan (跨平台) |
| 构建系统 | LimitlessBuilder + Node + Ninja |
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
│   ├── Builder/                # 构建工具
│   │   └── LimitlessBuilder/   # TypeScript 构建系统
│   ├── Content/                # 引擎内容资源
│   └── Source/
│       ├── Runtime/            # 引擎运行时模块 (核心)
│       └── ThirdParty/         # 第三方库
│           ├── Glm/
│           ├── Spdlog/
│           └── Vulkan/
├── External/                   # 外部依赖
├── GenerateProject.bat         # Windows LB 工程生成入口
├── LICENSE
└── README.md
```

## 3. 运行时模块一览表

`Engine/Source/Runtime` 为引擎核心源码目录，各模块职责如下：

| 模块 | 全称 | 职责 | 源文件数 | 依赖 |
|------|------|------|----------|------|
| **Core** | Core | 基础类型、自有数学值类型、日志与引擎通用工具 | 5 | Spdlog |
| **Platform** | Platform | 窗口所有权、消息泵、平台事件、surface 与单调时间边界 | 11 | Core |
| **Application** | Application | `FEngineLoop`、Application 生命周期、确定性 runtime module registry、显式帧阶段与时间上下文 | 7 | Core, Platform |
| **Asset** | Asset | 持久 Asset/Type ID、typed runtime handle、虚拟 soft path 与同步 registry；定义 opaque Mesh/Material 逻辑类型，不持有 GPU payload | 8 | Core |
| **Reflection** | Reflection | 稳定 type/property IDs、owned runtime metadata registry、property bag、结构化二进制序列化与显式 provider 生命周期 | 14 | Core |
| **RHI** | Render Hardware Interface | 渲染硬件接口，封装 Vulkan 底层对象 (Buffer、Texture、Pipeline、Swapchain、CommandList 等) | 27 | Core, Platform, Spdlog, Vulkan |
| **RFG** | Render Frame Graph | 渲染帧图系统，负责 Pass 编排、资源屏障规划、依赖分析与执行调度 | 42 | Core, RHI |
| **Renderer** | Renderer | 高层渲染器，定义 RenderPass / RenderPipeline / RenderView、Mesh/Material GPU payload 与正式 BasePass→Composite 管线，提供 CPU 资产到 GPU payload 的构造 | 24 | Core, Asset, RHI, RFG；私有 AssetFormat |
| **World** | World | Entity 生命周期、typed contiguous component storage、Transform hierarchy/dirty world-matrix cache、确定性 tick/deferred command boundary、单线程 native fixed/variable system schedule、Mesh/Material typed handles、primary camera 逻辑组件，以及显式 `FWorld` Reflection P0 provider | 10 | Core, Asset, Reflection |
| **WorldRenderer** | World Renderer | 从 committed World 与同步 Asset registry 构建 frame-owned immutable render snapshot，选择相机、剔除并确定性排序 indexed mesh proxies | 4 | Core, Asset, Renderer, World |
| **FileSystem** | File System | 有界读文件、受限挂载目录访问、同目录原子写入 | 2 | Core |
| **AssetFormat** | Asset Format | 版本化 Mesh、Material、CPU Texture 的数据和编解码 | 2 | Core, Asset |
| **AssetLoading** | Asset Loading | 挂载索引、同步加载、会话内 registry 缓存和 provider 生命周期 | 2 | Core, Asset, AssetFormat, FileSystem |
| **Scene** | Scene Persistence | JSON 场景、持久实体 ID、候选 World 构造与保存恢复 | 4 | Core, Asset, World, FileSystem |
| **DemoApplication** | Demo Application | 从磁盘加载场景与资产，组装共享 BasePass pipeline，提取场景快照并呈现 | 4 | Application, Asset, AssetLoading, Core, Platform, Spdlog, RHI, RFG, Reflection, Renderer, Scene, World, WorldRenderer |
| **Launch** | Launch | `WinMain` / `main` 与通用 lifecycle bootstrap | 2 | Core, DemoApplication |

> **注**：文件数统计包含 `.cpp` / `.h` / `.hpp` / `.mm` 等源码文件。

## 4. 构建系统说明

### 4.1 LimitlessBuilder

LB 递归加载模块目录内的 `Build.ts`，构建依赖图和 Ninja 编译动作。开发者不维护集中式模块 include 列表；`GenerateProject.bat` 可生成 Rider / Visual Studio 使用的 Makefile 工程。

### 4.2 支持的构建配置

配置维度为 **Platform × Optimization × TargetType**，共 8 种组合：

| 维度 | 可选值 |
|------|--------|
| Platform | `win64` (VS2022)、`mac` (Xcode, arm64) |
| Optimization | `Debug`、`Release` |
| TargetType | `Editor`、`Game` |

生成的 Solution 文件：
```
Solution/LimitlessEngine.sln
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
1. **发现模块** —— 扫描全部 `Build.ts`。
2. **发现工具链** —— 解析 Node、Ninja、Visual Studio 与 Windows SDK。
3. **生成工程** —— 由 LB 生成 `LimitlessEngine.sln` 与 `.vcxproj`。

## 6. 模块依赖拓扑图

```text
Spdlog ---> Core ---> Platform ---> Application
              | \
              |  +-------> Reflection ------+
              +----------> Asset --------+   |
                              |          |   |
                              +------> Renderer
                              |          |   |
                              +------> World <+
                                         | \
                              Renderer --+--> WorldRenderer

Core + Platform + Spdlog + Vulkan ---> RHI ---> RFG ---> Renderer

Application / Platform / Asset / RHI / RFG / Reflection / Renderer / World / WorldRenderer / Spdlog
                         |
                         v
                  DemoApplication
                         |
                         v
                       Launch
                         |
                         v
                  LimitlessEditor/Game

图例说明：
  --->  表示 public dependency (模块链接依赖)
  LimitlessEngine 在 Editor 模式下输出 LimitlessEditor.exe
  LimitlessEngine 在 Game 模式下输出 LimitlessGame.exe
```

### 依赖明细

| 模块 | 直接依赖 |
|------|----------|
| Core | Spdlog |
| Platform | Core |
| Application | Core, Platform |
| Asset | Core |
| RHI | Core, Platform, Spdlog, Vulkan |
| RFG | Core, RHI |
| Renderer | Core, Asset, RHI, RFG；私有 AssetFormat |
| Reflection | Core |
| World | Core, Asset, Reflection |
| WorldRenderer | Core, Asset, Renderer, World |
| DemoApplication | Application, Asset, Core, Platform, Spdlog, RHI, RFG, Reflection, Renderer, World, WorldRenderer |
| Launch | Core, DemoApplication |
| LimitlessEngine (主工程) | 由 Launch 入口沿 public link graph 到达全部运行时依赖 |

## 7. 命名规范

P0 的语言、namespace、Core 类型、ownership 与容器规范由
[ADR-0001: P0 Modern C++ Language, Core Types, and Containers](ADR/0001-modern-cpp-core-types-and-containers.md)
统一约束。当前代码仍处于迁移前/迁移中状态：最终根 namespace 直接为 `LE`，不提供
`Limitless` alias；通用容器位于 `LE`，数学位于 `LE::Math`，领域 namespace 只在确有
语义时保持浅层使用。

已落地的 allocator、containers、String 契约见 [Core allocation and containers](CoreContainers.md)；
跨 DLL creator-side destruction、`UniquePtr`/`SharedPtr`/`WeakPtr` 与 `Function` 规则见
[Core ownership and callable utilities](CoreOwnership.md)。

引擎采用 **Unreal Engine (UE) 风格** 命名规范，核心规则如下：

| 前缀 | 含义 | 示例 |
|------|------|------|
| `F` | 普通 native/value/RAII 类型（非 tracing GC） | `FNonCopyable`、`FPlatformWindow`、`FRHIBuffer` |
| `L` | 未来 managed lifecycle / tracing GC 对象 | 暂不随意新增 |
| `T` | **Template** (模板类 / 泛型) | `TVulkanResourceBase` |
| `E` | **Enum** (枚举类型) | `ERHIPlatform`、`EPixelFormat`、`EQueueType` |
| `I` | Interface (接口，项目中暂未广泛使用) | — |

其中 `F` 表示不参与未来 tracing GC 的普通 native/value/RAII 类型；`L` 保留给将来受
managed lifecycle/GC 契约机械约束的对象。P0 保留既有类型、函数、成员、常量和布尔命名，
基础设施迁移不夹带 cosmetic rename。

### 变量与函数

- 函数名和普通变量名默认使用大驼峰（PascalCase），每个单词首字母大写。局部变量、成员和形参都遵循这一约定，例如 `FrameIndex`、`TickState` 和 `OutFlushResult`。
- `bool` 变量在大驼峰名称前加小写 `b`，包括局部变量、成员和形参，例如 `bTransformsUpdated`、`bActive` 和 `bEnabled`。
- 返回 `bool` 的函数仍按函数命名，例如 `IsValid()` 和 `HasTransform()`。

### 其他约定

- 类型别名：使用 `int8`、`int32`、`uint32`、`float32` 等明确位宽的别名。
- 宏命名：全大写 + 下划线，如 `LE_SMALL_NUMBER`、`LE_KINDA_SMALL_NUMBER`。
- 模块 API 宏：由构建系统自动生成，格式为 `[MODULE_NAME]_API` (如 `CORE_API`、`RHI_API`)。
- 文件/目录：模块内按 `Public/` 与 `Private/` 分离接口与实现；头文件按子系统分目录存放 (如 `RHI/`、`Renderer/`)。
