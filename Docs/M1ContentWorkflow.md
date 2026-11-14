# M1 内容工作流

Win64 Game 和 Editor 构建会读取样例导入清单，运行 AssetImport 和 SceneTool，并在可执行文件旁生成 `DemoContent/Scene.json` 和 `DemoContent/Assets`。源码路径、导入参数与输出都进入构建图。每个 Game 或 Editor 构建变体独立构建内容工具。

1. 修改 `Engine/Content/Examples/AssetImport/` 内的 `Triangle.obj`、`Palette.ppm`、`WarmOrange.material`、`Scene.json` 或导入清单 `Imports.json`。实体和资产用稳定 UUID 交叉引用。代码、资产与场景都可通过常规 Git diff 和命令行验证。
2. 运行 `Engine/Builder/LimitlessBuilder.bat build --platform Win64 --config Debug --type Game`。非法输入会让构建报错并显示出错位置，成功时重新生成可运行的样例。
3. 启动 `Engine/Binaries/Win64/Debug/Limitless/Game/LimitlessGame.exe`。当前样例有 7 个实体及 4 个可见网格，在现有 BasePass 中混合显示贴图和纯色材质。纹理工作流和验收见 [TextureSampling.md](TextureSampling.md)。

无需打开窗口即可运行校验。先构建 `SceneTool` Program，随后传入场景文件、mount 名称和已导入资产的绝对根目录。单个资产源可用 [AssetImport](AssetImport.md) 的 `--check`。JSON 字段及示例见 [SceneFormat](SceneFormat.md)。

```powershell
Engine/Builder/LimitlessBuilder.bat build --target SceneTool --platform Win64 --config Debug --type Program
$content = (Resolve-Path 'Engine/Binaries/Win64/Debug/Limitless/Game/DemoContent').Path
& Engine/Binaries/Win64/Debug/SceneTool/Program/SceneTool.exe check 'Engine/Content/Examples/AssetImport/Scene.json' Game "$content/Assets"
```

以下命令在启动时编辑一个实体的局部 X，保存当前 World，关闭窗口后从保存文件重新启动。可以编辑保存后的 JSON 并再次验证。将路径传给 `LIMITLESS_DEMO_CONTENT` 可选择另一个绝对内容根，其目录结构与生成的 `DemoContent` 相同。

```powershell
$game = (Resolve-Path 'Engine/Binaries/Win64/Debug/Limitless/Game/LimitlessGame.exe').Path
$save = Join-Path (Resolve-Path 'Engine/Intermediate').Path 'M1SavedScene.json'
$env:LIMITLESS_DEMO_EDIT_ENTITY = '8a0a5d74-17ef-48e0-a46b-f7105518e702'
$env:LIMITLESS_DEMO_EDIT_X = '-1.78'
$env:LIMITLESS_DEMO_SAVE_SCENE = $save
Start-Process -FilePath $game -WorkingDirectory (Get-Location).Path -Wait
Remove-Item Env:LIMITLESS_DEMO_EDIT_ENTITY, Env:LIMITLESS_DEMO_EDIT_X, Env:LIMITLESS_DEMO_SAVE_SCENE
$env:LIMITLESS_DEMO_SCENE = $save
Start-Process -FilePath $game -WorkingDirectory (Get-Location).Path -Wait
Remove-Item Env:LIMITLESS_DEMO_SCENE
```

在第二次启动前，可以用 `SceneTool check` 验证存档引用。运行时日志 `Firefly.log` 记录加载、保存和渲染结果。Win64 Vulkan 验收应包含已安装 Khronos 验证回调、resize、最小化恢复和正常退出，且 `[VK Validation][ERROR]` 为零。若已安装 Overwolf 且其隐式层加载报错，可只在验收进程环境设 `DISABLE_VULKAN_OW_OBS_CAPTURE=1`，保留 Khronos 验证层。

## Win64 Debug 验收记录

Win64 Debug 的 AssetImport、SceneTool、Game、Editor 和 Test 已构建。Builder 单元与集成测试 44 项通过，C++ Test target 和规范扫描通过。在 GeForce RTX 4070 Ti（Vulkan 1.4.341）上，Game 加载五实体场景，两个网格进入 BasePass。将左侧子实体局部 X 改为 `-0.8` 后保存，新进程从保存文件恢复实体、层级和资源引用。验证了 resize、最小化恢复和正常退出。两次运行均安装 Khronos 验证回调，错误为零。该次验收用进程环境变量禁用了本机的 Overwolf 捕获层。macOS 尚无自动样例 staging 和运行验收，可通过绝对内容根路径提供同样的已生成内容。
