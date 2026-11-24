# Win64 Editor Shader 预览

在 Win64 Vulkan Editor 中按 **F7**，可编译并预览 `BasePassShadowPS.lsf` 的 `MainPS`。入口会包含 `BasePassPS.lsf` 及递归 include。源码编辑保存在工作区，已切换的 Shader 在当前 Editor 会话内生效。重新启动时使用打包的离线版本。

## 启动 Lantern

在仓库根目录构建 Win64 Debug Editor。开发环境需要 Node.js 22.6 或更新版本，以及提供 `glslangValidator`、`spirv-val`、`spirv-cross` 和 `spirv-dis` 的 Vulkan SDK。Editor 从 `LIMITLESS_BUILDER_NODE` 或绝对 `PATH` 项查找 Node，Builder 从 `VULKAN_SDK` 或绝对 `PATH` 项查找 Shader 工具。

```powershell
Engine/Builder/LimitlessBuilder.bat build --target Limitless --platform Win64 --config Debug --type Editor
```

按 [Lantern Editor 启动步骤](GltfImport.md#在-win64-editor-刷新资产版本) 准备好作者场景和资产发布目录，在启动 exe 的同一个 PowerShell 里设置源码根目录。

```powershell
$env:LIMITLESS_EDITOR_SHADER_ROOT = (Get-Location).Path
```

编辑 `Engine/Content/Shaders/BasePassPS.lsf` 的最后一个 `return`，例如给原计算加一层红色。

```hlsl
return float4(saturate(material * factor + float3(0.40, 0.0, 0.0)), 1.0);
```

保存后回到 Lantern 窗口按 F7。修改资源绑定和布局需要通过正常构建流程同步 Shader 与运行时代码。F5 场景重载和 F6 资产刷新可以在同一个会话里继续使用。

## 读取结果

在 exe 旁边的 `Firefly.log` 搜索 `Editor shader preview `。后面的单行 JSON 提供 `requestId`、`status`、`stage`、`code` 和 `diagnostic`，`started` 还会记录编译进程的 `processId`。`presented` 表示使用此版本的帧已成功提交显示。编译时再次按键会报告 `busy`。`rejected` 表示本次请求失败，Editor 继续使用当前 Shader。

编译错误的 `diagnostic` 指向该请求的 JSON 诊断文件，文件记录编译器提供的源码路径与行号。Editor 在发起下一次编译或退出时清理诊断文件。若在编译期间改过依赖文件并收到 `source_changed`，下一次 F7 会使用最新源码。
