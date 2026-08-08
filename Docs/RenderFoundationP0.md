# 渲染基础 P0 收口记录

## 冻结范围

RFG/RAL P0 于 2026-08-06 关闭并冻结。冻结面为现有 RFG Record/Compile/Execute 路径及其 Vulkan RAL Dynamic Rendering 后端，包括 single-RT、最多 8 路 MRT、可选 `D32_FLOAT` / `D24_UNORM_S8_UINT` 深度模板，以及双 Pass 的 offscreen-composite 演示。

本次收口不增加新的 Renderer 特性。Swapchain 帧调度、移除逐帧 `WaitIdle`、按 frame slot 管理瞬态资源，以及真实 BasePass 均保留给 Roadmap 后续阶段。

## 必须保持的约束

- `FRALPipelineDesc_Graphics` 与 `FRALRenderPassDesc` 的颜色附件数量、格式，以及深度模板附件的存在性和格式必须一致。
- 一个 rendering scope 至少包含一个颜色或深度模板附件；颜色附件不得超过 8 个。
- 同一 scope 的全部附件必须具有相同的有效 mip 尺寸，RenderArea 必须位于该尺寸内。
- Dynamic Rendering 不做隐式 layout transition。颜色写入必须在 Pass 中声明 `RenderTarget`；当前可写深度模板用法必须在 Begin 前声明并转换到 `DepthStencilWrite`。
- `D24_UNORM_S8_UINT` 的 depth 与 stencil 指向同一个 `VkRenderingAttachmentInfo`；`D32_FLOAT` 只提供 depth attachment。
- P0 验收要求 Khronos Validation Layer 可用、Validation Messenger 成功安装，并且从首帧到正常退出的 `[VK Validation][Error]` 数量为 0。

## 通用验证步骤

在仓库根目录构建：

```powershell
Engine\Builder\LimitlessBuilder.bat build --platform Win64 --config Debug --type Game
```

以仓库根目录作为工作目录运行 `Engine\Binaries\Win64\Debug\Limitless\Game\LimitlessGame.exe`，至少渲染一帧后正常关闭窗口，再检查 `Firefly.log`。通过日志必须包含 `Vulkan validation messenger installed.`、对应 Pipeline/Pass 标记和 `Goodbye!`，且以下计数必须为 0：

```powershell
@(Select-String -Path Firefly.log -SimpleMatch '[VK Validation][Error]').Count
```

不得接受 Validation 不可用，或 RAL 销毁前被强制终止的进程结果。

## 验证变体复现

默认 checkout 同时覆盖 offscreen single-RT 与 composite。MRT 和 depth/stencil 是同一演示的验证专用变体，必须在隔离 worktree 中运行，不能留在冻结源码中。

### dual-MRT + composite

本地已有验证提交 `51141a3`，其父提交正是冻结基线 `b2ed297`：

```powershell
git worktree add --detach C:\tmp\limitless-phase0-mrt 51141a3
Set-Location C:\tmp\limitless-phase0-mrt
Engine\Builder\LimitlessBuilder.bat build --platform Win64 --config Debug --type Game
```

预期日志为 `RenderTargets=2, HasDepth=false`，随后是 composite 的 `RenderTargets=1, HasDepth=false`，Pass 顺序为 `OffscreenColorPass -> CompositeToBackBufferPass`。

### depth/stencil + composite

验证快照树 `85649c3c2af4f053b7f1edd3748f93498bf2f370` 保存了已审查的 D24 consumer。它创建 `D24_UNORM_S8_UINT`，在 RFG 中显式声明 `Undefined -> DepthStencilWrite`，再绑定 depth/stencil attachment。可在 `b2ed297` worktree 中只应用以下五个文件的既有差异：

```powershell
git worktree add --detach C:\tmp\limitless-phase0-depth b2ed297
git diff --binary b2ed297 85649c3c2af4f053b7f1edd3748f93498bf2f370 -- `
  Engine/Source/Runtime/Launch/Private/Windows/LaunchWindows.cpp `
  Engine/Source/Runtime/RAL/Private/Vulkan/VulkanCommandList.cpp `
  Engine/Source/Runtime/RAL/Private/Vulkan/VulkanPipeline.cpp `
  Engine/Source/Runtime/Renderer/Private/Renderer/Passes/TriangleCompositePasses.h `
  Engine/Source/Runtime/Renderer/Public/Renderer/DemoRenderPipelines.h |
  git -C C:\tmp\limitless-phase0-depth apply --whitespace=nowarn -
Set-Location C:\tmp\limitless-phase0-depth
Engine\Builder\LimitlessBuilder.bat build --platform Win64 --config Debug --type Game
```

预期日志为 `RenderTargets=1, HasDepth=true`，随后是 composite 的 `RenderTargets=1, HasDepth=false`，Pass 顺序同上。验证完成后移除两个临时 worktree；验证专用 consumer 不进入冻结源码。

## 2026-08-06 验收结果

环境：Windows 11、MSVC 14.37.32822、NVIDIA GeForce RTX 4070 Ti、Vulkan loader/device API 1.4.341、Core Dynamic Rendering、Debug Game target。

| 路径 | 构建 | 进程 | Validation Messenger | Validation Errors |
|---|---:|---:|---:|---:|
| single-RT + composite | exit 0 | 正常退出 0 | installed | 0 |
| dual-MRT + composite | exit 0 | 正常退出 0 | installed | 0 |
| depth/stencil + composite | exit 0 | 正常退出 0 | installed | 0 |

另外从包含正式 Phase 0 diff 的临时 Git 快照 `47a3725` 创建 clean worktree 后，完整 54-step build 直接成功；`RFGGraphExporter.h` 随源码存在，不再需要手工复制。

## 已知限制

- Validation 当前只输出 hard error，warning 与 performance diagnostic 不属于本门禁。
- 演示仍逐帧等待 Graphics Queue 完成，Swapchain 同步也保持简化；后续 frame lifecycle 任务负责修正。
- 当前 BeginRenderPass 固定使用可写 `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`。只读 depth/stencil 的管线和 layout 策略尚未实现。
- `.gitignore` 中对 `Engine/Source/Runtime/RFG/Public/Debug/RFGGraphExporter.h` 有精确例外；只要 tracked RFG 头文件仍 include 它就必须保留。P0 不实现 exporter。
