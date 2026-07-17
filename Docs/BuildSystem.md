# Limitless Engine 构建系统设计文档

## 1. 构建系统概述

Limitless Engine 使用 [Sharpmake](https://github.com/ubisoft/Sharpmake) 作为元构建系统（Meta-Build System）。Sharpmake 是 Ubisoft 开源的 C# 项目生成工具，通过编写 C# 脚本来描述项目结构、编译选项和依赖关系，最终生成 Visual Studio 解决方案（`.sln`）和项目文件（`.vcxproj`），或 Xcode 项目。

**选用理由**

- 以代码方式描述构建配置，版本控制友好，避免手动维护庞大且易冲突的 XML 项目文件。
- 原生支持多平台、多配置矩阵，适合同时输出 Win64（VS2022）与 macOS（Xcode）工程。
- 依赖传播机制（`PublicDependency` / `PrivateDependency`）与模块化的游戏引擎架构天然契合。

---

## 2. 构建流程

整体流程如下：

```
.Build.cs 脚本 + LimitlessBuilder.cs
        |
        v
Sharpmake.Application (dotnet build & run)
        |
        +---> Limitless_vs2022_win64.sln  +  .vcxproj
        +---> Limitless_xcode_mac.sln     +  .xcodeproj
```

### 2.1 四步生成流程

| 步骤 | 动作 | 说明 |
|------|------|------|
| 1 | 模块自发现 | 扫描 `Engine/Source` 下所有 `*.Build.cs`，生成 `Engine/Builder/GeneratedIncludeFiles.cs` |
| 2 | 工具链校验 | 检查 `External/Sharpmake` 子模块存在，且系统已安装 `dotnet` SDK |
| 3 | 编译 Sharpmake | `dotnet build External/Sharpmake/Sharpmake.Application/Sharpmake.Application.csproj` |
| 4 | 执行生成 | 以 `Engine/Builder/LimitlessBuilder.cs` 为入口，调用 `Sharpmake.Application` 输出工程文件 |

生成后的工程文件默认放置在：

- 解决方案：`Solution/Limitless_<DevEnv>_<Platform>.sln`
- 项目文件：`Temp/ProjectFiles/`
- 编译输出：`Engine/Binaries/<Platform>/`
- 中间文件：`Temp/Obj/<ProjectName>/`

---

## 3. 目标配置矩阵

`TargetRule` 定义了四个维度：`Platform` × `DevEnv` × `Optimization` × `TargetType`。

当前支持的完整组合共 **8 种**：

| # | Platform | DevEnv | Optimization | TargetType | 配置名示例 |
|---|----------|--------|--------------|------------|------------|
| 1 | Win64 | VS2022 | Debug | Game | Debug Game |
| 2 | Win64 | VS2022 | Debug | Editor | Debug Editor |
| 3 | Win64 | VS2022 | Release | Game | Release Game |
| 4 | Win64 | VS2022 | Release | Editor | Release Editor |
| 5 | Mac | Xcode | Debug | Game | Debug Game |
| 6 | Mac | Xcode | Debug | Editor | Debug Editor |
| 7 | Mac | Xcode | Release | Game | Release Game |
| 8 | Mac | Xcode | Release | Editor | Release Editor |

**TargetType 语义**

- `Game`：Monolithic 模式，所有模块静态链接为单一可执行文件，适合发布。
- `Editor`：Modular 模式，Win64 下引擎模块编译为 DLL，支持热更与开发迭代。

---

## 4. 核心脚本说明

### 4.1 LimitlessBuilder.cs

- **职责**：Sharpmake 入口脚本。
- **关键行为**：
  - 通过 `[module: Sharpmake.Include(...)]` 引入所有模块规则与工具类。
  - 设置 Windows SDK 版本为 `10.0.22621.0`。
  - 在 `SharpmakeMain` 中调用 `arguments.Generate<SolutionRule>()` 触发整个解决方案生成。

### 4.2 Solution.cs

- **类**：`SolutionRule : Sharpmake.Solution`
- **职责**：定义解决方案级配置与包含的项目。
- **关键点**：
  - 名称固定为 `Limitless`。
  - 文件名格式：`[solution.Name]_[target.DevEnv]_[target.Platform]`，例如 `Limitless_vs2022_win64.sln`。
  - 配置名由 `Optimization` + `TargetType` 拼接（如 `Debug Editor`）。
  - 每个配置均添加 `LimitlessProject` 作为唯一主项目。

### 4.3 LimitlessProject.cs

- **类**：`LimitlessProject : Sharpmake.Project`
- **职责**：定义最终可执行文件（`LimitlessEditor` / `LimitlessGame`）。
- **关键点**：
  - `SourceRootPath` 指向 `Engine/Source/Runtime/Launch`，即主入口代码所在模块。
  - Win64 输出类型为 `Exe`，并指定子系统为 `Windows`；macOS 目标架构为 `arm64`。
  - 通过 `WITH_EDITOR=1/0` 宏区分 Editor 与 Game 构建。
  - 显式依赖六个引擎模块：`Core`, `Launch`, `RAL`, `RFG`, `Renderer`, `World`。其中 `RFG` 使用 `DependencySetting.DefaultWithoutLinking`，表示包含其头文件与编译依赖，但不将其库链接到最终 Exe（由 `Launch` 自身或其他模块按需链接）。

### 4.4 Module.cs

- **类**：`ModuleRule : Sharpmake.Project`
- **职责**：所有引擎源码模块的基类。
- **关键行为**：
  - `SourceRootPath` 自动设为 `.Build.cs` 所在目录。
  - 自动向 `IncludePaths` 添加 `Public` 与 `Private` 子目录。
  - 根据平台注入 `PLATFORM_WINDOWS=1` 或 `PLATFORM_MAC=1`。
  - **API 宏自动导出**：见第 6 节。

- **类**：`ThirdPartyModuleRule : Sharpmake.Project`
- **职责**：第三方库模块的基类。
- **关键行为**：
  - `Output` 默认为 `None`（纯头文件库不参与链接）。
  - 不生成独立的编译输出目录，仅提供 Include 路径、宏定义与链接库配置。

### 4.5 Target.cs

- **枚举**：`TargetType`（`Game`, `Editor`）
- **类**：`TargetRule : Sharpmake.Target`
- **职责**：扩展 Sharpmake 原生的 `Target`，增加 `TargetType` 维度，使配置矩阵支持 Editor / Game 两种构建模式。

---

## 5. 模块规则

### 5.1 源码模块（ModuleRule）

| 属性 | 默认值/行为 |
|------|-------------|
| `SourceRootPath` | `.Build.cs` 所在目录 |
| `SourceFilesExtensions` | 额外包含 `.cs`（Sharpmake 脚本本身） |
| `IncludePaths` | `Public/`, `Private/` |
| `Output` | Editor + Win64 → `Dll`；其他 → `Lib` |
| `SolutionFolder` | `Programs` |

### 5.2 第三方模块（ThirdPartyModuleRule）

| 属性 | 默认值/行为 |
|------|-------------|
| `Output` | `None`（不编译为独立库） |
| `ProjectPath` | 解决方案根目录（不放入 `Temp/ProjectFiles`） |
| `SolutionFolder` | `Programs` |

**典型第三方模块差异**

- **Glm**：纯头文件库，`Output = None`，排除所有 `.cpp`，仅暴露 `include` 目录。
- **Spdlog**：编译为静态库，`Output = Lib`，定义 `SPDLOG_COMPILED_LIB`；macOS 额外定义 `SPDLOG_NO_EXCEPTIONS`。
- **Vulkan**：`Output = None`，仅提供头文件与 SDK 导入库（`vulkan-1.lib` / `libvulkan`）的路径和链接配置。Win64 默认回退路径为 `C:\VulkanSDK\1.3.275.0`；macOS 依次回退 `VULKAN_SDK`、`/opt/homebrew/lib`、`/usr/local/lib`。

---

## 6. API 宏导出机制

Win64 Editor 配置下，引擎模块以 DLL 形式编译，需使用 `__declspec(dllexport/dllimport)` 控制符号可见性。构建系统通过 `ModuleRule` 自动完成宏定义，无需在每个源文件中手动区分。

**规则（以 `Core` 模块为例）**

| 配置 | `CORE_API` 定义（本模块编译时） | `CORE_API` 定义（依赖方使用） |
|------|-------------------------------|------------------------------|
| Editor Win64 | `__declspec(dllexport)` | `__declspec(dllimport)` |
| Game / macOS | （空） | （空） |

实现代码位于 `Module.cs`：

```csharp
string apiMacro = this.Name.ToUpper() + "_API";
if (target.TargetType == TargetType.Editor && target.Platform == Platform.win64)
{
    conf.Output = Configuration.OutputType.Dll;
    conf.Defines.Add($"{apiMacro}=__declspec(dllexport)");
    conf.ExportDefines.Add($"{apiMacro}=__declspec(dllimport)");
}
else
{
    conf.Output = Configuration.OutputType.Lib;
    conf.Defines.Add($"{apiMacro}=");
    conf.ExportDefines.Add($"{apiMacro}=");
}
```

**例外**：`Launch` 模块无论配置如何，始终输出为 `Lib`，因为它被 `LimitlessProject` 直接包含源代码并编译为最终 Exe，不单独生成 DLL。

---

## 7. 各模块依赖关系表

### 7.1 引擎模块（ModuleRule）

```
LimitlessProject (Exe)
  ├── Core
  │     └── Glm, Spdlog
  ├── Launch
  │     ├── Core, Spdlog
  │     ├── RAL
  │     ├── RFG
  │     ├── Renderer
  │     └── World
  ├── RAL
  │     ├── Core
  │     ├── Spdlog
  │     └── Vulkan
  ├── RFG
  │     ├── Core
  │     └── RAL
  ├── Renderer
  │     ├── Core
  │     ├── RAL
  │     └── RFG
  └── World
        ├── Core
        ├── RAL
        └── Renderer
```

> `LimitlessProject` 对 `RFG` 使用 `DefaultWithoutLinking`，意味着 `RFG` 的编译产物不会被链接到最终 Exe，但其 Public Include 路径和宏仍对 `LimitlessProject` 可见。

### 7.2 第三方模块（ThirdPartyModuleRule）

| 模块 | 类型 | 说明 |
|------|------|------|
| Glm | 头文件库 | `include` 目录，定义 `LE_USE_GLM` |
| Spdlog | 静态库 | `include` 目录，定义 `SPDLOG_COMPILED_LIB` |
| Vulkan | 头文件 + SDK 导入库 | `Include` 目录，链接系统 Vulkan SDK |

---

## 8. 项目生成步骤

### Windows（Visual Studio 2022）

在项目根目录执行：

```batch
GenerateProject.bat
```

脚本内部流程：
1. 调用 `GenerateIncludes.ps1` 扫描并生成 `GeneratedIncludeFiles.cs`。
2. 检查 `External/Sharpmake` 子模块与 `dotnet` 环境。
3. 编译 `Sharpmake.Application`。
4. 运行 Sharpmake，生成 `Solution/Limitless_vs2022_win64.sln` 及配套 `.vcxproj`。

### macOS（Xcode）

在项目根目录执行：

```bash
./GenerateProject.sh
```

脚本内部流程与 Windows 类似，最终生成 `Solution/Limitless_xcode_mac.sln` 及 Xcode 工程。注意 `.sh` 脚本中的模块发现使用 `find` 命令替代 PowerShell。

---

## 9. 已知问题

1. **全部 PublicDependency 导致过度传播**

   当前所有模块依赖均使用 `AddPublicDependency`，导致传递性依赖无差别暴露。例如依赖 `RAL` 的模块会自动获得 `RAL` 的所有 Public 依赖（`Core`, `Spdlog`, `Vulkan`）的 Include 路径和宏，即使它们并不直接需要 Vulkan。应将部分依赖改为 `AddPrivateDependency`，仅将必要接口保留在 Public 层。

2. **无预编译头（PCH）配置**

   当前 `ModuleRule` 中未配置预编译头。随着引擎规模扩大，建议为核心模块（如 `Core`、`RAL`）引入 PCH，以减少重复编译开销。Sharpmake 支持通过 `conf.PrecompSource` 与 `conf.PrecompHeader` 进行配置。

3. **Launch 模块符号特殊处理**

   `Launch` 被硬编码为始终输出 `Lib`，且其 `LAUNCH_API` 宏定义为空。这与其它模块的自动 DLL 逻辑不一致，需开发者手动确保 `LimitlessProject` 正确包含 `Launch` 的源文件以避免链接错误。
