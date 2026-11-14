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

## 编辑作者相机

将样例 `Scene.json` 复制到 `Engine/Intermediate/CameraEdited.json` 后，按 UUID `8a0a5d74-17ef-48e0-a46b-f7105518e704` 找到主相机。可以将局部 `transform.translation` 改为 `[0, 0, 4]`，`transform.rotation` 改为 `[0, 0, 0.13052619, 0.99144486]`，`camera.verticalFieldOfViewRadians` 改为 `0.9`。平移和缩放采用 `[x, y, z]`，旋转采用有限的非零四元数 `[x, y, z, w]`。视场角以弧度计，必须严格介于 `0` 和 `π` 之间。`nearPlane` 必须大于零，`farPlane` 必须大于 `nearPlane`。活动相机按 `active` 和 `priority` 参与主相机选择，Game 预检也会检查父层级求值后的相机世界变换是否为刚性变换。

用上一步准备好的 SceneTool 和资产根校验副本。成功时退出码为 `0`，文件字节保持原样。把视场角设为 `0` 或把四元数四个分量都设为 `0`，会返回非零退出码并在标准错误中报告文件、实体 UUID 和字段。

```powershell
$cameraScene = (Resolve-Path 'Engine/Intermediate/CameraEdited.json').Path
$before = (Get-FileHash -LiteralPath $cameraScene).Hash
& Engine/Binaries/Win64/Debug/SceneTool/Program/SceneTool.exe check $cameraScene Game "$content/Assets"
$checkExit = $LASTEXITCODE
$unchanged = (Get-FileHash -LiteralPath $cameraScene).Hash -eq $before
"SceneTool exit $checkExit, bytes unchanged: $unchanged"
```

校验成功后，将 `LIMITLESS_DEMO_SCENE` 设为 `$cameraScene` 并启动 Game，检查首帧是否使用新机位和视场角。结束后移除该环境变量。

Win64 Demo 打开场景后从作者主相机出发。窗口有焦点时，用 W/S 沿视线前后移动、A/D 向相机两侧移动，按住右键移动鼠标转向。松开右键结束转向，WASD 仍可移动。切换到其他窗口或最小化后，回到窗口需要重新按下按键。漫游只影响本次查看，重启后回到 Scene JSON 中的作者机位。

## 保存并重新加载场景

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

验收漫游与保存时，先设 `LIMITLESS_DEMO_SAVE_SCENE` 保存作者状态。第二次用同一个源场景启动，换一个存档路径并设置 `LIMITLESS_DEMO_TEST_AFTER_NAVIGATION=1`，移动相机后等待日志出现 `Presented navigated camera` 和 `Saved scene`。该测试入口在第一个呈现成功的漫游帧后执行指定的保存，也可配合 `LIMITLESS_DEMO_NEXT_SCENE` 验证替换场景。用 `Get-FileHash` 比较两份存档与前后源文件。两份存档应一致，源文件哈希应保持原值。

## Win64 Debug 验收记录

Win64 Debug 的 AssetImport、SceneTool、Game、Editor 和 Test 已构建。Builder 单元与集成测试 44 项通过，C++ Test target 和规范扫描通过。在 GeForce RTX 4070 Ti（Vulkan 1.4.341）上，Game 加载五实体场景，两个网格进入 BasePass。将左侧子实体局部 X 改为 `-0.8` 后保存，新进程从保存文件恢复实体、层级和资源引用。验证了 resize、最小化恢复和正常退出。两次运行均安装 Khronos 验证回调，错误为零。该次验收用进程环境变量禁用了本机的 Overwolf 捕获层。macOS 尚无自动样例 staging 和运行验收，可通过绝对内容根路径提供同样的已生成内容。

### M2 键鼠与相机

- Win64 Debug Game、Editor、SceneTool 和 LimitlessTests 构建成功。C++ 测试通过，Builder 单元与集成测试共 44 项通过。
- 在 RTX 4070 Ti 上，W 在右键松开时可移动。按住右键转出视野再转回，贴图与纯色网格消失后重新可见。指针到达桌面边缘时仍可继续转向。在客户区外松开右键，窗口释放捕获，后续移动鼠标的两张客户区截图一致。
- 按住 W 和右键时切到测试辅助窗口或最小化，Game 窗口释放捕获。仍按住这两个键恢复时，前后两张画面一致，重新按键后输入恢复。缩放窗口前后各有 2 次累计纹理上传。Alt+F4 正常退出，验收进程的 Khronos 验证错误为 0。
- 按 UUID 修改作者相机的位置、旋转和视场角，`SceneTool check` 返回 0。零视场角和零四元数分别返回 1，包含文件、字段和实体 UUID。校验前后源文件字节一致。漫游前后的显式存档 SHA-256 一致，重启后的画面与编辑后的作者机位逐像素一致。
- 特意在漫游后尝试替换场景。无效替换保留已漫游的画面，有效替换从新场景的作者机位起步。
