# Proposal

## Why

现有 3D 样例验证了贴图、方向光和阴影，导入输入仍是分别编写的 OBJ、PPM 和材质文件。接入一个有真实几何和节点层级的 glTF 模型，可以验证常见内容输入能否沿用现有资产、场景和渲染链路，也让人和 AI 能直接修改场景并立即检查结果。

## What Changes

- 离线导入一组明确定义的 glTF 2.0 静态模型输入，输出 `.leasset` 和可编辑的初始 Scene JSON。首版支持外置 BIN/PNG、索引三角形、节点 TRS、法线、UV 和不透明 Base Color 材质。一个网格的多个 primitive 可以各用自己的材质。
- 在版本管理中保存显式 ID 映射。重导入复用资产 UUID、虚拟路径和实体 UUID。源文件重新导出导致索引变动时，由人或 AI 在清单中明确重新绑定并审阅。
- 将一批产物验证后整体发布，保留上一版可用产物，并保护已经由人或 AI 编辑的 Scene JSON。提供无窗口的检查和可供脚本消费的诊断。
- 在 Win64 用 Khronos 的 Lantern 模型验证贴图、层级、光照和投影。另用小测试件覆盖单网格多材质和几何复用。验收包含 AI 按 UUID 修改 Scene JSON、`SceneTool check --json` 和 F5 实际显示。

## Capabilities

### New Capabilities

### Modified Capabilities

- `asset-import` — 增加静态 glTF 转换、显式身份映射、整批发布与重导入回退的要求。

## Impact

`AssetImport` 负责格式适配与候选产物生成，工具侧编排重导入与发布。复用 `AssetFormat`、Scene 数据格式和 `SceneTool`，Win64 Game 继续以现有挂载和 F5 入口预览。首版目标是当前不透明 BasePass 下的模型预览。GLB、压缩、动画、完整 PBR、运行中切换已烘焙资产目录和 Editor Shader 动态编译属于后续工作。
