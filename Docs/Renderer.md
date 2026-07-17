# Renderer 模块设计文档

## 1. 模块职责

Renderer 是引擎的高层渲染模块，基于 **RFG（Render Frame Graph）** + **RAL（Render Abstraction Layer）** 构建可扩展的渲染管线。

其主要职责包括：

- 接收游戏线程提交的场景描述（`FRenderScene`）和视图参数（`FRenderView`）。
- 通过可插拔的 `IRenderPipeline` 将高层渲染意图转换为有序的 `IRenderPass` 序列。
- 借助 `FRenderGraphBuilderBridge` 将 Pass 的资源依赖声明转译为 RFG 的图节点与边。
- 驱动 RFG 完成图的编译与执行，最终输出到 `FRALSwapchain`。

简而言之，Renderer 是 **场景逻辑 → 渲染指令 → GPU 提交** 之间的 orchestration 层。

---

## 2. 核心概念

### 2.1 RenderScene

`FRenderScene` 是帧渲染时的一帧场景快照，仅包含渲染代理（Proxy），不含游戏逻辑对象：

| 成员 | 类型 | 说明 |
|------|------|------|
| `Meshes` | `std::vector<FRenderMeshProxy>` | 网格渲染代理，含 `GraphicsPipeline`、`VertexBuffer`、`VertexCount`、`PassMask`、`SortKey` |
| `Lights` | `std::vector<FRenderLightProxy>` | 光源代理（当前为空壳） |
| `Cameras` | `std::vector<FRenderCameraProxy>` | 相机代理（当前为空壳） |

`ERenderMeshPassMask` 定义了网格可参与的目标通道：`BackBuffer`、`SceneColor`。

### 2.2 RenderView

`FRenderView` 描述一个相机视角的渲染参数；`FRenderViewFamily` 则将多个视图及共享的渲染目标绑定在一起：

| 结构/成员 | 说明 |
|-----------|------|
| `FRenderView::Viewport` | `FRALViewport`，视口矩形与深度范围 |
| `FRenderView::Profile` | `ERenderPlatformProfile`（`Desktop` / `Mobile`） |
| `FRenderView::ShadingPath` | `EShadingPath`（`Deferred` / `Forward`） |
| `FRenderView::bIsEditorView` | 是否为编辑器视图 |
| `FRenderTargetBinding` | BackBuffer、SceneColor、DepthStencil 的 `FRALTextureView*` 绑定 |
| `FRenderViewFamily` | 视图数组 + 主 Swapchain + 渲染目标绑定 |

### 2.3 RenderPass

`IRenderPass` 是渲染通道的最小可执行单元，采用 **两阶段模型**：

- **Setup 阶段**：声明资源读写依赖（通过 `FRenderGraphBuilderBridge`），RFG 据此构建依赖图。
- **Record 阶段**：在 RFG 执行回调中录制具体的 RAL 命令（`FRALCommandList`）。

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
|    RAL (Abstraction)     |  <- FRALCommandList / FRALDevice
|  跨平台 GPU 指令抽象      |
+--------------------------+
|    Platform Backend      |  <- Vulkan / D3D12 / Metal ...
+--------------------------+
```

- **Renderer**：面向场景，定义高层概念。
- **RFG Bridge**：解耦 Renderer 与 RFG，避免 Renderer 直接依赖 RFG 内部类型；同时缓存已导入的 RAL 资源句柄，防止重复导入。
- **RFG**：负责资源生命周期、同步屏障、执行顺序优化。
- **RAL**：负责命令列表、管线状态、资源描述的跨平台抽象。

---

## 4. 主要类说明

### 4.1 `FRenderer`（Renderer.h）

模块入口类，单例式生命周期管理：

| 方法 | 说明 |
|------|------|
| `Initialize()` | 初始化 `FRFGPassRegistry`、`FRFGRuntime`、`FRFGInstance` |
| `Shutdown()` | 逆序关闭 |
| `RenderFrame(FrameContext, RenderScene)` | 每帧主入口：构建管线计划 → 创建 RFG Builder → 遍历 Pass Setup/Record → 编译并执行图 → Present |
| `GetGraphInstance()` | 暴露底层 `FRFGInstance`，供外部直接操作 |

当前 `RenderFrame` 仅处理 `ViewFamily.Views` 的第一个视图（`front()`），多视图支持尚未实现。

### 4.2 `FRenderScene`（RenderScene.h）

帧级场景代理容器。`Reset()` 用于清空上一帧数据。

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

- `ImportTexture` / `ImportBuffer`：将 `FRALTexture*` / `FRALBuffer*` 导入 RFG，返回 `FRFGResourceHandle`；重复导入会返回已缓存的句柄。
- `Read` / `Write`：在 Pass 的 Setup 阶段声明资源访问。
- `MarkOutput`：标记图的外部输出资源（如 BackBuffer）。

---

## 5. DemoRenderPipelines 与 SimpleRenderPipeline 现状

### 5.1 Demo 管线

当前引擎仅提供两条演示管线，用于验证 RFG + RAL 链路：

| 管线 | 名称 | Pass 数 | 说明 |
|------|------|---------|------|
| `FTriangleBackBufferPipeline` | `TriangleBackBufferPipeline` | 1 | 直接将场景网格绘制到 Swapchain BackBuffer |
| `FTriangleCompositePipeline` | `TriangleCompositePipeline` | 2 | 先绘制到 Offscreen `SceneColor` 纹理，再通过全屏 Composite Pass 采样并输出到 BackBuffer |

#### `FTriangleBackBufferPass`
- Setup：导入 Swapchain BackBuffer，标记 Write + Output。
- Record：筛选 `PassMask` 含 `BackBuffer` 的网格，按 `SortKey` 排序，设置 Viewport/Scissor，绑定图形管线并绘制。

#### `FOffscreenTrianglePass`
- Setup：导入外部创建的 `SceneColor` 纹理，标记 Write。
- Record：筛选 `PassMask` 含 `SceneColor` 的网格，绘制到 `SceneColorView`。

#### `FCompositePass`
- Setup：读取 `SceneColor`，写入 BackBuffer，标记 Output。
- Record：手动插入 Vulkan `vkCmdPipelineBarrier` 将 `SceneColor` 从 `COLOR_ATTACHMENT_OPTIMAL` 转换到 `SHADER_READ_ONLY_OPTIMAL`，然后绑定 Composite 管线与 BindGroup，绘制全屏三角形（3 个顶点）。

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

`Renderer.Build.cs` 声明了以下公开依赖：

| 模块 | 用途 |
|------|------|
| **Core** | 基础类型（`FString`、`FNonCopyable`、平台宏） |
| **RAL** | 渲染抽象层（`FRALCommandList`、`FRALDevice`、`FRALSwapchain`、`FRALTexture`、`FRALBuffer` 等） |
| **RFG** | 渲染帧图（`FRFGInstance`、`FRFGBuilder`、`FRFGPassHandle`、`FRFGResourceHandle`、`FRFGPassContext` 等） |

Renderer 自身被更高层模块（如 Game、Editor）依赖，作为渲染调用的统一入口。

---

## 8. 当前限制

1. **仅有 Demo 管线**：生产级 `Forward+`、`Deferred` 管线尚未实现；`EShadingPath` 枚举存在但无对应管线类。
2. **无正式材质系统**：`FRenderMeshProxy` 直接持有 `FRALPipeline_Graphics*` 和 `FRALBuffer*`，没有材质、Shader、参数表的抽象。
3. **场景数据极简**：`FRenderLightProxy`、`FRenderCameraProxy` 为空壳；无 Transform、Bounds、LOD、剔除系统。
4. **单视图限制**：`FRenderer::RenderFrame` 仅取 `ViewFamily.Views.front()`，多视口/分屏/Shadow Cascade 不支持。
5. **平台特定代码侵入**：`TriangleCompositePasses.h` 中直接调用了 Vulkan API（`vkCmdPipelineBarrier`），破坏了 RAL 的抽象边界。
6. **无后处理框架**：无 Bloom、Tone Mapping、AA 等后处理 Pass 的标准注册与组合机制。
7. **同步模型粗糙**：`RenderFrame` 使用 `bSubmitImmediately = true` + `bWaitForCompletion = true` 的阻塞执行，未利用 CPU/GPU 并行。

---

## 9. 未来扩展方向

### 9.1 着色路径
- 实现正式的 **Deferred Shading** 管线（GBuffer Pass → Lighting Pass → Shading Pass）。
- 实现 **Forward+**（Tiled/Clustered Forward）管线，适配透明材质和 MSAA 需求。
- 根据 `EShadingPath` 和 `ERenderPlatformProfile` 自动选择管线。

### 9.2 材质系统
- 引入 `FMaterial` / `FMaterialInstance` 抽象，管理 Shader、纹理采样器、常量缓冲。
- 在 `FRenderMeshProxy` 中替换裸 `FRALPipeline_Graphics*` 为材质引用。
- 支持 Shader 变体（Keyword / Feature Level）和运行时热重载。

### 9.3 后处理
- 构建 `PostProcessChain`，支持按顺序注入 Tonemapping、Bloom、SSAO、TAA、FSR/DLSS 等 Pass。
- 定义后处理体积（Post Process Volume）与混合权重。

### 9.4 场景与剔除
- 填充 `FRenderCameraProxy`（View/Projection 矩阵、FOV、Near/Far）。
- 填充 `FRenderLightProxy`（类型、位置、方向、颜色、强度、阴影参数）。
- 引入 CPU 端 Frustum/Occlusion Culling，生成可见性列表后再构建 `FRenderScene`。

### 9.5 多视图与并行
- 支持 `FRenderViewFamily` 的多视图渲染（Split Screen、Shadow Maps、Reflection Probes）。
- 将 `bWaitForCompletion` 改为基于 Fence/Semaphore 的帧间流水线，提升 CPU/GPU 重叠度。

### 9.6 跨平台清理
- 移除 Demo Pass 中的裸 Vulkan 调用，统一通过 RAL 的屏障/Transition API 表达资源状态变更。

---

## 10. 文件索引

| 文件 | 说明 |
|------|------|
| `Public/Renderer/Renderer.h` | `FRenderer` 主类 |
| `Public/Renderer/RenderScene.h` | `FRenderScene`、`FRenderMeshProxy`、`FRenderLightProxy`、`FRenderCameraProxy` |
| `Public/Renderer/RenderView.h` | `FRenderView`、`FRenderViewFamily`、`FRenderTargetBinding` |
| `Public/Renderer/RenderPass.h` | `IRenderPass`、`FRenderPassSetupContext`、`FRenderPassRecordContext` |
| `Public/Renderer/RenderPipeline.h` | `IRenderPipeline`、`FRenderPipelinePlan` |
| `Public/Renderer/RenderGraphBuilderBridge.h` | `FRenderGraphBuilderBridge` |
| `Public/Renderer/RendererFrameContext.h` | `FRendererFrameContext` |
| `Public/Renderer/PlatformRenderProfile.h` | `ERenderPlatformProfile` |
| `Public/Renderer/ShadingPath.h` | `EShadingPath` |
| `Public/Renderer/SimpleRenderPass.h` | `FSimpleRenderPass` |
| `Public/Renderer/SimpleRenderPipeline.h` | `FSimpleRenderPipeline` |
| `Public/Renderer/DemoRenderPipelines.h` | `FTriangleBackBufferPipeline`、`FTriangleCompositePipeline` |
| `Public/Renderer/RendererMinimal.h` | 模块统一头文件（聚合包含） |
| `Private/Renderer/Renderer.cpp` | `FRenderer` 实现 |
| `Private/Renderer/RenderGraphBuilderBridge.cpp` | `FRenderGraphBuilderBridge` 实现 |
| `Private/Renderer/Pipelines/DemoRenderPipelines.cpp` | Demo 管线实现 |
| `Private/Renderer/Passes/TriangleBackBufferPass.h` | BackBuffer 绘制 Pass |
| `Private/Renderer/Passes/TriangleCompositePasses.h` | Offscreen + Composite Pass |
