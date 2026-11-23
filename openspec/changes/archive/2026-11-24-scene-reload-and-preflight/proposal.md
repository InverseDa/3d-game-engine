# Proposal

## Why

相机、光源和网格可按 UUID 从 JSON 修改。AI 和开发者需要在运行中的 Game 查看修改结果，并能在提交前定位无法渲染的作者数据。

## What Changes

- 在 Win64 Demo 中按 F5 重载当前场景文件，安全发布通过预检的候选场景，失败时继续呈现已发布场景。
- 为 `SceneTool check --json` 提供机器可读诊断，并与渲染提取共用受光网格的世界法线矩阵校验规则。
- 将场景替换请求和发布移到 Update 的 tick 边界，复用已有场景、资源会话和帧同步机制。
- 跟踪构建引用的 RFG exporter 头文件，验证隔离源码检出的 Win64 构建。

## Capabilities

### Modified Capabilities

- `scene-persistence` 扩展运行时场景重载和无窗口校验契约。

## Impact

改动涉及 Scene、World、WorldRenderer、SceneTool、Win64 Platform 输入、DemoApplication 和对应的构建与测试。当前 JSON 和已有的 cooked 资产构成本轮重载范围。
