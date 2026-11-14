# Proposal

## Why

M1 已能把可编辑的场景和贴图资产加载到 BasePass，但当前只能从场景文件指定的机位查看内容。M2 让用户在现有 Win64 Demo 中直接检查场景，也把 AI 修改作者机位后的无窗口校验和启动验证纳入交付。

## What Changes

- 为 Win64 窗口提供键盘、右键和相对鼠标输入。Demo 在窗口有焦点时用 WASD 移动，按住右键转视角。
- 从已加载场景的主相机起步，漫游机位保存在 Demo 会话。离开并重启后，从场景中的作者机位重新开始。
- 验收 AI 或脚本按实体 UUID 修改 Scene v1 的相机变换与参数，再用现有 `SceneTool check` 无窗口校验，并启动 Game 检查新机位。
- 以 Win64 Debug Game 和 SceneTool 为交付验收入口。跨平台值类型沿用现有 Platform 边界，macOS 键鼠实现以及 Renderer 的方向光各走后续变更。

## Capabilities

### New Capabilities

- `window-input` 定义窗口键鼠输入、每帧状态以及焦点和鼠标捕获的生命周期。
- `scene-camera-navigation` 定义从作者主相机起步的会话漫游及其与渲染剔除的关系。

### Modified Capabilities

- `scene-persistence` 明确漫游姿态与场景保存的关系，并把直接编辑相机 JSON 的无窗口校验写入验收。

## Impact

- Platform 的公共窗口输入契约和 Win32 实现，以及 DemoApplication 的帧输入和相机会话状态。
- WorldRenderer 的视图提取可接收会话机位。现有 World 相机组件、主相机选择规则、Scene v1 格式及 Renderer、RFG 和 RHI 接口继续沿用。
- SceneTool 沿用现有命令和资产挂载方式。补充相机编辑校验、渲染和保存的回归验证。
