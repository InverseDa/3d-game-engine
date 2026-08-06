# RFG（Render Frame Graph）设计文档

## 1. 模块职责

RFG（Render Frame Graph）是引擎渲染系统的核心调度层，负责将高层渲染逻辑与底层 GPU 资源管理解耦。其职责包括：

- **自动化资源生命周期管理**：Transient 资源（每帧创建的纹理/Buffer）由 Frame Graph 自动分配与回收，支持基于生命周期的内存复用（Resource Aliasing）。
- **自动化屏障推导**：根据 Pass 对资源的读写访问模式，自动计算 `PipelineBarrier` 与 `Queue Family Ownership Transfer`，消除手工同步错误。
- **自动化 Pass 调度与裁剪**：通过依赖分析确定 Pass 执行顺序，剔除对最终输出无贡献的 Pass；预留多队列（Graphics/Compute/Transfer）并发执行能力。
- **编译计划缓存**：稳定图结构可复用已编译的调度计划，降低每帧 CPU 开销。
- **数据驱动模板实例化**：通过 `FRFGTemplate` 支持离线 authoring 的渲染图模板在运行时实例化。

## 2. 架构总览

RFG 采用 **四阶段管线** 架构：

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Authoring  │ --> │   Record    │ --> │   Compile   │ --> │   Execute   │
│  (模板定义)  │     │  (声明建图)  │     │  (编译优化)  │     │  (提交GPU)   │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
```

1. **Authoring**：通过 `FRFGTemplate` 定义资源、Pass 节点与边的数据驱动描述；经 `FRFGTemplateInstancer` 验证并生成图签名。
2. **Record**：运行时通过 `FRFGBuilder` 以声明式 API 创建资源、注册 Pass、声明读写依赖，产出 `FRFGRecordedGraph`。
3. **Compile**：`FRFGCompiler` 对记录图进行依赖分析、Pass 裁剪、屏障规划与拓扑排序，产出 `FRFGCompiledPlan`。`FRFGPlanCache` 支持按签名命中缓存。
4. **Execute**：`FRFGExecutor` 根据编译计划创建/绑定实际 RAL 资源，遍历 Pass 并执行回调或 `IRFGPassExecutor`，最终提交 CommandList 到队列。

## 3. 子系统详细说明

### 3.1 Core（基础设施）

#### 3.1.1 句柄系统（`RFGHandles.h`）

- `FRFGPassHandle` / `FRFGResourceHandle`：轻量级 32-bit ID 句柄，`InvalidId = 0xFFFFFFFFu`。
- 提供 `std::hash` 特化，可直接作为 `unordered_map/set` 键值。
- 句柄与数组索引一一对应（`RecordedGraph` 内部按 ID 线性存储），保证 O(1) 随机访问。

#### 3.1.2 类型系统（`RFGTypes.h`）

核心枚举与描述符：

| 类型 | 说明 |
|------|------|
| `ERFGQueueType` | Graphics / Compute / Transfer |
| `ERFGResourceType` | Texture / Buffer |
| `ERFGAccessType` | None / Read / Write / ReadWrite |
| `ERFGHazardType` | RAW / WAR / WAW |
| `ERFGPipelineStage` | None / Graphics / Compute / Copy / Host / All |
| `ERFGPassFlags` | `HasSideEffects` / `NeverCull` / `AsyncComputeCandidate` |
| `ERFGResourceFlags` | `Imported` / `External` / `Persistent` |

资源描述符：
- `FRFGTextureDesc`：Width/Height/Depth/MipLevels/ArrayLayers/Format/UsageMask
- `FRFGBufferDesc`：Size/Stride/Usage/UsageMask
- `FRFGAccessDesc`：Access/ShaderStage/PipelineStage，支持子资源范围（Mip/Array Layer）

编译与执行选项：
- `FRFGCompileOptions`：控制裁剪、屏障省略、状态合并、计划缓存、确定性排序开关
- `FRFGGraphSignature`：64-bit Hash，用于缓存键值
- `FRFGSourceLocation`：文件+行号，用于调试溯源

#### 3.1.3 黑板（`RFGBlackboard.h` / `.cpp`）

- 键值对存储容器，支持三类数据：`FRFGPassHandle`、`FRFGResourceHandle`、`uint64`。
- 使用 `std::unordered_map<std::string, ...>` 实现，键由 `FString` 转为 `std::string`。
- 用途：在 `FRFGBuilder` 录制阶段按名称缓存句柄，便于跨 Pass 引用资源。

#### 3.1.4 运行时（`RFGRuntime.h` / `.cpp`）

- 模块级单例概念，持有 `FRFGCompiler*` 与 `FRFGPlanCache*`。
- `Initialize(IRFGPassRegistry*)`：绑定 Pass 注册表。
- `Compile(...)`：优先查询 `PlanCache`，命中则直接返回缓存计划；否则走完整编译管线并回写缓存。
- `SetCompileOptions`：动态开关各项编译优化。

#### 3.1.5 实例（`RFGInstance.h` / `.cpp`）

- 用户级门面（Facade），组合 `FRFGRuntime`（编译侧）与 `FRFGExecutor`（执行侧）。
- `CreateBuilder()`：创建已绑定当前 `PassRegistry` 的 `FRFGBuilder`。
- `Compile(...)` / `Execute(...)`：代理到 Runtime 与 Executor。
- 一个 `FRFGInstance` 对应一个渲染上下文的生命周期。

### 3.2 Record（声明式图构建）

#### 3.2.1 Pass 注册表（`RFGPassRegistry.h` / `.cpp`）

- `IRFGPassRegistry`：纯虚接口，支持注册/注销/查询 Pass 类型。
- `FRFGPassRegistry`：基于 `unordered_map<std::string, FRFGRegisteredPassType>` 的实现。
- `FRFGPassSchema`：描述 Pass 的元数据（类型名、默认队列、默认标志、参数列表）。
- `IRFGPassExecutor`： Pass 的执行接口，`Execute(FRFGPassContext&, const FRFGPassParameterBlock&)`。
- `FRFGRegisteredPassType`：将 Schema 与 Executor 绑定。

#### 3.2.2 记录图（`RFGRecordedGraph.h` / `.cpp`）

- `FRFGPassNode`：
  - 基础信息：`Handle`、`Name`、`PassTypeName`、`Queue`、`Flags`、`SourceLocation`
  - 执行负载：`Parameters`、`ExecuteCallback`（`std::function`）、`Executor`
  - 依赖声明：`ResourceAccesses`（资源+访问描述数组）、`ExplicitDependencies`（显式 Pass 依赖句柄数组）
- `FRFGResourceNode`：
  - `Handle`、`Name`、`Desc`、`Flags`
  - 外部资源指针：`ImportedTexture` / `ImportedBuffer`（来自 RAL）
- 存储结构：
  - `std::vector<FRFGPassNode> PassNodes`：线性数组，Handle.Id 即数组下标。
  - `std::vector<FRFGResourceNode> ResourceNodes`：同上。
  - `std::vector<FRFGPassHandle> PassOrder`：记录 Pass 添加顺序。
  - `std::unordered_set<FRFGResourceHandle> OutputResources`：标记最终输出资源，用于裁剪阶段。

#### 3.2.3 构建器（`RFGBuilder.h` / `.cpp`）

声明式 API，是用户（Renderer）与 RFG 交互的主要入口：

- **资源创建**：
  - `CreateTexture(Name, FRFGTextureDesc)` / `CreateBuffer(Name, FRFGBufferDesc)`：生成 Transient 资源节点。
  - `ImportTexture(Name, FRALTexture*)` / `ImportBuffer(Name, FRALBuffer*)`：导入外部已有资源，自动读取 RAL 描述填充 `FRFGResourceDesc`。
- **Pass 注册**：
  - `AddPass(Name, PassTypeName, ParameterBlock, Flags, QueueType, SourceLocation)`：添加 Pass 节点；若 `QueueType` 为默认 Graphics 且注册表中有该类型，则自动继承 `PreferredQueue` 与 `DefaultFlags`。
  - `SetPassCallback(PassHandle, Callback)`：绑定 Lambda 执行体。
- **依赖声明**：
  - `Read(PassHandle, ResourceHandle, AccessDesc)` / `Write(...)`：向 PassNode 追加资源访问记录，并强制 Access 为 Read/Write。
  - `AddDependency(BeforePass, AfterPass)`：显式添加 Pass 间执行顺序依赖。
- **输出标记**：`MarkOutput(ResourceHandle)`，声明该资源为图最终输出。
- **签名生成**：`BuildSignature()`，对图中所有资源节点、Pass 节点、访问描述、显式依赖进行 FNV-1a 哈希，得到 64-bit `FRFGGraphSignature`，用于缓存键值。
- **黑板访问**：`GetBlackboard()`，用于在构建过程中按名称共享句柄。

> **宏辅助**：`RFGMacros.h` 提供 `ENQUEUE_RENDER_PASS` 宏，一键封装 `AddPass` + `SetPassCallback`，并自动注入 `__FILE__` / `__LINE__` 源码位置。

### 3.3 Compile（编译优化）

#### 3.3.1 编译器（`RFGCompiler.h` / `.cpp`）

编译流程（`FRFGCompiler::Compile`）：

1. `DependencyAnalyzer.BuildDependencies(...)`：构建 Pass 依赖图并拓扑排序。
2. `Culler.CullPasses(...)`：若 `bEnablePassCulling` 开启，裁剪无用 Pass。
3. `BarrierPlanner.BuildResourceLifetimes(...)`：计算每个资源在编译后 Pass 序列中的首次/末次使用索引，标记 `bTransient`。
4. `BarrierPlanner.BuildBarriers(...)`：为每个 Pass 生成 `PreBarriers`。
5. 日志输出：打印 Pass 顺序（含 DependencyLevel 与 Queue）及图导出（Dot/Json）。

#### 3.3.2 依赖分析器（`RFGDependencyAnalyzer.h` / `.cpp`）

- **资源访问驱动的隐式依赖**：按 `PassOrder` 顺序遍历 Pass，维护每个资源的 `LastReaders` / `LastWriters`。
  - 当前 Pass **Write**：与所有前序 `LastWriters` 生成 WAW，与所有前序 `LastReaders` 生成 WAR；清空读写列表，将自身记入 `LastWriters`。
  - 当前 Pass **Read**：与所有前序 `LastWriters` 生成 RAW；将自身记入 `LastReaders`。
- **显式依赖**：将 `ExplicitDependencies` 直接加入为 RAW 边（去重）。
- **拓扑排序**：基于 Kahn 算法（入度减零法）进行稳定拓扑排序，排序键为 `(DependencyLevel, InsertionOrder, Name)`，确保 `bDeterministicSort` 开启时结果可复现。
- **DependencyLevel 计算**：每个 Pass 的 Level 为所有入边源 Pass Level + 1 的最大值。同 Level Pass 之间无依赖，未来可作为 AsyncCompute / 多队列并行批次的依据。

#### 3.3.3 裁剪器（`RFGCuller.h` / `.cpp`）

- **种子 Pass**：满足以下任一条件即保留：
  - `ERFGPassFlags::HasSideEffects`（如写入 SwapChain、发起 Present）
  - `ERFGPassFlags::NeverCull`
  - 写入被 `MarkOutput` 标记的资源
- **反向传播**：从种子 Pass 出发，沿 `IncomingEdges` 反向遍历，标记所有被依赖的 Pass 为必需。
- **图压缩**：剔除非必需 Pass 后，重新计算剩余 Pass 的 `DependencyLevel` 并清理失效边。

#### 3.3.4 屏障规划器（`RFGBarrierPlanner.h` / `.cpp`）

- **资源生命周期（`BuildResourceLifetimes`）**：
  - 遍历编译后 Pass 序列，统计每个资源首次与末次出现的 Pass 索引。
  - `bTransient = true` 当且仅当资源无 `Imported | External | Persistent` 标志。
  - 该生命周期信息是后续 **Resource Aliasing**（内存复用）的基础。
- **屏障生成（`BuildBarriers`）**：
  - 维护每个资源的 `FLastResourceState`（Stage / Access / Queue / bInitialized）。
  - 仅在以下情况插入 `FRFGBarrierTransition`：
    - 队列不同（`SrcQueue != DstQueue`）→ 需要 Queue Family Ownership Transfer；或
    - Stage 或 Access 发生变化；且
    - 前序状态或当前状态涉及 Write（`AccessHasWrite`）
  - Barrier 挂在 Pass 的 `PreBarriers` 数组中，按 Pass 顺序提交即可保证状态连续性。
  - 支持跨队列转移：若 `SrcQueue != DstQueue`，同一 `FRFGBarrierTransition` 语义上对应 Release + Acquire 对。

#### 3.3.5 计划缓存（`RFGPlanCache.h` / `.cpp`）

- `std::unordered_map<uint64, std::shared_ptr<const FRFGCompiledPlan>> CachedPlans`
- `Find(Signature)`：缓存命中/未命中统计。
- `Store(Signature, Plan)`：回写编译结果。
- 与 `FRFGRuntime::Compile` 协同：签名非零且 `bEnablePlanCache` 开启时优先查缓存。

#### 3.3.6 编译产物（`RFGCompiledPlan.h` / `.cpp`）

- `FRFGCompiledPass`：
  - `Handle`、`Name`、`Queue`、`Flags`
  - `DependencyLevel`：拓扑层级
  - `IncomingEdges`：入边列表（`FRFGDependencyEdge`，含 Hazard 类型）
  - `PreBarriers` / `PostBarriers`：屏障数组（当前主要使用 `PreBarriers`）
- `FRFGCompiledResourceLife`：
  - `Resource`、`FirstPassIndex`、`LastPassIndex`、`bTransient`
- `FRFGCompiledPlan`：聚合上述数组，支持 `Clear()` 复用。

### 3.4 Execute（GPU 提交）

#### 3.4.1 执行器（`RFGExecutor.h` / `.cpp`）

执行流程（`FRFGExecutor::Execute`）：

1. `PrepareResources`：遍历 `FRFGRecordedGraph` 的所有资源节点：
   - Imported 资源直接绑定指针；
   - Transient 资源通过 `FRALDevice::CreateTexture/CreateBuffer` 创建，存入 `ExecutionContext.OwnedTextures/OwnedBuffers`。
2. `CommandList->Begin()`。
3. 遍历 `FRFGCompiledPlan::GetPasses()`：
   - 设置 `FRFGPassContext`（PassIndex、ExecutionContext、RecordedGraph、CompiledPlan）。
   - 优先调用 `PassNode.ExecuteCallback`（Lambda）；若未设置，则回退到 `PassNode.Executor->Execute(...)`（注册表模式）。
   - 注意：当前实现中 **Barrier 由执行器在 Pass 回调内部或 RAL 层实际插入**，`PreBarriers` 数据已在编译阶段完成推导，供执行侧参考或未来自动提交。
4. `CommandList->End()`。
5. 若 `bSubmitImmediately`，通过 `GraphicsQueue->Submit(...)` 提交。
6. 若 `bWaitForCompletion`，执行 `Queue->WaitIdle()`。
7. `ResetTransientResources`：销毁本帧创建的 Transient RAL 资源。

#### 3.4.2 Pass 上下文（`RFGPassContext.h` / `.cpp`）

- `GetDevice()` / `GetCommandList()`：访问当前 GPU 设备与 CommandList。
  - `GetCommandList` 会根据当前 Pass 的 `Queue` 类型从 `FRFGExecutionContext` 中分发（当前统一返回单 CommandList，预留多队列扩展）。
- `ResolveTexture(ResourceHandle)` / `ResolveBuffer(ResourceHandle)`：将 RFG 句柄解析为实际 RAL 资源指针。
- `GetPassIndex()`：获取当前在编译计划中的索引。

#### 3.4.3 执行上下文（`RFGPassContext.h`）

`FRFGExecutionContext`：
- 设备与队列指针：`Device`、`GraphicsQueue`、`ComputeQueue`、`TransferQueue`
- CommandList：当前单线程实现共用一份 `CommandList`；`GetCommandList(ERFGQueueType)` 预留多队列分发。
- 资源映射：`TextureResources` / `BufferResources`（`unordered_map<uint32, RAL*>`，Key 为 Handle.Id）
- 所有权数组：`OwnedTextures` / `OwnedBuffers`，用于帧末统一销毁。
- `ResetTransientResources()`：释放所有权数组中的资源并清空映射。

### 3.5 Authoring（数据驱动模板）

#### 3.5.1 模板（`RFGTemplate.h` / `.cpp`）

- `FRFGTemplate`：离线可序列化的图描述，包含：
  - `Resources`：`FRFGTemplateResource` 数组（ResourceId、Name、Desc、Flags）
  - `Nodes`：`FRFGTemplateNode` 数组（NodeId、Name、PassTypeName、Queue、Flags、DefaultParameters、ReadResources、WriteResources）
  - `Edges`：`FRFGTemplateEdge` 数组（SourceNodeId -> TargetNodeId）
- `Version`：模板版本号，用于资产兼容性检查。

#### 3.5.2 参数存储（`RFGParameterStore.h` / `.cpp`）

- `FRFGParameterValue`：原始字节数组 `Bytes` + `Revision`。
- `FRFGParameterStore`：键值对容器，支持运行时向模板节点注入参数。

#### 3.5.3 模板实例化器（`RFGTemplateInstancer.h` / `.cpp`）

- `Instantiate(Template, ParameterStore, Instance, Options)`：
  - 对 Template 的全部资源、节点、边进行 FNV-1a 哈希，生成 `FRFGGraphSignature`。
  - 基础验证：空模板时生成 Warning 并标记 `bSucceeded = false`。
  - 返回 `FRFGTemplateInstantiateResult`（含 `Validation`、`Signature`、`bSucceeded`）。
- 当前实现为轻量级哈希与验证，未来可扩展为将 Template 展开为 `FRFGRecordedGraph` 并通过 `FRFGBuilder` 重建。

#### 3.5.4 验证报告（`RFGValidationReport.h` / `.cpp`）

- `FRFGValidationIssue`：Severity（Info/Warning/Error）、Message、Context。
- `FRFGValidationReport`：Issue 列表聚合，支持 `HasErrors()` 快速判断。

#### 3.5.5 创作编译接口（`RFGAuthoringCompiler.h`）

- `IRFGAuthoringCompiler`：纯虚接口，接收 `FRFGAuthoringCompileRequest`（AssetPath、Version、实验性节点开关），产出 `FRFGAuthoringCompileResult`（Template + Validation）。
- 预期由资产管线（如编辑器插件或离线工具）实现，将 JSON/Binary 资产编译为 `FRFGTemplate`。

### 3.6 Debug（调试工具）

#### 3.6.1 图导出器（`RFGGraphExporter.h`）

- `FRFGGraphExporter`：支持将 `FRFGRecordedGraph` 与 `FRFGCompiledPlan` 导出为：
  - `Dot`：Graphviz 有向图格式，可用于可视化 Pass 依赖与屏障。
  - `Json`：结构化数据，便于外部工具分析。
- 当前头文件已定义接口，实际导出字符串由编译器日志侧调用（`FRFGCompiler::Compile` 中通过 `FRFGGraphExporter::ExportToString` 打印 Trace 日志）。

## 4. 关键设计决策

### 4.1 资源 Aliasing（内存复用）

- `FRFGCompiledResourceLife` 精确记录了每个 Transient 资源在编译后 Pass 序列中的 `[FirstPassIndex, LastPassIndex]`。
- 生命周期不重叠的 Transient 资源可复用同一块 GPU 显存，降低显存峰值。
- 当前 RFG 本身不直接管理物理内存分配，而是将生命周期数据暴露给上层或 RAL 的内存分配器；Executor 在 `PrepareResources` 中按节点创建独立资源，未来可在该层接入 Aliasing Allocator。

### 4.2 自动屏障推导

- RFG 采用 **状态跟踪（State Tracking）** 模型而非全图双向依赖扫描。
- `FRFGBarrierPlanner::BuildBarriers` 按编译后 Pass 顺序线性遍历，维护每个资源的 `LastState`。
- 仅在 Stage / Access / Queue 变化且涉及 Write 时插入 Barrier，自然实现 Barrier Elision（省略不必要的屏障）。
- 跨队列 Barrier 复用同一 `FRFGBarrierTransition` 结构，通过 `SrcQueue != DstQueue` 区分普通 Pipeline Barrier 与 Queue Family Ownership Transfer。

### 4.3 Pass 裁剪

- 基于 **反向可达性分析**：从输出资源与不可裁剪 Pass（SideEffects / NeverCull）出发，沿依赖边反向传播。
- 被裁剪的 Pass 其资源访问不再参与屏障计算，从而进一步减少无效 Barrier。
- 裁剪后重新计算 `DependencyLevel`，保证层级信息始终与最终执行序列一致。

### 4.4 缓存复用

- `FRFGGraphSignature` 为 64-bit FNV-1a 哈希，覆盖资源描述、Pass 描述、访问模式与显式依赖。
- 只要图结构不变（如分辨率不变、后处理管线固定），即可直接复用 `FRFGCompiledPlan`，跳过 DependencyAnalyzer / Culler / BarrierPlanner 的全部计算。
- `FRFGCompileOptions.bEnablePlanCache` 可动态关闭缓存，便于调试或内存敏感场景。

### 4.5 命名约定

RFG 模块所有公共类型均以 `FRFG` 为前缀，核心示例如下：

- `FRFGPassNode`、`FRFGResourceNode` —— 记录图节点
- `FRFGCompiledPass`、`FRFGCompiledResourceLife` —— 编译产物
- `FRFGPassHandle`、`FRFGResourceHandle` —— 句柄
- `FRFGBuilder`、`FRFGCompiler`、`FRFGExecutor` —— 阶段主体

> **注意**：这些前缀命名是模块标识的一部分，请勿将其改回旧名（如 `FPassNode`、`FResourceNode` 等无前缀形式），以保持代码库的一致性与可搜索性。

## 5. 依赖关系

根据 `Build.ts`：

```csharp
conf.AddPublicDependency<CoreProject>(target);
conf.AddPublicDependency<RALProject>(target);
```

- **Core**：使用 `CoreMinimal.h`、FString、`FNonCopyable`、日志宏（`LE_LOG` / `LE_DECLARE_LOG_CATEGORY`）等基础设施。
- **RAL（Render Abstraction Layer）**：
  - `FRALDevice`、`FRALQueue`、`FRALCommandList`
  - `FRALTexture`、`FRALBuffer` 及其描述符
  - RFG 本身不直接调用底层 Graphics API（Vulkan/D3D12），而是通过 RAL 抽象完成资源创建与提交。

模块内部子系统依赖关系（高层依赖低层）：

```
Authoring --> Core
Record    --> Core + RAL
Compile   --> Core + Record
Execute   --> Core + Record + Compile + RAL
Debug     --> Record + Compile
```

## 6. 与业界方案对标

### 6.1 UE Render Dependency Graph（RDG）

| 维度 | UE RDG | RFG |
|------|--------|-----|
| 构建方式 | `FRDGBuilder::AddPass` Lambda 捕获 | `FRFGBuilder::AddPass` + `SetPassCallback` / `IRFGPassExecutor` |
| 资源管理 | `FRDGTexture` / `FRDGBuffer` 句柄 + 池化分配 | `FRFGResourceHandle` + `FRFGExecutionContext` 映射，预留 Aliasing |
| 屏障 | 自动推导，全 Pipeline Barrier | 自动推导，支持 Queue Family Transfer |
| 裁剪 | 基于输出反向裁剪 | 同左，支持 `HasSideEffects` / `NeverCull` |
| 缓存 | 无显式 Compile Plan Cache | 显式 `FRFGPlanCache` + `FRFGGraphSignature` |
| 多队列 | 支持 AsyncCompute、复制队列 | 预留 `ERFGQueueType` 与 `DependencyLevel`，当前共用单 CommandList |

RFG 与 RDG 核心理念一致：**声明即依赖，依赖即同步**。RFG 的 `FRFGPlanCache` 是其区别于 RDG 的显著特征，适合移动端或 CPU 开销敏感场景。

### 6.2 Frostbite FrameGraph

| 维度 | Frostbite FrameGraph | RFG |
|------|----------------------|-----|
| 阶段 | Setup → Compile → Execute | Authoring/Record → Compile → Execute |
| 资源声明 | `FrameGraph::Create` 在 Pass 内声明 | `FRFGBuilder::CreateTexture/Buffer` 在图级别声明 |
| 模板化 | 无内置数据驱动模板 | 内置 `FRFGTemplate` + `FRFGTemplateInstancer` |
| 执行回调 | `RenderPass::Execute` 虚函数 | `FRFGPassCallback` Lambda 或 `IRFGPassExecutor` |

Frostbite FrameGraph 更强调 Pass 自我描述资源；RFG 则将资源声明外提到 Builder，更适合数据驱动与模板复用。

## 7. 典型使用流程

```cpp
// 1. 初始化
FRFGRuntime Runtime;
FRFGPassRegistry Registry;
Runtime.Initialize(&Registry);

FRFGInstance Instance;
Instance.Initialize(&Runtime);

// 2. 注册 Pass 类型
FRFGRegisteredPassType PassType;
PassType.Schema.PassTypeName = "TAA";
PassType.Schema.PreferredQueue = ERFGQueueType::Graphics;
PassType.Executor = &MyTAAExecutor;
Registry.RegisterPassType(PassType);

// 3. Record
FRFGBuilder Builder = Instance.CreateBuilder();
FRFGResourceHandle SceneColor = Builder.CreateTexture("SceneColor", SceneColorDesc);
FRFGResourceHandle History = Builder.ImportTexture("History", ExternalHistoryTexture);

FRFGPassHandle TAA = Builder.AddPass("TAA", "TAA");
Builder.Read(TAA, History);
Builder.Write(TAA, SceneColor);
Builder.MarkOutput(SceneColor);
Builder.SetPassCallback(TAA, [](FRFGPassContext& Context) { /* ... */ });

// 4. Compile
FRFGCompileResult Result = Instance.Compile(Builder.GetRecordedGraph(), Builder.BuildSignature());

// 5. Execute
FRFGExecutionContext ExecCtx;
ExecCtx.Device = RALDevice;
ExecCtx.GraphicsQueue = GraphicsQueue;
ExecCtx.CommandList = CmdList;
Instance.Execute(Result, Builder.GetRecordedGraph(), ExecCtx);
```

## 8. 扩展方向

- **AsyncCompute**：当前 `DependencyLevel` 已划分无依赖 Pass 批次，未来可在 `FRFGExecutor` 中为 Compute Queue 分配独立 `CommandList`，按 Level 批量提交。
- **Resource Aliasing Allocator**：在 `PrepareResources` 中引入基于 `FRFGCompiledResourceLife` 的内存别名分配器，复用 `FRALDevice` 的 Heap/Pool 接口。
- **子资源屏障细化**：当前 `FRFGBarrierTransition` 已携带 `BaseMipLevel` / `MipCount` / `BaseArrayLayer` / `LayerCount`，但 `BuildBarriers` 尚未按子资源拆分，未来可扩展为细粒度 Mip/Slice 级屏障。
- **自动根签名/描述符推导**：结合 RAL 的 PipelineState，从 `FRFGPassNode::ResourceAccesses` 自动生成 DescriptorSet / RootSignature 绑定。

## 9. P0 冻结状态

RFG/RAL P0 已于 2026-08-06 关闭并冻结。当前执行路径已通过 single-RT、dual-MRT、depth/stencil 与 offscreen-composite 的 Vulkan Validation 验收；深度模板写入必须在 Pass 资源声明中使用 `ERALResourceState::DepthStencilWrite`，由 BarrierPlanner 在 Dynamic Rendering 开始前生成显式转换。验收记录与复现步骤见 [RenderFoundationP0.md](RenderFoundationP0.md)。
