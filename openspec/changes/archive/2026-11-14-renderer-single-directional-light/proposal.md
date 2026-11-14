# Proposal

## Why

当前 BasePass 已能显示贴图和纯色材质，画面只反映资产自身的颜色。单方向光补齐从离线网格法线、可编辑场景数据到实际光照的路径，让人和 AI 都能通过修改源内容控制画面并复现结果。

## What Changes

- OBJ 导入支持显式顶点法线和法线接缝。有法线的网格写入 mesh v3，已有 mesh v1/v2 可直接读取，原输入可重建出相同字节。
- Scene v2 增加可选的 `directionalLight` 实体组件。用稳定 UUID 编辑世界空间光线方向、线性 RGB、强度和启用状态。一个场景至多启用一盏灯。
- WorldRenderer 将启用的灯提取到快照。BasePass 对有法线的网格计算线性空间 Lambert 漫反射，沿用现有纯色和 Base Color 纹理输入。无启用光源或无顶点法线的绘制保留现有输出。
- 验收涵盖无窗口导入和 SceneTool 校验、保存及重启、候选场景回滚，以及 Win64 Vulkan 像素基准。默认贴图与 M2 漫游场景继续作为回归样例，另加独立光照样例。

## Capabilities

### New Capabilities

- `renderer-directional-lighting` 定义单方向光的快照、着色、空间约定及兼容图像行为。

### Modified Capabilities

- `asset-import` 定义显式 OBJ 顶点法线、接缝展开与 mesh v3 兼容。
- `scene-persistence` 定义 Scene v2 光源组件、AI 可编辑字段、无窗口校验和保存恢复。
- `renderer-texture-sampling` 说明现有纹理采样结果如何作为光照输入，并明确无光照像素基准。

## Impact

- AssetImport、AssetFormat 和 Renderer 网格构造读取法线。现有材质和纹理文件格式沿用。
- Scene、SceneTool、World、WorldRenderer 和 BasePass 增加一条从作者光源到像素的路径。方向光使用现有 RHI 和帧图边界。
- 本次光照范围为一个方向光的直接漫反射。阴影、环境光、PBR、法线贴图和多光源留给后续变更。
