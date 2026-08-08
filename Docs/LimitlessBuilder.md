# LimitlessBuilder 设计文档

> 状态：设计草案 / 待评审
> 关联文档：[BuildSystem.md](./BuildSystem.md)（当前 Sharpmake 实现的归档说明）

## 1. 背景与动机

当前引擎使用 Sharpmake（C#）作为元构建系统，痛点集中在：

- **启动链路长**：每次 `GenerateProject.bat` 都要先 `dotnet build` Sharpmake.Application 再跑，冷启动 5-10 秒。
- **C# 反射调试体验差**：`[Configure]` attribute + `[module: Sharpmake.Include("...")]` 字符串 include 报错信息晦涩。
- **跨平台脚本割裂**：Win 用 `.bat` + PowerShell，Mac 用 `.sh`，模块发现逻辑两份。
- **生成的是 vcxproj/xcodeproj**：每次工程变更都要重新生成 IDE 工程文件，且 Mac 上生成 xcodeproj 极慢。
- **被外部工具绑定**：Sharpmake 是子模块，版本跟随上游，自定义能力受限。

**LimitlessBuilder 的目标**：用 TS 自己实现一套轻量元构建系统，编译后端用 ninja（先期），保留接口将来可切换为自实现的 ActionGraph 后端（UBT CLI 风格）。

## 2. 设计原则

| 原则 | 含义 |
|------|------|
| 配置即代码 | `Build.ts` 是真正的 TS，有类型、有 IDE 补全，能写条件逻辑 |
| 后端可插拔 | 核心产出 `BuildAction[]` IR，后端是 Strategy，ninja 是首个实现 |
| 不生成 IDE 工程 | 只产出 `build.ninja` + `compile_commands.json`，用 VSCode/CLion/Rider 开发 |
| 增量编译靠内容哈希 | 不依赖文件 mtime，避免跨平台时间戳问题 |
| 单一二进制分发 | 用 `bun build --compile` 或 `pkg` 打成单 exe，开发者无需装 Node |
| 唯一构建入口 | LB 是唯一构建系统，模块和工程生成均由它处理 |

## 3. 整体架构

四层结构，详见架构图。

```
┌─────────────────────────────────────────────────────┐
│ 输入层                                               │
│   Build.ts   *.target.ts   limitless.config.ts      │
└──────────────────────┬──────────────────────────────┘
                       ▼
┌─────────────────────────────────────────────────────┐
│ 核心管线                                             │
│   Loader → DependencyGraph → IR Builder             │
│            (+ API 宏推导)        (BuildAction DAG)  │
└──────────────────────┬──────────────────────────────┘
                       ▼
┌─────────────────────────────────────────────────────┐
│ IBackend 接口  (Strategy 接缝)                      │
│   generate(ir, target) → BuildResult                │
└──────┬──────────────────────────────────┬───────────┘
       ▼                                  ▼
┌──────────────────┐         ┌────────────────────────┐
│ NinjaBackend     │         │ ActionGraphBackend     │
│ (当前)           │         │ (未来，UBT CLI 风格)   │
└──────────────────┘         └────────────────────────┘
```

## 4. 输入层：`Build.ts` DSL

### 4.1 替代 `.Build.cs` 的模块描述

以 `Engine/Source/Runtime/Core/Build.ts` 为例（对照 `Core.Build.cs`）：

```typescript
import {
    ModuleBuild,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class CoreBuild extends ModuleBuild {
    public readonly Name = "Core";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Spdlog");
    }
}
```

对照原 C# 版本，TS 版的优势：
- 原生 `import` 替代 `[module: Sharpmake.Include("...")]` 字符串 include
- `ModuleBuild` 基类让模块可拥有辅助方法和局部状态，避免配置对象承担逻辑
- 配置对象 `Configuration` 是强类型，IDE 补全完整
- 条件逻辑直接写 TS，无需 C# 反射

### 4.2 第三方库模块

以 `Engine/Source/ThirdParty/Vulkan/Build.ts` 为例（替代 `Vulkan.Build.cs`）：

```typescript
import * as Fs from "node:fs";
import * as Path from "node:path";
import {
    ModuleBuild,
    OutputType,
    type ModuleConfiguration,
    type Target,
} from "../../../Builder/Runtime/Api.ts";

export default class VulkanBuild extends ModuleBuild {
    public readonly Name = "Vulkan";
    public override readonly ThirdParty = true;

    public Configure(Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.Output = OutputType.None;
        Configuration.IncludePaths.push("[module.SourceRoot]/Include");

        const Sdk = process.env.VULKAN_SDK;
        const LibraryPath = Sdk ? Path.join(Sdk, Target.Platform === "Win64" ? "Lib" : "lib") : "";
        if (LibraryPath && Fs.existsSync(LibraryPath)) {
            Configuration.LibraryPaths.push(LibraryPath);
            Configuration.LibraryFiles.push(Target.Platform === "Win64" ? "vulkan-1.lib" : "vulkan");
        }
    }
}
```

### 4.3 Target 配置

新建 `Engine/Builder/Targets/Limitless.target.ts`（替代 `Target.cs` + `Solution.cs`）：

```typescript
import { defineTarget, Platform, DevEnv, Optimization, TargetType } from "@limitless/builder";

export default defineTarget({
  name: "Limitless",
  modules: ["Core", "RAL", "RFG", "Renderer", "World", "Launch"],
  entryModule: "Launch",  // 主可执行文件的源码模块

  matrix: [
    { platform: Platform.Win64, optimization: Optimization.Debug | Optimization.Release,
      targetType: TargetType.Game | TargetType.Editor },
    // Mac 只在 macOS 上生成
    ...(process.platform === "darwin" ? [{
      platform: Platform.Mac, optimization: Optimization.Debug | Optimization.Release,
      targetType: TargetType.Game | TargetType.Editor,
    }] : []),
  ],

  outputName: (target) => target.targetType === TargetType.Editor ? "LimitlessEditor" : "LimitlessGame",
});
```

### 4.4 自定义动作

生成代码、shader 编译等步骤通过 `ModuleConfiguration.CustomActions` 声明，不能放入
`CustomProperties`：

```typescript
Configuration.CustomActions.push({
    Id: "GenerateBindings",              // 模块内稳定 ID
    Inputs: ["[module.SourceRoot]/Api.idl"],
    ImplicitInputs: ["[module.SourceRoot]/Generator.config"],
    Outputs: [
        "[module.Generated]/Api.generated.cpp",
        "[module.Generated]/Api.generated.h",
    ],
    Command: [process.execPath, "tools/generate.mjs", "[module.SourceRoot]/Api.idl"],
    WorkingDirectory: "[engine.Root]",
    DependsOn: ["PrepareSchema"],         // 短 ID 指向同模块 custom action
    Description: "GEN Api bindings",
    RunBeforeCompile: true,
});
```

IR ID 为 `<Module>::custom::<Id>`；`DependsOn` 也可写完整 IR ID 以引用其它模块的动作。
`DependsOn` 是动作排序关系，在 Ninja 中为 order-only（`||`），不会因依赖产物 mtime
变化而让 consumer 变脏。需要 timestamp 传播时，同时把 producer output 放进
`ImplicitInputs`（`|`）。`RunBeforeCompile` 会自动把该动作加入本模块所有 compile 的
`DependsOn`，并把它的 outputs 加入 compile 的 `ImplicitInputs`，适合首次生成头文件。

custom output 中的 `.cpp/.cc/.c/.mm/.m` 即使生成前不存在也会成为 compile action；
其它扩展名不会被误编译。`Output.None` 模块仍会生成 custom action。动作 ID、output
producer 和依赖环在完整 action graph 上统一校验，不依赖声明数组的先后顺序。

### 4.5 路径变量

保留 Sharpmake 风格的路径变量，但用 TS 模板字符串替代方括号语法：

| 变量 | 含义 |
|------|------|
| `[module.SourceRoot]` | 当前模块源码根目录 |
| `[project.SourceRootPath]` | 兼容旧配置的当前模块源码根目录 |
| `[engine.Root]` | 项目根目录 |
| `[engine.Source]` | `Engine/Source` |
| `[engine.Binaries]` | `Engine/Binaries/<Platform>` |
| `[engine.Temp]` | `Engine/Intermediate/Build/<Platform>/<Config>` |
| `[engine.Generated]` | `<engine.Temp>/Generated` |
| `[module.Generated]` | `<engine.Generated>/<ModuleName>` |

路径字段允许相对路径（相对模块根）并在 IR 中规范化为绝对路径。custom outputs 只允许
落在当前配置的 temp/generated 或当前平台 binaries 根内，`..` 逃逸会被拒绝。Command
是 argv token 数组而不是 shell 字符串；路径变量会展开，但 `/flag` 等普通 token 不会被
当成路径改写。

## 5. IR（中间表示）

核心数据结构是 `BuildAction`，所有后端都消费它：

```typescript
interface BuildAction {
  Id: string;                          // 唯一 ID（用于依赖引用）
  Type: ActionType;                    // Compile | Link | Archive | Custom
  Inputs: string[];                    // 输入文件绝对路径
  Outputs: string[];                   // 一个或多个输出文件绝对路径
  Command: string[];                   // executable + argv tokens
  WorkingDirectory: string;
  DependsOn: string[];                 // 依赖的其它 action id
  Description: string;                 // 给 ninja 进度条显示用
  ImplicitInputs?: string[];           // timestamp 依赖（如生成的头文件）
}

type ActionType = "compile" | "link" | "archive" | "custom";

interface BuildResult {
  Actions: BuildAction[];              // 校验并稳定拓扑排序后的动作列表
  CompileCommandsPath: string;         // compile_commands.json 输出路径
}
```

IR Builder 的职责：
1. 遍历依赖图，为每个模块的每个 `.cpp` 生成一个 `compile` action
2. 根据模块 `Output` 类型生成 `link`（DLL/EXE）或 `archive`（Lib）action
3. 处理 typed custom actions 及其 generated-file compile 集成
4. 对完整 action graph 校验 ID、producer、依赖和 cycle

## 6. IBackend 接口

```typescript
interface IBackend {
  readonly name: string;

  // 生成构建文件（如 build.ninja），不执行编译
  generate(ir: BuildAction[], target: Target, opts: GenerateOptions): Promise<BuildResult>;

  // 执行编译（可选，部分后端如 ninja 是 spawn 子进程）
  build?(result: BuildResult, opts: BuildOptions): Promise<number>;

  // 清理产物
  clean?(target: Target): Promise<void>;
}

function createBackend(name: "ninja" | "action-graph", config: BackendConfig): IBackend;
```

切换后端只需改 `limitless.config.ts` 里的 `backend: "ninja"` 一行。

## 7. NinjaBackend 实现要点

### 7.1 ninja 文件格式（极简）

ninja 文件由若干 `build` 语句组成，每个对应一个 `BuildAction`：

```ninja
# 自动生成，勿手改
ninja_required_version = 1.10

rule cc
  command = $cl /nologo /c $flags /Fo$out $in /Fd"$pdb"
  description = CC $out
  deps = msvc

rule link
  command = $link /nologo /OUT:$out $in $libs
  description = LINK $out

rule custom
  command = $Cmd
  description = $Desc
  restat = 1

build Temp/Win64/Debug/Core/Core.obj: cc Engine/Source/Runtime/Core/Private/Core.cpp
  flags = /std:c++17 /utf-8 /DDEBUG=1 ...
  cl = "C:/Program Files/.../cl.exe"
  pdb = "Temp/Win64/Debug/Core/Core.pdb"

build Engine/Binaries/Win64/Core.dll: link Temp/Win64/Debug/Core/Core.obj ...
  libs = vulkan-1.lib ...
```

每条 edge 会渲染全部 outputs、explicit inputs、`|` implicit inputs 和 `||` action-ID
dependencies。Backend 会先创建所有 output parent directories，custom edge 使用
`restat = 1`，并按 action 的 `WorkingDirectory` 执行 tokenized command。默认目标按
`<TargetName>::exe` 精确选择 resolved executable；不会从多输出 custom edge 猜测。
`compile_commands.json` 只包含 compile actions。

目前 custom action 的执行集成由 Ninja 后端负责。VCXProj/Xcode 生成器仍用于 IDE
浏览、索引和调用 LB，不在原生项目文件里重复表达 custom edge。

### 7.2 ninja 给你的免费能力

- 并行编译（`-j` 自动按 CPU 核数）
- 增量编译（基于文件 mtime + 可选内容哈希）
- 子进程池调度
- 跨平台（Win/Mac/Linux 同一份 `build.ninja`）

### 7.3 compile_commands.json

每个 `compile` action 同时写入一条 JSON 条目，给 clangd/VSCode/CLion 用：

```json
[
  {
    "directory": "E:/Projects/3d-game-engine",
    "command": "cl.exe /nologo /c /std:c++17 /utf-8 ... Engine/Source/Runtime/Core/Private/Core.cpp",
    "file": "Engine/Source/Runtime/Core/Private/Core.cpp"
  }
]
```

## 8. 平台与工具链抽象

```typescript
interface IToolchain {
  readonly platform: Platform;
  findCompiler(): string;              // cl.exe / clang++
  findLinker(): string;                // link.exe / ld
  findArchiver(): string;              // lib.exe / ar
  getSystemIncludePaths(): string[];
  getSystemLibPaths(): string[];
  makeCompileCommand(action: BuildAction): string[];
  makeLinkCommand(action: BuildAction): string[];
}

class MSVCToolchain implements IToolchain { ... }   // Win64
class ClangToolchain implements IToolchain { ... }  // Mac/Linux
```

工具链发现策略：
- **Win64**：通过 `vswhere.exe` 找到 VS2022 安装路径，再加载 MSVC 环境（`vcvarsall.bat` 的等价逻辑，用 TS 重写）
- **Mac**：直接用 `xcrun --find clang++`，配合 `VULKAN_SDK` 环境变量

## 9. 增量编译机制

**两种模式，按后端选择：**

| 后端 | 增量机制 | 说明 |
|------|----------|------|
| NinjaBackend | ninja 内置 mtime + `deps = msvc` 解析 `.d` 文件 | 简单可靠，足够日常用 |
| ActionGraphBackend (未来) | 自管 `.buildcache` SQLite，内容哈希 | 跨平台一致，不受 mtime 漂移影响 |

`.buildcache` schema 草案：

```
table files (
  path TEXT PRIMARY KEY,
  hash TEXT NOT NULL,            -- SHA-256 of content
  mtime INTEGER NOT NULL,
  size INTEGER NOT NULL
)

table actions (
  id TEXT PRIMARY KEY,
  inputs_hash TEXT NOT NULL,     -- 所有输入文件哈希的聚合
  command_hash TEXT NOT NULL,    -- 命令行哈希（命令变了也要重编）
  last_built INTEGER NOT NULL
)
```

## 10. CLI 设计

```
limitless-builder <command> [options]

Commands:
  generate [target]              生成 build.ninja + compile_commands.json
  build    [target] [--config]   生成并执行编译
  clean    [target]              清理产物
  list     modules|targets       列出模块/目标
  graph    [target] --dot        导出依赖图（DOT 格式）

Options:
  --platform <win64|mac>
  --config <debug|release>
  --type <game|editor>
  --backend <ninja|action-graph>  覆盖 config 里的默认后端
  --verbose
```

典型用法：

```bash
# 生成 ninja 文件
limitless-builder generate --platform win64 --config debug --type editor

# 编译
limitless-builder build --platform win64 --config debug --type editor

# 一行搞定（生成 + 编译）
limitless-builder build --platform win64 --config debug --type editor

# 看 Core 模块依赖了谁
limitless-builder graph --module Core --dot | dot -Tsvg > core-deps.svg
```

## 11. 目录结构

```
Engine/Builder/
├── LimitlessBuilder.bat              # Windows 入口；PATH、注册表或环境变量发现 Node
├── LimitlessBuilder.sh
├── package.json
├── tsconfig.json
├── Runtime/
│   └── Api.ts                        # Build.ts 的公开 API
└── Source/
    ├── Backend/                      # Ninja 后端及接口
    ├── Build/                        # IR、源文件扫描、API 宏
    ├── Cli/                          # 命令行入口
    ├── Configuration/                # ModuleBuild、Target、共享模型
    ├── Discovery/                    # Build.ts 和 target 的加载
    ├── Graph/                        # 依赖图
    ├── Project/                      # 项目路径、Visual Studio 解决方案生成
    ├── Toolchain/                    # MSVC、vswhere、Ninja 发现
    └── Utilities/                    # 日志等通用工具

# 用户的 Build.ts 文件（替代 .Build.cs）
Engine/Source/Runtime/Core/Build.ts
Engine/Source/Runtime/RAL/Build.ts
Engine/Source/Runtime/Launch/Build.ts
Engine/Source/Runtime/Renderer/Build.ts
Engine/Source/Runtime/RFG/Build.ts
Engine/Source/Runtime/World/Build.ts
Engine/Source/ThirdParty/Glm/Build.ts
Engine/Source/ThirdParty/Spdlog/Build.ts
Engine/Source/ThirdParty/Vulkan/Build.ts
```

Windows IDE 未继承终端 PATH 时，入口还会读取 Windows 持久化的用户/系统 PATH。
仍无法发现时，可在 IDE 的构建环境中设置
`LIMITLESS_BUILDER_NODE=<node.exe 的绝对路径>`；该显式配置优先于所有自动发现方式。

## 12. API 宏自动化（关键逻辑）

这是从 Sharpmake `Module.cs` 第 87-103 行迁移过来的核心逻辑：

```typescript
// api-macro.ts
export function deriveApiMacro(
  moduleName: string,
  target: Target
): { selfDefine: string; exportDefine: string; output: OutputType } {
  const macro = `${moduleName.toUpperCase()}_API`;

  // Launch 始终是 Lib，因为被主工程直接编进 Exe
  if (moduleName === "Launch") {
    return { selfDefine: "", exportDefine: "", output: OutputType.Lib };
  }

  // Editor + Win64 = DLL，需要 dllexport/dllimport
  if (target.targetType === TargetType.Editor && target.platform === Platform.Win64) {
    return {
      selfDefine: `${macro}=__declspec(dllexport)`,
      exportDefine: `${macro}=__declspec(dllimport)`,
      output: OutputType.Dll,
    };
  }

  // 其它情况（Game 或 Mac）= 静态 Lib，API 宏为空
  return { selfDefine: "", exportDefine: "", output: OutputType.Lib };
}
```

## 13. 与 UBT 的对比

| 维度 | UnrealBuildTool | LimitlessBuilder |
|------|-----------------|---------------|
| 语言 | C# (.NET) | TypeScript (Node/Bun) |
| 配置文件 | `.Build.cs` / `.Target.cs` | `Build.ts` / `.target.ts` |
| 加载机制 | Roslyn 反射 | ES Module 动态 import |
| 后端 | vcxproj (IDE) + 直接 spawn (CLI) | ninja (当前) + ActionGraph (未来) |
| IDE 工程 | 生成 vcxproj + xcodeproj | 生成轻量 VS 工程和原生 Xcode 工程 |
| 增量编译 | 自管 `.lastbuildinfo` 哈希 | ninja mtime (当前) + 自管 SQLite (未来) |
| Unity Build | ✅ 内置 | ❌ 后期 |
| PCH | ✅ 自动管理 | ❌ 后期 |
| Live Coding | ✅ | ❌ 不做 |
| 分布式编译 | ✅ XGE/FASTBuild | ❌ 用 FASTBuild 集成 |
| 插件系统 | ✅ `.uplugin` 自动发现 | ⚠️ 后期 |

## 14. 迁移路径（分阶段）

### Phase 0：脚手架（1 周业余时间）

- [x] 创建 `Engine/Builder/` 下的 LB 目录结构
- [ ] 初始化 `package.json` + `tsconfig.json`
- [x] 实现 `ModuleBuild` / `defineTarget` API + 类型定义
- [x] 写一个能跑的 `limitless-builder list modules` 命令

**验收**：能列出当前所有 9 个 `.Build.cs` 对应的模块名。

### Phase 1：核心管线（2 周业余时间）

- [x] 实现 Loader（动态 import `Build.ts`）
- [ ] 实现依赖图 + 拓扑排序
- [ ] 实现 API 宏推导
- [ ] 实现 IR Builder（生成 `BuildAction[]`）
- [ ] 实现 MSVCToolchain（vswhere + vcvars 等价逻辑）

**验收**：`limitless-builder generate --platform Win64 --config Debug --type Game` 能生成 `BuildAction` 和 ninja 文件。

### Phase 2：NinjaBackend + 编译跑通（1-2 周）

- [ ] 实现 `IBackend` 接口
- [ ] 实现 `NinjaBackend.generate()`
- [ ] 实现 `compile_commands.json` 输出
- [x] 实现 `limitless-builder build` 调用 ninja

**验收**：用 LimitlessBuilder 编译 `LimitlessGame.exe` 跑起来，行为与 Sharpmake 版本一致。

### Phase 3：迁移所有模块（1 周）

- [x] 把 9 个 `.Build.cs` 转成 `Build.ts`
- [ ] 把 `Target.cs` + `Solution.cs` 合并成 `Limitless.target.ts`
- [ ] 把 `Module.cs` / `LimitlessProject.cs` 的逻辑迁进 core
- [ ] 写 `limitless.config.ts`

**验收**：所有 4 个配置（Debug/Release × Game/Editor）都能在 Win64 上编译通过。

### Phase 4：Mac 平台（1 周）

- [ ] 实现 `ClangToolchain`
- [ ] 实现 Mac 平台规则（`arm64`、`AppKit/QuartzCore` framework）
- [ ] 实现 `.mm` / `.m` 文件编译支持
- [ ] 平台源文件排除（`MacWindow.mm` vs `WindowsWindow.cpp`）
- [x] 从 `Build.ts` 生成原生 Xcode 工程

**验收**：在 macOS 上用 LimitlessBuilder 编译出 `LimitlessGame` 可执行文件。

### Phase 5：清理（几天）

- [x] 删除 `External/Sharpmake/` 子模块和预编译 DLL 目录
- [x] 删除所有 Sharpmake C# 规则与 `.Build.cs` 文件
- [x] 删除旧的模块 include 生成脚本
- [x] 用 `GenerateProject.bat` 兼容入口和 `Docs/BuildSystem.md` 接入 LB

## 15. 关键决策记录

| 决策 | 选择 | 理由 |
|------|------|------|
| 配置文件语言 | TypeScript | 用户偏好；与项目其他工具链统一；类型系统够用 |
| 后端 | ninja (Strategy 接口预留) | 快速拿到 UBT CLI 级性能，不被 ninja 绑死 |
| IDE 工程 | 按平台生成 | 保留 LB 的模块配置为单一事实来源，同时支持 VS/Xcode 开发体验 |
| 配置文件后缀 | `Build.ts` / `.target.ts` | 直观，与 Sharpmake 的 `.Build.cs` 一一对应 |
| 分发方式 | 单一编译产物 (bun/pkg) | 开发者无需装 Node |
| 增量编译 | 先用 ninja 内置，后期自管 SQLite | 不过度设计 |

## 16. 风险与开放问题

1. **MSVC 环境加载**：通过 `vswhere` 获取安装目录，直接组合 MSVC 工具目录；Windows SDK 优先读取 `WindowsSdkDir`，再查 `KitsRoot10` 注册表项。无需调用 `vcvarsall.bat`，也不依赖固定安装路径。

2. **PCH 支持**：ninja 对 PCH 的支持不如 MSBuild 顺滑，需要在 IR 层把 PCH 当作额外的 compile action 处理。Phase 1 可不做，Phase 3 再补。

3. **API 宏跨平台一致性**：Mac 上模块全是静态 Lib（无 DLL 概念），`CORE_API` 等宏必须为空。已有逻辑覆盖，但要加测试用例防止回归。

4. **`compile_commands.json` 体积**：引擎大起来后这个文件可能上百 MB，clangd 加载慢。可考虑分模块生成多个 `compile_commands_<module>.json`，用 `clangd` 的 `--query-driver` 配置。

5. **Node 运行时依赖**：开发者要装 Node 或 Bun。最简方案是发布单 exe（用 `bun build --compile`），但要测试 Win/Mac 都能跑。
