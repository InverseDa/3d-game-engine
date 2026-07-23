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
        Configuration.PublicDependencies.push("Glm", "Spdlog");
    }
}
```

LB 会递归发现全部 `Build.ts`，因此新增模块不需要维护集中式 include 列表。

## 工具发现

- Node：`LIMITLESS_BUILDER_NODE`、PATH、Windows 注册表。
- Ninja：`LIMITLESS_BUILDER_NINJA`、PATH、Windows 注册表中的持久化 PATH。
- Visual Studio：`vswhere` 与标准安装器目录。
- Windows SDK：`WindowsSdkDir` 或 Windows SDK 注册表项。
- Xcode：工程生成后使用 macOS SDK、clang、AppKit/QuartzCore，以及 `VULKAN_SDK`
  或常见 Homebrew 路径下的 Vulkan Loader。

在 Rider 未继承终端环境时，Windows 批处理入口会直接读取用户级的 LB
工具变量，因此无需依赖 Rider 的 PATH。
