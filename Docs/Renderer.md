# Renderer 模块设计文档

## 1. 模块职责

Renderer 是引擎的高层渲染模块，基于 **RFG（Render Frame Graph）** + **RHI（Render Hardware Interface）** 构建可扩展的渲染管线。

World 持有 Mesh 和 Material 的类型化资产句柄。WorldRenderer 将句柄解析成帧级
`FRenderScene`，其中的网格代理借用 Renderer 的资产 payload。Renderer 的各个 Pass
从 payload 取得绘制所需的 RHI 资源，并选择各自的管线。

其主要职责包括：

- 接收游戏线程提交的场景描述（`FRenderScene`）和视图参数（`FRenderView`）。
- 通过可插拔的 `IRenderPipeline` 将高层渲染意图转换为有序的 `IRenderPass` 序列。
- 借助 `FRenderGraphBuilderBridge` 将 Pass 的资源依赖声明转译为 RFG 的图节点与边。
- 驱动 RFG 编译图并录制命令。帧调度器负责 Acquire、Submit 和 Present。

简而言之，Renderer 是 **场景逻辑 → 渲染指令 → GPU 提交** 之间的 orchestration 层。

---

## 2. 核心概念

### 2.1 RenderScene

`FRenderScene` 是帧渲染时的一帧场景快照，仅包含渲染代理（Proxy），不含游戏逻辑对象：

| 成员 | 类型 | 说明 |
|------|------|------|
| `Meshes` | `LE::Array<FRenderMeshProxy>` | 经主相机剔除并确定性排序的绘制代理，携带实体标识、变换、包围盒、材质 UUID 和借用的 Mesh/Material 引用 |
| `ShadowCasters` | `LE::Array<FRenderShadowCaster>` | 启用方向光时从相机剔除前的几何收集 Mesh 引用、世界变换和包围盒 |
| `Lights` | `LE::Array<FRenderLightProxy>` | 零或一个方向光，携带世界空间朝光方向与线性 RGB 强度 |
| `Cameras` | `LE::Array<FRenderCameraProxy>` | 恰好一个 primary camera，复制 View/Projection/ViewProjection、primitive ID 与 viewport |

`FMaterialAsset` 包含 `ERenderMeshPassMask`，用于判断网格能否参与相应的颜色 Pass。

### 2.2 RenderView

`FRenderView` 描述一个相机视角的渲染参数；`FRenderViewFamily` 则将多个视图及共享的渲染目标绑定在一起：

| 结构/成员 | 说明 |
|-----------|------|
| `FRenderView::Viewport` | `FRHIViewport`，视口矩形与深度范围 |
| `FRenderView::Profile` | `ERenderPlatformProfile`（`Desktop` / `Mobile`） |
| `FRenderView::ShadingPath` | `EShadingPath`（`Deferred` / `Forward`） |
| `FRenderView::bIsEditorView` | 是否为编辑器视图 |
| `FRenderTargetBinding` | BackBuffer、SceneColor、DepthStencil 的 `FRHITextureView*` 绑定 |
| `FRenderViewFamily` | 视图数组 + 主 Swapchain + 渲染目标绑定 |

### 2.3 RenderPass

`IRenderPass` 是渲染通道的最小可执行单元，采用 **两阶段模型**：

- **Setup 阶段**：声明资源读写依赖（通过 `FRenderGraphBuilderBridge`），RFG 据此构建依赖图。
- **Record 阶段**：在 RFG 执行回调中录制具体的 RHI 命令（`FRHICommandList`）。

```cpp
class IRenderPass {
    virtual const char* GetPassName() const = 0;
    virtual ERFGQueueType GetQueueType() const = 0;
    virtual void Setup(FRenderPassSetupContext& Context) = 0;
    virtual void Record(FRenderPassRecordContext& Context) = 0;
};
```

`FSimpleRenderPass` 提供了基于 `std::function` 的便捷实现，用于快速原型。

### 2.4 RenderPipeline

`IRenderPipeline` 负责根据场景、视图和帧上下文 **动态规划 Pass 序列**：

```cpp
class IRenderPipeline {
    virtual const char* GetName() const = 0;
    virtual EShadingPath GetShadingPath() const = 0;
    virtual void BuildPasses(const FRendererFrameContext&, const FRenderScene&,
                             const FRenderView&, FRenderPipelinePlan& OutPlan) = 0;
};
```

`FRenderPipelinePlan` 是一个简单的 `IRenderPass*` 数组，由 `FRenderer::RenderFrame` 消费并转化为 RFG 节点。

---

## 3. 架构分层

```
+--------------------------+
|        Renderer          |  <- 本模块（场景/视图/管线抽象）
|  FRenderer               |
|  IRenderPipeline         |
|  IRenderPass             |
|  FRenderScene            |
|  FRenderView             |
+--------------------------+
|    RFG Bridge            |  <- FRenderGraphBuilderBridge
|  资源导入 / Read / Write  |
+--------------------------+
|    RFG (Frame Graph)     |  <- FRFGInstance / FRFGBuilder
|  图构建 / 编译 / 调度 / 执行 |
+--------------------------+
|    RHI (Abstraction)     |  <- FRHICommandList / FRHIDevice
|  跨平台 GPU 指令抽象      |
+--------------------------+
|    Platform Backend      |  <- Vulkan / D3D12 / Metal ...
+--------------------------+
```

- **Renderer**：面向场景，定义高层概念。
- **RFG Bridge**：解耦 Renderer 与 RFG，避免 Renderer 直接依赖 RFG 内部类型；同时缓存已导入的 RHI 资源句柄，防止重复导入。
- **RFG**：负责资源生命周期、同步屏障、执行顺序优化。
- **RHI**：负责命令列表、管线状态、资源描述的跨平台抽象。

---

## 4. 主要类说明

### 4.1 `FRenderer`（Renderer.h）

编排和录制渲染图的实例。

| 方法 | 说明 |
|------|------|
| `Initialize()` | 初始化 `FRFGPassRegistry`、`FRFGRuntime`、`FRFGInstance` |
| `Shutdown()` | 逆序关闭 |
| `RenderFrame(FrameContext, RenderScene)` | 每帧录制入口：构建管线计划 → 创建 RFG Builder → 遍历 Pass Setup/Record → 编译图；将 RFG-owned transient 转交当前 frame scope，不负责 Acquire、Submit 或 Present |
| `GetGraphInstance()` | 暴露底层 `FRFGInstance`，供外部直接操作 |

当前 `RenderFrame` 仅处理 `ViewFamily.Views` 的第一个视图（`front()`），多视图支持尚未实现。

### 4.2 `FRenderScene`（RenderScene.h）

帧级场景代理容器。WorldRenderer 将它封装在只读
`FWorldRenderSnapshot` 中；Renderer 只在当前 frame callback 内同步读取。
`FRenderPrimitiveId` 是两个 `uint64` 组成的 Renderer-neutral strong value，
`FRenderBounds` 是默认 invalid、显式校验 finite/min-max 的 world-space AABB。

### 4.3 `FRenderView` / `FRenderViewFamily`（RenderView.h）

视图参数与目标绑定的载体。当前 `ERenderPlatformProfile` 和 `EShadingPath` 仅作标记，尚未影响实际管线行为。

### 4.4 `IRenderPass` / `FSimpleRenderPass`（RenderPass.h / SimpleRenderPass.h）

`IRenderPass` 是所有自定义 Pass 的基类。`FSimpleRenderPass` 通过回调减少样板代码：

```cpp
FSimpleRender Pass Pass("MyPass", ERFGQueueType::Graphics,
    [](FRenderPassSetupContext& Ctx){ /* import/write resources */ },
    [](FRenderPassRecordContext& Ctx){ /* record commands */ });
```

### 4.5 `IRenderPipeline` / `FSimpleRenderPipeline`（RenderPipeline.h / SimpleRenderPipeline.h）

`FSimpleRenderPipeline` 是静态 Pass 列表的包装器，适合固定管线原型：

```cpp
FSimpleRenderPipeline Pipeline("Simple", EShadingPath::Deferred);
Pipeline.AddPass(&PassA);
Pipeline.AddPass(&PassB);
```

### 4.6 `FRenderGraphBuilderBridge`（RenderGraphBuilderBridge.h）

桥接 Renderer 与 RFG 的关键类，职责：

- `ImportTexture` / `ImportBuffer`：将 `FRHITexture*` / `FRHIBuffer*` 导入 RFG，返回 `FRFGResourceHandle`；重复导入会返回已缓存的句柄。
- `Read` / `Write`：在 Pass 的 Setup 阶段声明资源访问。
- `MarkOutput`：标记图的外部输出资源（如 BackBuffer）。

---

## 5. BasePassCompositePipeline 与 SimpleRenderPipeline

### 5.1 正式 BasePass vertical slice

`FSceneRenderer` 是默认场景管线入口，持有着色器模块、管线、布局、SceneColor 和
SceneDepth 附件、Composite 绑定及帧槽光照资源。应用提供设备、交换链、着色器字节码
和场景快照，把当前帧作用域交给 `RecordFrame`。调整尺寸时先构建候选附件与
Composite 资源，等待已提交帧，再替换原资源。

`FBasePassCompositePipeline` 先录制可选的 ShadowDepth，再录制 BasePass 和 Composite。
BasePass 写 SceneColor 和 SceneDepth，Composite 将 SceneColor 输出到 BackBuffer。

#### `FBasePass`

`FRenderAssetProvider` 将已解码的 CPU 网格与材质转换为 BasePass payload。
网格上传位置、白色顶点色、UV、法线和索引。旧网格使用默认 UV 或法线值。
引用颜色纹理的材质要求网格提供源 UV。Renderer 创建共享的材质布局（set 0）。

首次请求一个纹理 ID 时，provider 创建 host-visible staging 和单 mip RGBA8 sRGB
图像，在 Graphics queue 录制 `Undefined → CopyDestination → Copy → ShaderResource`
并等待专用 fence。确认完成后才创建 view、加入会话缓存并构造材质绑定。若完成状态
不可确认，提交所用的 staging、命令列表、allocator、fence 与图像保持存活。后续
上传会先查询是否完成，结束会话时通过受检 fence/终止队列等待回收。

同一会话的材质按纹理 UUID 共享 image/view，各有独立的 bind group。无纹理引用的材质
绑定共享白纹理，共用 nearest / clamp-to-edge sampler 和 LOD 0。先释放材质绑定再释放
需要的纹理引用，最后一个引用释放时回收纹理。应用在提交帧完成后销毁会话，再销毁
pipeline、layout 和设备。固定附件格式支持在 resize 时复用材质与 pipeline。

- Preflight 在产生计划前验证唯一 primary camera、B8G8R8A8_SRGB SceneColor、D32_FLOAT
  depth、extent、opaque blend/depth contract、slot0 vertex binding、VB/IB usage/size、R16/R32
  index format、有效的持久 Material ID、与 pipeline 匹配的材质 bind group 及 sRGB sampled image。
- Setup 导入 SceneColor/depth、VB/IB 和采样纹理。对共享纹理声明一次 Pixel ShaderResource 读取，
  导入初始状态也是 ShaderResource。附件和 VB/IB 保持各自的状态。
- Record 先选择 BasePass 管线，清除 color/depth 并设置 viewport/scissor，再按快照顺序
  绑定材质、帧级灯光和顶点/索引缓冲。每次绘制上传 128 字节的 LocalToClip、BaseColor
  和逆转置法线行。
- 光照使用 mesh v3 的显式顶点法线。Base Color 在 sRGB 纹理解码后进入线性 Lambert 漫反射，
  无启用光源或无作者法线的 draw 沿用原颜色公式。场景灯光和像素基准见 [DirectionalLight.md](DirectionalLight.md)。
- frame slot 各自持有一个 32 字节 UniformBuffer 和绑定组。槽 fence 完成后写入该帧灯光或停用值，
  BasePass 声明读取 set 1 binding 0，材质继续用 set 0。调整窗口尺寸不重建灯光缓冲。
  顶点着色器逐行点乘计算 clip position。像素着色器线性乘以顶点色、材质色和 sRGB 解码
  后的采样颜色，输出 alpha 为 1。
- 零可见 mesh 仍是有效计划，BasePass 仍清除 SceneColor/depth。

#### `FDirectionalShadowPass`

- 将投影体的世界包围盒投影到光源空间，建立单方向光的正交覆盖范围。使用独立于主相机
  可见网格的投影体列表，支持屏幕外物体向屏幕内投影。
- 每个帧槽保存一张 1024×1024 深度纹理和该帧的接收端常量。RFG 根据 ShadowDepth
  写入和 BasePass 读取声明处理深度附件到采样状态的转换。BasePass 用深度比较调节
  Lambert 直射光。立体场景与 Win64 取样位置见 [SolidGeometry.md](SolidGeometry.md)。

#### `FSceneColorCompositePass`

- Setup 只读取同一 SceneColor 并写/输出 BackBuffer，因此真实的 BasePass→Composite
  依赖和 RenderTarget→ShaderRead barrier 完全由 RFG 推导；Composite 不读取 depth。
- Record 仅使用 RHI render pass、pipeline、bind group 与 fullscreen draw，不含 Vulkan API。
- 即使 BasePass 零可见 mesh，Composite 仍采样并输出 clear SceneColor。

### 5.2 SimpleRenderPipeline

`FSimpleRenderPipeline` 是一个通用容器，内部维护 `std::vector<IRenderPass*>`。`BuildPasses` 时直接复制该列表到 `FRenderPipelinePlan`。它适合作为自定义管线的基座，但当前尚无正式的生产级管线基于它构建。

---

## 6. PlatformRenderProfile

`ERenderPlatformProfile` 是一个枚举，定义于 `PlatformRenderProfile.h`：

```cpp
enum class ERenderPlatformProfile : uint8 {
    Desktop = 0,
    Mobile,
};
```

**用途**：标记当前视图的目标平台特性（如性能等级、功能集、分辨率策略）。`FRenderView` 携带该字段，供未来管线根据平台差异选择不同的着色路径、分辨率缩放或功能裁剪。当前代码中尚未产生实际分支逻辑。

---

## 7. 依赖关系

`Build.ts` 声明了以下公开依赖：

| 模块 | 用途 |
|------|------|
| **Core** | 基础类型（`LE::String`、`LE::FNonCopyable`、平台宏） |
| **Asset** | persistent asset IDs 与 opaque Mesh/Material logical identities |
| **RHI** | 渲染硬件接口（`FRHICommandList`、`FRHIDevice`、`FRHISwapchain`、`FRHITexture`、`FRHIBuffer` 等） |
| **RFG** | 渲染帧图（`FRFGInstance`、`FRFGBuilder`、`FRFGPassHandle`、`FRFGResourceHandle`、`FRFGPassContext` 等） |

Renderer 自身被更高层模块（如 Game、Editor）依赖，作为渲染调用的统一入口。

### 7.1 两槽帧调度

`FRenderFrameScheduler` 固定维护两个 frame slot。每槽独占 command allocator/list、acquire
semaphore、render-finished semaphore、initially-signaled completion fence 与 deferred
release 队列。槽复用顺序为 fence wait → creator-side deferred release → allocator reset →
Acquire；同步 record callback 返回后，由 scheduler 自己执行 fence reset、精确 SubmitInfo
提交和 Present，调用方不能伪造 InFlight 状态。

`FRenderFrameScope` 是 callback 期间的 borrowed view，并提供 generation-checked
`DeferRelease` ownership sink；callback 返回后不可再向旧槽投递。正常成功帧不调用
`WaitIdle`。resize/shutdown 会等待实际 InFlight 槽；submit 失败留下的 unsignaled fence 不会
被等待，terminal shutdown 才使用 exceptional queue idle。

`FRenderer::RenderFrame` 只负责构建和录制 RFG，不再提交或 Present。Renderer 在调用栈内用
backend-neutral borrowed `IRFGDeferredReleaseSink` 适配当前 `FRenderFrameScope`；RFG-owned
texture/buffer ownership 在录制完成后、提交前转入 exact frame slot。imported resources 不转移，
sink 缺失或拒绝时不会提交，未转移资源仍由 RFG 精确清理。

---

## 8. 当前限制

1. **BasePass 仍是最小 vertical slice**：生产级 `Forward+`、`Deferred`、透明与多 Pass 材质尚未实现；`EShadingPath` 枚举存在但未驱动选择。
2. **材质范围**：当前支持 opaque base color 和单张颜色纹理。纹理参数表、材质实例和通用 shader variant 仍待扩展。
3. **场景数据范围**：已有主相机、CPU 视锥剔除、单方向光和单张方向光阴影图。LOD 与 occlusion culling 留给后续阶段。
4. **单视图限制**：`FRenderer::RenderFrame` 仅取 `ViewFamily.Views.front()`，多视口、分屏和级联阴影仍待扩展。
5. **同步资产路径**：Asset registry 和 WorldRenderer snapshot build 仍同步执行，没有异步加载或 render thread。
6. **无后处理框架**：无 Bloom、Tone Mapping、AA 等后处理 Pass 的标准注册与组合机制。
7. **Transient 暂无跨帧复用池**：RFG-owned transient 已绑定 exact frame slot 并在 fence 完成后安全销毁，但尚未实现跨帧 aliasing/复用池或显存预算策略。

---

## 9. 未来扩展方向

### 9.1 着色路径
- 实现正式的 **Deferred Shading** 管线（GBuffer Pass → Lighting Pass → Shading Pass）。
- 实现 **Forward+**（Tiled/Clustered Forward）管线，适配透明材质和 MSAA 需求。
- 根据 `EShadingPath` 和 `ERenderPlatformProfile` 自动选择管线。

### 9.2 材质系统
- 引入 `FMaterial` / `FMaterialInstance` 抽象，管理 Shader、纹理采样器、常量缓冲。
- 根据材质与 Pass 选择着色变体及相应管线。
- 支持 Shader 变体（Keyword / Feature Level）和运行时热重载。

### 9.3 后处理
- 构建 `PostProcessChain`，支持按顺序注入 Tonemapping、Bloom、SSAO、TAA、FSR/DLSS 等 Pass。
- 定义后处理体积（Post Process Volume）与混合权重。

### 9.4 场景与剔除
- 扩展 `FRenderLightProxy`，支持其他光源类型、位置和阴影参数。
- 在现有 CPU frustum culling 之后增加 LOD、occlusion culling 与批处理。

### 9.5 多视图与并行
- 支持 `FRenderViewFamily` 的多视图渲染（分屏、级联阴影、反射探针）。
- 在其余调用方迁移到 `FRenderFrameScheduler` 后，淘汰 RFG 的 legacy immediate-submit / `WaitIdle` 兼容入口。

---

## 10. 文件索引

| 文件 | 说明 |
|------|------|
| `Public/Renderer/Renderer.h` | `FRenderer` 主类 |
| `Public/Renderer/SceneRenderer.h` | 默认场景管线、附件调整和帧录制入口 |
| `Public/Renderer/RenderScene.h` | `FRenderScene` 和网格、投影体、方向光、相机代理 |
| `Public/Renderer/RenderView.h` | `FRenderView`、`FRenderViewFamily`、`FRenderTargetBinding` |
| `Public/Renderer/RenderPass.h` | `IRenderPass`、`FRenderPassSetupContext`、`FRenderPassRecordContext` |
| `Public/Renderer/RenderPipeline.h` | `IRenderPipeline`、`FRenderPipelinePlan` |
| `Public/Renderer/RenderGraphBuilderBridge.h` | `FRenderGraphBuilderBridge` |
| `Public/Renderer/RendererFrameContext.h` | `FRendererFrameContext` |
| `Public/Renderer/RenderFrameScheduler.h` | 两槽帧调度、borrowed frame scope 与 deferred-release sink |
| `Public/Renderer/PlatformRenderProfile.h` | `ERenderPlatformProfile` |
| `Public/Renderer/ShadingPath.h` | `EShadingPath` |
| `Public/Renderer/SimpleRenderPass.h` | `FSimpleRenderPass` |
| `Public/Renderer/SimpleRenderPipeline.h` | `FSimpleRenderPipeline` |
| `Public/Renderer/BasePass.h` | BasePass attachment、indexed draw、128 字节对象常量和 32 字节帧灯光常量 |
| `Public/Renderer/RenderAssetProvider.h` | BasePass 网格、材质、同步纹理上传及会话缓存 |
| `Public/Renderer/BasePassCompositePipeline.h` | 默认 Pass 排序与绑定 |
| `Public/Renderer/RendererMinimal.h` | 模块统一头文件（聚合包含） |
| `Private/Renderer/Renderer.cpp` | `FRenderer` 实现 |
| `Private/Renderer/RenderFrameScheduler.cpp` | 两槽 Acquire/record/Submit/Present 状态机 |
| `Private/Renderer/RenderGraphBuilderBridge.cpp` | `FRenderGraphBuilderBridge` 实现 |
| `Private/Renderer/Passes/BasePass.cpp` | BasePass preflight、RFG Setup 与 RHI Record |
| `Private/Renderer/Passes/DirectionalShadow.cpp` | 阴影帧槽资源、光源正交变换和深度 Pass |
| `Private/Renderer/Passes/SceneColorCompositePass.h` | backend-neutral SceneColor composite pass |
| `Private/Renderer/Pipelines/BasePassCompositePipeline.cpp` | ShadowDepth、BasePass、Composite 计划构建 |
