# RAL（Render Abstraction Layer）设计文档

## 1. 模块职责

RAL 是 Limitless Engine 的**跨平台渲染 API 抽象层**，负责将底层图形 API（当前仅 Vulkan）封装为与平台无关的接口，供上层渲染模块（Renderer）使用。其目标包括：

- 屏蔽 Vulkan/D3D12/OpenGL 等底层 API 差异，提供统一的资源与执行模型。
- 以现代图形 API 概念（BindGroup、Pipeline、CommandList）为核心，避免遗留固定管线包袱。
- 集中管理 GPU 资源生命周期、队列提交、同步与窗口呈现。

> **当前状态**：仅实现 Vulkan 后端，平台支持 Windows（Win32 Surface）与 macOS（Metal Surface via MoltenVK）。

---

## 2. 核心设计原则

### 2.1 现代图形 API 抽象

RAL 的设计紧贴 Vulkan/D3D12 风格，核心抽象包括：

| 抽象概念 | 说明 |
|---------|------|
| **FRALDevice** | GPU 设备入口，负责所有资源的创建与全局状态管理。 |
| **FRALQueue** | 执行提交队列，封装命令缓冲的异步提交与等待。 |
| **FRALCommandList** | 命令列表，记录渲染/计算/拷贝命令。 |
| **FRALPipeline_Graphics** | 图形管线对象，封装 Shader、顶点布局、光栅化/深度/混合状态。 |
| **FRALBindGroup / FRALBindGroupLayout** | 描述符集及其布局，管理 Shader 资源绑定。 |

### 2.2 资源继承体系

所有 GPU 资源均继承自 `FRALResource`（派生自 `FNonCopyable`），提供统一的禁止拷贝语义与可选的 Bindless 索引支持：

```
FRALResource
├── FRALDevice
├── FRALQueue
├── FRALCommandList
├── FRALBuffer
├── FRALTexture
├── FRALTextureView
├── FRALShader
├── FRALPipeline_Graphics
├── FRALBindGroupLayout
├── FRALBindGroup
├── FRALSampler
├── FRALSwapchain
├── FRALSemaphore
└── FRALFence
```

### 2.3 Bindless 支持（实验性）

`FRALDevice` 暴露 `AllocateBindlessIndex()` 与 `GetBindlessHeapGPUDescriptor()`，用于支持基于全局描述符索引的纹理访问。当前 Vulkan 后端在设备初始化时检测 `VK_EXT_descriptor_indexing` 或 Vulkan 1.2 核心支持，并创建一个大型的 `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` 描述符集。

---

## 3. 类型系统总览

### 3.1 像素格式（EPixelFormat）

RAL 定义了引擎内统一的像素格式枚举，后端负责映射到原生 API 格式：

| 枚举值 | 说明 |
|-------|------|
| `R8_UNORM` / `R8_SNORM` / `R8_UINT` / `R8_SINT` | 单通道 8-bit |
| `R32_UINT` | 单通道 32-bit 整数（常用于索引） |
| `R8G8B8A8_UNORM` / `R8G8B8A8_SRGB` / `B8G8R8A8_SRGB` | 四通道 8-bit（含 SRGB） |
| `R16G16_FLOAT` / `R16G16B16A16_FLOAT` | 半精度浮点 |
| `R32_FLOAT` / `R32G32_FLOAT` / `R32G32B32_FLOAT` / `R32G32B32A32_FLOAT` | 全精度浮点 |
| `D32_FLOAT` / `D24_UNORM_S8_UINT` | 深度 / 深度模板 |

### 3.2 Shader 阶段（EShaderStage）

采用位掩码设计，支持按位或组合：

- `Vertex`、`Pixel`、`Compute`、`Geometry`、`Hull`、`Domain`
- 组合值：`Graphics = Vertex | Pixel | Geometry | Hull | Domain`，`AllStage = Graphics | Compute`

### 3.3 资源用途（EResourceUsage）

| 枚举值 | 说明 |
|-------|------|
| `Local` | GPU 本地显存，CPU 不可直接读写，访问速度最快。 |
| `Upload` | CPU 写、GPU 读，适用于 Uniform Buffer 与动态顶点数据。 |
| `Readback` | GPU 写、CPU 读，适用于回读缓冲区。 |

### 3.4 其他关键枚举

- **ECullMode** / **EFillMode**：光栅化剔除与填充模式。
- **ECompareFunction**：深度/模板比较函数。
- **ESamplerFilter** / **ESamplerMipmapMode** / **ESamplerAddressMode**：采样器滤波与寻址模式。
- **EAttachmentLoadOp** / **EAttachmentStoreOp**：Render Pass 附件加载/存储操作。
- **EBufferUsageFlags**：Buffer 用途标志（顶点、索引、Uniform、Storage、间接参数、传输等）。

---

## 4. 资源抽象类说明

### 4.1 Buffer（FRALBuffer）

- **接口**：`Map(uint64 Offset, uint64 Size)` / `Unmap()` 提供 CPU 内存映射；`GetDesc()` 返回描述符。
- **描述符（FRALBufferDesc）**：`Name`、`Size`、`Usage`（EResourceUsage）、`UsageFlag`（EBufferUsageFlags 位掩码）。
- **Vulkan 实现**：`FVulkanRALBuffer` 创建 `VkBuffer` + `VkDeviceMemory`，根据 `EResourceUsage` 选择对应 `VkMemoryPropertyFlags`。

### 4.2 Texture / TextureView（FRALTexture / FRALTextureView）

- **FRALTexture**：不可直接用于 Shader 绑定，需通过 `FRALTextureView` 创建视图。
  - **描述符（FRALTextureDesc）**：尺寸（Width/Height/Depth）、MipLevels、ArrayLayers、Format，以及用途标志（`bIsUAV`、`bIsRenderTarget`、`bIsDepthStencil`、`bIsShaderResource`）。
- **FRALTextureView**：对 Texture 的某一 Mip/Array 层级创建视图。
  - **描述符（FRALTextureViewDesc）**：引用目标 Texture、Format（可覆写）、MipSlice/ArraySlice 与层级数。
- **Vulkan 实现**：`FVulkanRALTexture` 管理 `VkImage` + `VkDeviceMemory`；`FVulkanRALTextureView` 管理 `VkImageView`。Swapchain 后备缓冲使用不拥有 Image 所有权的特殊构造路径。

### 4.3 Shader（FRALShader）

- **接口**：`GetDesc()` 返回 `FRALShaderDesc`，内含 Stage、EntryPoint、ByteCode、ByteCodeSize 与资源绑定反射信息（`Bindings` 数组）。
- **描述符（FRALShaderDesc）**：`Name`、`Stage`、`Bindings`（`FRALShaderResourceBinding` 数组，含 Set/Binding/Count/Type/StageFlags）、`ByteCode`、`ByteCodeSize`、`EntryPoint`（默认 `"main"`）。
- **Vulkan 实现**：`FVulkanRALShader` 将 SPIR-V 字节码封装为 `VkShaderModule`。

### 4.4 Graphics Pipeline（FRALPipeline_Graphics）

- **接口**：`GetDesc()` 返回 `FRALPipelineDesc_Graphics`。
- **描述符（FRALPipelineDesc_Graphics）**：
  - Shader：VertexShader、PixelShader（暂仅支持 VS/PS）。
  - 顶点输入布局：`VertexBindings`（`FRALVertexInputBinding`，支持逐实例）+ `VertexAttributes`（`FRALVertexInputAttribute`，Location/Binding/Format/Offset）。
  - 固定状态：`BlendState`、`DepthStencilState`、`RasterizerState`。
  - BindGroup 布局：`BindGroupLayouts` 数组。
  - Render Target 格式：最多 8 个 `RenderTargetFormats` + `DepthStencilFormat`。
- **Vulkan 实现**：`FVulkanRALPipeline_Graphics` 创建 `VkPipelineLayout` 与基于 Dynamic Rendering 的 `VkPipeline`，管线描述可声明 0–8 个颜色附件以及可选的 `D32_FLOAT` / `D24_UNORM_S8_UINT` 深度模板附件。动态状态仅包含 Viewport 与 Scissor。

### 4.5 BindGroup / BindGroupLayout（FRALBindGroup / FRALBindGroupLayout）

- **FRALBindGroupLayout**：描述一组绑定槽位的类型与可见性。
  - **描述符（FRALBindGroupLayoutDesc）**：`Name`、`SetIndex`、`Bindings`（`FRALBindGroupLayoutItem` 数组，含 Binding/Count/Type/StageFlags）。
  - **类型（ERALBindGroupItemType）**：UniformBuffer、StorageBuffer、SampledImage、StorageImage、Sampler、CombinedImageSampler。
- **FRALBindGroup**：将实际资源（Buffer/TextureView/Sampler）与布局关联。
  - **描述符（FRALBindGroupDesc）**：`Name`、`Layout`、`Items`（`FRALBindGroupItem` 数组，含 Binding/Buffer/TextureView/Sampler/Offset/Range）。
- **Vulkan 实现**：`FVulkanRALBindGroupLayout` 创建 `VkDescriptorSetLayout`；`FVulkanRALBindGroup` 创建独立的 `VkDescriptorPool` 并分配 `VkDescriptorSet`，在构造时一次性写入描述符。

> **BindGroup Set 索引约定**：0 = Global，1 = Pass，2 = Material，3 = Object。

### 4.6 Sampler（FRALSampler）

- **描述符（FRALSamplerDesc）**：Min/Mag Filter、MipmapMode、AddressU/V/W、LOD 范围、各向异性开关与倍数、比较函数开关、BorderColor。
- **Vulkan 实现**：`FVulkanRALSampler` 创建 `VkSampler`。

### 4.7 Swapchain（FRALSwapchain）

- **接口**：
  - `AcquireNextImage(SignalSemaphore, Timeout)`：使用 caller-owned binary semaphore 显式取得后备缓冲，返回 `Success / Suboptimal / OutOfDate / Error` 与本帧 view。
  - `GetCurrentBackBufferView()`：仅在成功（含 Suboptimal）Acquire 与随后的 Present/Resize 之间返回当前 view，其余时间返回 null。
  - `Present(WaitSemaphore)`：等待 caller-owned render-finished semaphore 并返回同一组 backend-neutral 状态；每次调用都会消费当前 acquired 状态。
  - `Resize(uint32 Width, uint32 Height)`：由上层显式重建 Swapchain，清除 acquired 状态且不自动 Acquire。
- **描述符（FRALSwapchainDesc）**：`FPlatformSurface`（含 surface type 与 opaque 平台句柄）、尺寸、BackBufferFormat、`bEnableVsync`。
- **Vulkan 实现**：`FVulkanRALSwapchain` 只管理 `VkSurfaceKHR`、`VkSwapchainKHR`、后备 `VkImage` 与对应的 `FVulkanRALTextureView`。它不拥有逐帧 semaphore，不在构造、Resize 或 Present 后自动 Acquire，也不在 OutOfDate/Suboptimal 时偷偷重建。Vulkan 的 `VK_SUCCESS / VK_SUBOPTIMAL_KHR / VK_ERROR_OUT_OF_DATE_KHR / 其他错误` 被直接映射到公共状态，由上层决定继续本帧、重建或退出。

---

## 5. 执行抽象

### 5.1 Device（FRALDevice）

`FRALDevice` 是所有 GPU 操作的工厂与入口：

| 方法 | 说明 |
|-----|------|
| `GetGraphicsQueue()` | 获取图形队列（当前为 Graphics/Compute/Transfer 通用队列）。 |
| `CreateBuffer()` / `CreateTexture()` / `CreateShaderFromFile()` | 创建基础资源。 |
| `CreateGraphicsPipeline()` | 创建图形管线。 |
| `CreateCommandList()` | 创建命令列表，参数 `EQueueType` 目前仅用于标识，实际均使用 Graphics Queue Family。 |
| `CreateBinarySemaphore()` / `CreateFence()` | 创建 caller-owned 帧同步对象；Fence 可指定 initially signaled。 |
| `CreateSwapchain()` | 创建窗口 Swapchain。 |
| `CreateBindGroup()` / `CreateBindGroupLayout()` | 创建资源绑定组。 |
| `CreateSampler()` | 创建采样器。 |
| `GetBindlessHeapGPUDescriptor()` / `AllocateBindlessIndex()` | Bindless 支持接口。 |

工厂入口：`RAL::CreateDevice()`，当前硬编码返回 `FVulkanRALDevice`。所有由 RAL backend
创建并以 `FRALResource*` 暴露的对象必须通过 `RAL::DestroyResource()` 释放；该导出函数
在 RAL 模块内执行 null-safe 的 virtual destructor 路径，避免 Editor DLL consumer 跨模块
裸 `delete`。

### 5.2 Queue（FRALQueue）

- **类型（EQueueType）**：`Graphics`（渲染+计算+传输）、`Compute`、`Transfer`。
- **接口**：
  - `Submit(const FRALSubmitInfo&)`：提交命令列表，可附带 Wait/Signal 信号量与 Signal Fence，返回 `Success / InvalidArguments / Error`。
  - `WaitIdle()`：阻塞等待队列空闲。
- **Vulkan 实现**：`FVulkanRALQueue` 封装 `VkQueue`，`Submit` 将 `FRALCommandList` 映射为 `VkCommandBuffer` 并组装 `VkSubmitInfo`。Wait Stage 固定为 `VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT`；null/无效同步项不再被静默过滤，而是返回 `InvalidArguments`。

### 5.3 CommandAllocator 与 CommandList

`FRALCommandAllocator` 拥有后端命令内存；`Reset()` 只能在由它分配的全部 command list
完成 GPU 执行后调用。设备通过 `CreateCommandAllocator(EQueueType)` 创建 allocator，再以
`CreateCommandList(FRALCommandAllocator*)` 创建非 owning 关联的 command list。销毁顺序
必须是 command list 在前、allocator 在后，且两者均经 `RAL::DestroyResource()`。

命令列表采用显式 Begin/End 模式：

| 阶段/命令 | 说明 |
|----------|------|
| `Begin()` / `End()` | 开始/结束命令记录。 |
| `BeginRenderPass()` / `EndRenderPass()` | Dynamic Rendering 范围；Begin 校验当前 Pipeline 与颜色/深度模板附件的数量、格式和尺寸。 |
| `SetGraphicsPipeline()` | 绑定图形管线。 |
| `SetViewport()` / `SetScissorRect()` | 动态设置视口与裁剪矩形。 |
| `SetVertexBuffer()` / `SetIndexBuffer()` | 绑定顶点/索引缓冲；索引格式支持 `R32_UINT` 与 `R16_UINT`。 |
| `SetBindGroup(uint32 SetIndex, FRALBindGroup*)` | 按 Set 索引绑定描述符集。 |
| `SetPushConstants()` | 设置 Push Constant 数据。 |
| `Draw()` / `DrawIndexed()` | 绘制调用。 |

- **Vulkan 实现**：`FVulkanRALCommandAllocator` 独占 `VkCommandPool`，`Reset()` 映射为 `vkResetCommandPool`；`FVulkanRALCommandList` 仅持有从该 pool 分配的 Primary `VkCommandBuffer` 与非 owning allocator 引用。`BeginRenderPass` 组装 `VkRenderingInfo`，支持 single-RT、最多 8 路 MRT、depth-only 以及 color + depth/stencil；`D24_UNORM_S8_UINT` 的 depth/stencil 共用同一附件描述。Dynamic Rendering 不负责资源状态转换，调用方必须在 Begin 前通过 RAL/RFG barrier 将颜色附件转换到 `RenderTarget`、将当前可写深度模板附件转换到 `DepthStencilWrite`。`EndRenderPass` 会自动为 Swapchain 图像插入 `COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR` 的 Pipeline Barrier。

---

## 6. 同步原语

| 类型 | 接口 | 说明 |
|-----|------|------|
| **FRALSemaphore** | 纯标记接口 | GPU-GPU 同步信号量。 |
| **FRALFence** | `Reset()` / `Wait(uint64 Timeout)` / `IsSignaled()` | GPU-CPU 同步围栏。 |

- **Vulkan 实现**：`FVulkanRALSemaphore` 封装 `VkSemaphore`；`FVulkanRALFence` 封装 `VkFence`，`Wait` 默认超时为 `UINT64_MAX`。

---

## 7. Vulkan 后端现状

### 7.1 架构

所有 Vulkan 对象均位于 `RAL/Private/Vulkan/` 目录下，Public 头文件仅暴露 `Vulkan/VulkanRAL.h`（供需要直接访问 Vulkan 句柄的高级用户或调试使用）。核心类前缀为 `FVulkanRAL*`。

### 7.2 设备初始化流程

`FVulkanRALDevice` 构造函数按以下顺序初始化：

1. `InternalCreateInstance()`：创建 `VkInstance`，API 版本 1.2，启用平台 Surface 扩展与可选的 `VK_EXT_debug_utils`。
2. `InternalSetupValidationMessenger()`：若启用 Validation，安装 `VkDebugUtilsMessengerEXT`，**当前仅输出 Error 级别消息**。
3. `InternalSelectPhysicalDevice()`：枚举物理设备，优先选择独立 GPU（`VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU`），并查找首个支持 Graphics 的 Queue Family。
4. `InternalCreateLogicalDevice()`：创建逻辑设备，检测并启用 Descriptor Indexing（Bindless）特性；macOS 额外处理 `VK_KHR_portability_subset`。
5. `InternalSetupBindlessHeap()`：若支持 Bindless，创建大型 `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` 描述符集。
6. 创建 `FVulkanRALQueue`（Graphics Family，Queue Index 0）。

### 7.3 平台扩展

| 平台 | Instance 扩展 | Surface 类型 |
|-----|--------------|-------------|
| Windows | `VK_KHR_surface`、`VK_KHR_win32_surface`、`VK_EXT_debug_utils`（Validation） | `EPlatformSurfaceType::Win32` |
| macOS | `VK_KHR_surface`、`VK_EXT_metal_surface`、`VK_KHR_portability_enumeration`、`VK_EXT_debug_utils`（Validation） | `EPlatformSurfaceType::MetalLayer` |

---

## 8. 依赖关系

根据 `Build.ts`：

| 模块 | 依赖方式 | 说明 |
|-----|---------|------|
| **Core** | Public | 基础类型（`CoreMinimal.h`）、日志分类、`LE::String`、`LE::FNonCopyable` 等。 |
| **Spdlog** | Public | 日志输出（`LE_LOG` 宏内部使用）。 |
| **Vulkan** | Public | Vulkan 头文件与加载。 |

---

## 9. 已知限制

1. **仅 Vulkan 后端**：`RAL::CreateDevice()` 硬编码创建 `FVulkanRALDevice`，无 D3D12/OpenGL 分支。
2. **无多线程命令录制**：`FRALCommandList` 未暴露线程安全或 Secondary Command Buffer 支持，当前所有录制均发生在主线程。
3. **Validation 硬编码开关**：`LE_RAL_ENABLE_VALIDATION` 宏在 `RALTypes.h` 中硬编码为 `1`，无法通过运行时配置关闭。
4. **单一 Graphics Queue**：当前仅创建并使用一个 Graphics Queue，无独立的 Compute/Transfer Queue 实现（`EQueueType` 参数被忽略）。
5. **Dynamic Rendering 范围有限**：已覆盖 0–8 个颜色附件与 `D32_FLOAT` / `D24_UNORM_S8_UINT`，但尚未支持 Resolve、MSAA、分层渲染、独立 stencil 状态或只读 depth/stencil 的完整管线策略。
6. **BlendState 未完整实现**：`FRALBlendStateDesc` 仅包含 `bEnable` 布尔值，无独立颜色/Alpha 混合因子与操作配置。
7. **Bindless 分配策略占位**：`FVulkanRALDevice::AllocateBindlessIndex()` 使用简单的静态递增计数器，无空闲索引回收，存在溢出风险。
8. **资源状态由调用方负责**：Dynamic Rendering 不隐式转换 image layout；缺少 RAL barrier 或 RFG `DepthStencilWrite` 声明会触发 Vulkan Validation 错误。
9. **RFG transient 异步 ownership**：record-only/异步执行中的 RFG-owned transient 必须转交给 borrowed `IRFGDeferredReleaseSink`；Renderer 将其适配到当前 frame slot，并在对应 fence 完成后回收。缺少或拒绝 sink 时不会提交，未转移资源由 RFG 精确清理；imported resources 不参与 ownership 转移。
10. **Buffer 共享模式固定**：`FVulkanRALBuffer` 创建时 `sharingMode` 固定为 `VK_SHARING_MODE_EXCLUSIVE`，未处理跨 Queue Family 共享场景。

## 10. P0 冻结状态

RFG/RAL P0 已于 2026-08-06 完成 single-RT、dual-MRT、depth/stencil 与 offscreen-composite 验证。验收矩阵、复现方式和 Vulkan Validation 结果见 [RenderFoundationP0.md](RenderFoundationP0.md)。冻结后不在 P0 上继续添加渲染能力；后续帧同步、资源回收和真实 BasePass 分别进入 Roadmap 的后续阶段。
