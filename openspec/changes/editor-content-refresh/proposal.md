# Proposal

## Why

Lantern 已能离线重导入并安全发布新资产代。预览仍需重启才能挂载新代，延长了修改贴图到看到结果的反馈时间。

## What Changes

- Win64 Editor 在指定发布根和独立作者 Scene JSON 后，以最新已发布资产代启动，支持用 F6 在同一进程预览新代。
- 先验证候选资产和场景，再切换可用状态。保持仍是同一作者相机的漫游机位。失败或重复刷新当前代时保留已显示内容，结果提供版号和结构化诊断。
- 用 Lantern 验证换贴图、候选损坏、恢复、作者文件字节及 F5 的场景重载行为。

## Capabilities

### New Capabilities

- `editor-content-preview` — Editor 选择、预检和刷新离线发布的资产代。

### Modified Capabilities

## Impact

改动集中在 Demo 的 Editor 预览路径和平台快捷键。继续使用现有发布协议、场景校验、资产加载和 Renderer。首版采用手动刷新，沿用同步候选加载和帧排空。Shader 动态编译和文件监控另行安排。
