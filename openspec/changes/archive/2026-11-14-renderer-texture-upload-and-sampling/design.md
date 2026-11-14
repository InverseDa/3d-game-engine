# Design

## Context

M1 的 `FCpuTextureAsset` 存放 RGBA8 像素，AssetLoading 的直接纹理请求可在无 GPU 时运行。现有 BasePass 读取 Position、Color 和材质常量。Composite 已提供 SampledImage、Sampler 和 bind group 的 Vulkan 路径。RHI 已有 TransferSrc buffer usage 及 CopyDestination、ShaderResource 状态，但公开命令列表没有 Buffer→Texture 拷贝，录制和 fence 等待也缺少可供上传使用的结束状态。[资产格式](../../../../Docs/AssetFormat.md) · [同步加载](../../../../Docs/AssetLoading.md) · [Renderer](../../../../Docs/Renderer.md)

## Goals / Non-Goals

- 用现有同步内容会话实现从落盘纹理到 BasePass 的可复现渲染。保持 Asset 与 World 的图形依赖边界，沿用 M1 文件和场景身份。
- 首版限定于静态三角形、2D RGBA8 sRGB、mip 0、单层、一个可选颜色纹理和固定 nearest/clamp 采样。流送、mip 生成、PBR、透明、通用资源依赖图、热加载和 bindless 作为后续能力。

## Decisions

### 1. 只为新增数据写新版本

- `.leasset` 文件头保持原布局。v1 位置网格、纯色材质和 RGBA8 纹理继续解码并验证原格式；带 UV 网格和纹理材质写 v2。版本按类型校验，v2 不用于纹理。旧引擎不能读 v2 产物，源可保留并重新构建，场景 JSON 和 Asset ID 维持原格式。
- OBJ 增加 `vt u v` 和三角 `f v/vt v/vt v/vt`。v 和 vt 使用正数一基索引；一份网格使用统一面模式，混合、有缺项或越界时报错。按位置索引和 UV 索引的组合去重，保留绕序和接缝，重新检查展开后的顶点数。按 OBJ 的左下原点转成与 PPM 行序一致的运行时左上原点，在导入时做一次 `v = 1 - v`。
- `.material` 保留线性 `base-color r g b`，可选一条 `base-color-texture <UUID>`。同身份重导入允许 v1/v2 互换，沿用原子替换。AssetImport `--check` 验证源与编码；SceneTool 校验文件间依赖和网格 UV。对 OBJ 和材质源确保 Git 将它们作为文本展示差异。

### 2. 分离 CPU 依赖、GPU 实例和场景发布

| 边界 | 职责 |
|---|---|
| AssetImport / AssetFormat | 可编辑源、UV 转换、版本校验、稳定 ID 和纯 CPU 解码。 |
| AssetLoading / SceneTool | 解码材质，按 UUID 定位并验证一跳 CPU Texture 依赖。私有解析复用会话缓存，保留公共 API 重入保护。SceneTool 走同一数据校验规则，不创建 GPU。 |
| Renderer provider | 消费已验证 CPU 数据，上传纹理、缓存 image/view、创建材质 bind group；借用会话持有的共享 sampler 和 pipeline/layout。 |
| WorldRenderer / BasePass / RFG | 从已提交 World 提取渲染代理和借用绑定；按唯一 image 声明 Pixel ShaderResource 读取；逐 draw 绑定材质并采样。 |
| DemoApplication | 持有候选与活动场景的对应加载会话和 Renderer 资源。完成快照校验后只在 Idle 帧边界切换。 |

公开 CPU Texture handle 仍只解析 CPU 像素。AssetLoading 将已验证的 CPU 依赖数据和 ID 作为同步借用传给 provider，provider 不保留 registry 指针或借用像素。只有当前材质依赖允许内部加载，避免 provider 回调公共 `Load`。按设备会话及持久 Texture ID 缓存 GPU 实例。新建依赖或 GPU 缓存项在材质发布失败时回滚，已有成功对象及句柄保持可用。使用候选会话隔离活动场景；以 scope/owner 显式控制绑定、image/view 与共享设备的回收顺序。

材质加载记录本次是否新增 CPU 依赖，失败时仅注销该新条目，沿用 registry 对借用 view 的失效约定。GPU 缓存项由有效材质持有引用。构造中途失败或 registry 拒绝登记时，provider 清理候选 bind group 并释放其缓存引用。最后一个引用释放时回收 image/view；既有已登记材质继续持有各自的引用。每个会话持有自己的 registry 和 provider 缓存，避免候选失败修改活动资源。

### 3. 上传用现有 Graphics queue 和可确认完成的 fence

- 为 RHI 增加受检的紧密排列 Buffer→Texture2D 拷贝，覆盖 0 mip、0 layer、完整 color extent。校验设备、命令状态、渲染 scope、源 TransferSrc、目标 TransferDst、格式、尺寸和溢出安全的字节范围。Vulkan 使用 `vkCmdCopyBufferToImage`。上传分配应拒绝无效 Vulkan wrapper、无匹配内存类型或不支持采样/拷贝的格式。
- Renderer 创建 host-visible/coherent staging buffer，填入已验证像素，创建 sampled RGBA8 sRGB optimal image。专用 allocator/list 依序录制 `Undefined → CopyDestination → Copy → ShaderResource(Pixel)`，使用 Graphics queue 提交，并检查 fence 结果。以窄范围受检 RHI 操作暴露录制、提交和完成失败，避免仅凭当前 void Wait 返回就发布。已提交但完成情况未知时保留资源，直到确认完成或进入设备丢失的终止清理路径。正常帧不等待上传或对整队列调用 WaitIdle。
- 只有上传完成后，才把 image/view 交给材质和 RFG。RFG 以 ShaderResource 导入，不把纹理每帧重置为 Undefined。允许当前 barrier planner 为首读生成同状态屏障。resize 只替换尺寸相关资源；场景替换或 shutdown 先排空使用旧资源的帧，释放 bind group 后再释放对应纹理和共享绑定资源。

此切片使用现有 Graphics queue，可避免 transfer queue ownership transfer 和额外同步。以后引入异步上传时沿用相同的可用状态与发布边界。

### 4. 一个 BasePass 管线覆盖纯色和纹理材质

- 顶点布局增加 UV，v1 网格上传时填默认 UV，但仍保留“源无 UV”信息供组合校验。纯色材质绑定内部 1×1 白纹理。显式引用缺失纹理时返回错误，只有没有引用的材质使用白纹理。
- 使用一个密集 bind group set 0，SampledImage 和 Sampler 分别与 HLSL `t0`、`s1` 匹配。验收覆盖 layout/pipeline 一致性。每个 draw 在设置 pipeline 后绑定自己的材质 group。Shader 在线性空间计算 `vertexRGB × materialRGB × sampledRGB`，sRGB image 负责解码，现有 sRGB SceneColor 负责输出编码。Alpha 保持 1。
- 快照携带 GPU texture 和 bind group 的借用值。BasePass 对相同 RHI 纹理去重声明 Pixel ShaderResource 读，色彩和深度的现有附件契约保持。固定格式允许共享 BasePass pipeline 跨 resize 使用。

相比增加纯色/纹理两套 shader 和 pipeline，统一绑定减少了变体及生命周期分支。白纹理是共享的，常驻设备会话。

## Risks / Trade-offs

- 同步上传延长首次打开场景的时间。只在资源首次构造时进行，并检查共享纹理只上传一次。
- 错误绑定或重复转换可能让截图看起来有贴图却仍有错误。使用非对称 2×2 PPM、单色对照、不同纹理交错绘制和中间亮度乘色样本。以截图或图形捕获采样图像内部已知位置，避开边缘，记录目标 RGB 和容差。
- Vulkan 验证没有检查内容正确性的能力。像素验收与提交失败、资源释放及 Validation 零错误分别检查。

## Migration Plan

1. 先加受检上传和兼容 codec。已有 v1 fixtures 在整个过程中用于回归。
2. 接入同步依赖、Renderer provider/cache、BasePass 材质绑定，再更新 SceneTool 和 Demo 样例。
3. 复用可编辑 PPM 工作流，增加非对称贴图和混合材质场景。验证编辑、重建、重启、resize 及失败回滚后再发布。保留 v1 源与产物以便回退。
