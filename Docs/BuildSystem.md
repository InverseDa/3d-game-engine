# LimitlessBuilder 构建系统

Limitless Engine 使用 **LimitlessBuilder (LB)** 作为唯一构建系统。LB 以 TypeScript
加载各模块的 `Build.ts` 和依赖图；Windows 由 Ninja/MSVC 构建，macOS 可由同一份
模块配置生成原生 Xcode 工程。

## 入口

Windows 下使用以下命令：

```bat
Engine\Builder\LimitlessBuilder.bat build --platform Win64 --config Debug --type Game
```

或执行 `GenerateProject.bat` 生成 Rider / Visual Studio 可打开的 Makefile 工程。
工程中的 Build 命令仍由 LB 驱动，实际编译不会依赖 IDE 工程生成器。

macOS 下执行：

```sh
./GenerateProject.sh
```

脚本会生成根目录的 `LimitlessEngine.xcodeproj`，其中包含 Debug/Release 配置和共享
`LimitlessEngine` Scheme。脚本可从任意工作目录重复运行；生成内容未变化时不会重写文件。
需要 Node.js 22.6 或更新版本，可通过 `LIMITLESS_BUILDER_NODE` 指定 Node 可执行文件。

## 模块配置

每个模块在自身目录放置一个 `Build.ts`，默认导出继承 `ModuleBuild` 的类：

```typescript
export default class CoreBuild extends ModuleBuild {
    public readonly Name = "Core";

    public Configure(_Target: Target, Configuration: ModuleConfiguration): void {
        Configuration.PublicDependencies.push("Spdlog");
    }
}
```

LB 会递归发现全部 `Build.ts`，因此新增模块不需要维护集中式 include 列表。

依赖消费者只继承依赖模块的 `ExportDefines`；模块自身的 `Defines`（包括 Editor DLL 的
`*_API=dllexport`）不会泄漏给消费者。直接 public/private 依赖都参与当前模块的链接，
之后只沿 public 依赖继续传播；`WithoutLinking` 只剪断链接传播，不影响编译所需的
export defines 和 public include。Win64 DLL link action 同时声明 `.dll` 与 import `.lib`
输出，消费者把 import `.lib` 的绝对路径作为 explicit action input，使 Ninja 能从文件
producer 建立真实的构建顺序，而不是依赖并行时序碰巧正确。

### CustomActions 与生成源码

模块通过强类型 `Configuration.CustomActions` 声明生成步骤。每项包含稳定 `Id`、
`Inputs`、`Outputs`、argv `Command`，以及可选的 `WorkingDirectory`、`DependsOn`、
`ImplicitInputs`、`Description` 和 `RunBeforeCompile`。不要用 `CustomProperties` 承载构建图。

custom outputs 必须位于 `[engine.Temp]`、`[engine.Generated]`、`[module.Generated]` 或
`[engine.Binaries]`。生成的 C/C++/ObjC 源文件在首次存在前就进入 compile graph；生成
头文件可用 `RunBeforeCompile` 附着到模块编译。Ninja 将 `ImplicitInputs` 作为 timestamp
依赖，将 `DependsOn` 作为 order-only 依赖，并对多输出 custom edge 开启 `restat`。
完整 schema、路径变量和增量语义见 `Docs/LimitlessBuilder.md`。

CustomActions 当前由 LB/Ninja 执行；生成的 VCXProj/Xcode 工程继续通过 LB build 命令
进入同一 action graph，而不在 IDE 工程格式里维护第二份 custom-action 定义。

二进制产物按 `Engine/Binaries/<Platform>/<Config>/<TargetDescriptor>/<TargetType>` 隔离，
Ninja、object 和生成代码位于对应的
`Engine/Intermediate/Build/<Platform>/<Config>/<TargetDescriptor>/<TargetType>`。variant identity
使用已校验的 target descriptor `Name` 与 `TargetType`，不使用可能随配置变化的 `OutputName`。
因此同一 descriptor 的 Game/Editor、不同 Program/Test descriptor，以及 Debug/Release 都不会
复用模块库、DLL、对象、`build.ninja`、`compile_commands.json` 或 custom outputs。Visual Studio
Clean 也只清理当前 configuration 的当前 target variant。

## 工具发现

- Node：`LIMITLESS_BUILDER_NODE`、PATH、Windows 注册表。
- Ninja：`LIMITLESS_BUILDER_NINJA`、PATH、Windows 注册表中的持久化 PATH。
- Visual Studio：`vswhere` 与标准安装器目录。
- Windows SDK：`WindowsSdkDir` 或 Windows SDK 注册表项。
- Xcode：工程生成后使用 macOS SDK、clang、AppKit/QuartzCore，以及 `VULKAN_SDK`
  或常见 Homebrew 路径下的 Vulkan Loader。

在 Rider 未继承终端环境时，Windows 批处理入口会直接读取用户级的 LB
工具变量，因此无需依赖 Rider 的 PATH。
