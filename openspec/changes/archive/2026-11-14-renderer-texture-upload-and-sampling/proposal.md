# Proposal

## Why

M1 已能导入纹理并同步加载 RGBA8 像素，BasePass 仍只绘制纯色材质。下一步把现有纹理资产连到 GPU 和材质采样，让开发者和 AI 可以编辑纹理像素、网格 UV 和材质引用，再通过现有构建流程看到结果。

## What Changes

- 将现有 PPM 导出的 RGBA8 颜色纹理同步上传到 GPU，完成提交与状态转换后才允许材质使用。同一内容会话内的多个材质复用同一已上传纹理。
- 为受限 OBJ 增加 UV，为文本材质增加一个可选的 Base Color 纹理 UUID。通过无窗口命令检查源文件和场景的资源引用。
- 在现有不透明 BasePass 中采样 Base Color，并与线性材质色及顶点色相乘。保留 M1 网格、纯色材质、纹理和 JSON 场景的读取兼容性。
- 用可编辑的非对称纹理样例验证像素、色彩空间、UV 方向和共享资源。首版范围为 2D、单 mip、不透明 Base Color 和固定采样参数。

## Capabilities

### New Capabilities

- `renderer-texture-sampling`：同步上传颜色纹理，并让 BasePass 正确采样、复用和回收纹理资源。

### Modified Capabilities

- `asset-import`：支持可编辑 UV、纹理材质引用和兼容的导入产物升级。
- `synchronous-asset-loading`：同步解析材质的纹理依赖，保留独立 CPU 纹理加载接口。
- `scene-persistence`：无窗口检查和恢复覆盖传递纹理引用及 Mesh/Material 兼容性，场景 JSON 继续引用原有 Mesh/Material UUID。

## Impact

- 变更 AssetImport、AssetFormat、AssetLoading、SceneTool、Renderer、WorldRenderer、Vulkan RHI、BasePass shader 和 Demo 样例。World 继续保存逻辑资源句柄。
- Win64 Vulkan 为首个实际运行验收平台。PBR、透明、mip 生成、纹理流送、异步加载和热重载留待后续变更。
