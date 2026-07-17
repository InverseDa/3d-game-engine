# World 模块设计文档

## 1. 模块职责

World 模块是引擎的**游戏世界表示层**，负责维护运行时场景数据，并为渲染管线提供可消费的渲染代理（Render Proxy）。当前核心职责包括：

- **场景数据容器**：以 `FWorld` 为中心，持有场景中的几何体数据（`FWorldMeshComponent`）。
- **渲染数据提取**：通过 `FWorldRenderSceneExtractor` 将世界数据转换为渲染器可理解的 `FRenderScene`。
- **渲染桥梁**：作为游戏逻辑（未来）与 Renderer 模块之间的中间层，解耦世界表示与渲染实现。

---

## 2. 当前架构

### 2.1 类图与数据流

```
+----------------------------+         ExtractRenderScene()         +-------------------------+
|         FWorld             | -----------------------------------> |      FRenderScene       |
|----------------------------|                                      |-------------------------|
| - MeshComponents[]         |                                      | - Meshes[]              |
|   (FWorldMeshComponent)    |                                      |   (FRenderMeshProxy)    |
|                            |                                      | - Lights[]              |
| - Reset()                  |                                      | - Cameras[]             |
+----------------------------+                                      +-------------------------+
```

### 2.2 核心类型

| 类型 | 文件 | 说明 |
|------|------|------|
| `FWorldPrimitive` | `World.h` | 世界基元基类，仅含 `DebugName`。 |
| `FWorldMeshComponent` | `World.h` | 继承自 `FWorldPrimitive`，持有 `GraphicsPipeline`、`VertexBuffer`、`VertexCount`、`PassMask`、`SortKey`。 |
| `FWorld` | `World.h` | 世界根对象，内部为一个 `std::vector<FWorldMeshComponent>` 容器，提供 `Reset()` 方法。 |
| `FWorldRenderSceneExtractor` | `WorldRenderSceneExtractor.h/.cpp` | 静态工具类，单方法 `ExtractRenderScene(const FWorld&, FRenderScene&)`，负责逐元素浅拷贝至渲染场景。 |
| `WorldMinimal.h` | `WorldMinimal.h` | 聚合头文件，仅包含 `World.h` 与 `WorldRenderSceneExtractor.h`。 |

### 2.3 关键代码路径

**世界定义**（`World.h`）：

```cpp
class FWorld
{
public:
    void Reset() { MeshComponents.clear(); }
    std::vector<FWorldMeshComponent> MeshComponents;
};
```

**提取器实现**（`WorldRenderSceneExtractor.cpp:6-21`）：

```cpp
void FWorldRenderSceneExtractor::ExtractRenderScene(const FWorld& World, FRenderScene& OutRenderScene)
{
    OutRenderScene.Reset();
    OutRenderScene.Meshes.reserve(World.MeshComponents.size());

    for (const FWorldMeshComponent& MeshComponent : World.MeshComponents)
    {
        FRenderMeshProxy& RenderMesh = OutRenderScene.Meshes.emplace_back();
        RenderMesh.DebugName       = MeshComponent.DebugName;
        RenderMesh.GraphicsPipeline = MeshComponent.GraphicsPipeline;
        RenderMesh.VertexBuffer    = MeshComponent.VertexBuffer;
        RenderMesh.VertexCount     = MeshComponent.VertexCount;
        RenderMesh.PassMask        = MeshComponent.PassMask;
        RenderMesh.SortKey         = MeshComponent.SortKey;
    }
}
```

---

## 3. 与 Renderer 的交互流程

World 模块本身不直接执行绘制命令，而是通过**提取器模式**将数据推送至 Renderer：

```
Game Loop / Editor
       |
       v
   [FWorld]  <-- 逻辑层维护场景数据
       |
       | FWorldRenderSceneExtractor::ExtractRenderScene(World, RenderScene)
       v
   [FRenderScene]  <-- Renderer 模块的纯数据视图
       |
       v
   [Renderer]  <-- 执行裁剪、排序、提交 RAL 绘制
```

### 交互要点

1. **单向数据流**：World → Extractor → RenderScene，无回写。渲染器对 `FRenderScene` 的修改不影响 `FWorld`。
2. **逐帧重建**：`ExtractRenderScene` 首先调用 `OutRenderScene.Reset()`，每帧完全重建渲染代理列表。当前无增量更新或持久化映射。
3. **字段一一对应**：`FWorldMeshComponent` 与 `FRenderMeshProxy` 字段完全一致，提取器本质为浅拷贝循环。

---

## 4. 依赖关系

依据 `World.Build.cs`：

```csharp
conf.AddPublicDependency<CoreProject>(target);      // FString、基础类型
conf.AddPublicDependency<RALProject>(target);       // FRALBuffer、FRALPipeline_Graphics
conf.AddPublicDependency<RendererProject>(target);  // FRenderScene、ERenderMeshPassMask
```

| 依赖模块 | 用途 |
|----------|------|
| **Core** | 使用 `FString`（`DebugName`）、`uint32`/`uint64` 等基础类型。 |
| **RAL** | 直接持有 RAL 渲染资源指针（`FRALBuffer*`、`FRALPipeline_Graphics*`），说明 World 层已穿透至底层图形抽象。 |
| **Renderer** | 依赖 `FRenderScene`、`FRenderMeshProxy`、`ERenderMeshPassMask` 完成提取。 |

> **注意**：World 同时依赖 RAL 与 Renderer，导致其同时接触底层图形 API 抽象和上层渲染场景表示，耦合度较高。

---

## 5. 严重不足分析

当前 World 模块处于**极度原始的原型阶段**，距离生产级场景管理差距显著：

### 5.1 无 ECS / 无对象模型

- `FWorld` 仅为一个 `std::vector<FWorldMeshComponent>`，没有 **Entity**、**Actor** 或 **Object** 概念。
- 无法表达“一个物体由多个 Component 组成”的常识性结构（如 Mesh + Light + Collider）。
- 没有唯一标识符（EntityID / ObjectID），无法对场景元素进行寻址、销毁或生命周期管理。

### 5.2 无 Transform 层级

- `FWorldMeshComponent` 不包含任何空间信息：`Position`、`Rotation`、`Scale`、`LocalToWorld` 矩阵均缺失。
- 无父子层级（Scene Graph），无法实现骨骼、嵌套节点、坐标空间变换。
- 提取器直接将原始数据拷贝至 `FRenderScene`，渲染器接收到的 Mesh 缺乏世界空间变换。

### 5.3 无场景图与空间查询

- 所有 Mesh 平铺于一个线性 `vector` 中，无加速结构（BVH、八叉树、网格）。
- 无法支持视锥裁剪、遮挡剔除、光线查询等基础功能。

### 5.4 资源管理缺失

- `FWorldMeshComponent` 直接持有原始指针（`FRALBuffer*`、`FRALPipeline_Graphics*`），无资源句柄、引用计数或弱引用机制。
- 无材质、纹理、网格资源的间接引用系统，资源生命周期与世界对象生命周期纠缠。

### 5.5 序列化与持久化空白

- 无任何序列化接口（Save/Load、JSON/Binary）。
- 场景无法保存至磁盘，也无法从文件加载。

### 5.6 模块规模极小

- 整个模块仅 **5 个文件**，有效代码不足 100 行。
- `FWorldPrimitive` 与 `FRenderPrimitive`、`FWorldMeshComponent` 与 `FRenderMeshProxy` 几乎完全镜像，存在重复定义。
- `WorldMinimal.h` 仅为头文件聚合，无实际功能价值。

### 5.7 架构耦合

- World 层直接暴露 RAL 类型指针，破坏了“高层模块不应依赖低层细节”的分层原则。理想情况下，World 应通过资源句柄（如 `FMeshHandle`、`FMaterialHandle`）间接引用渲染资源，由 Renderer 或 RAL 模块在提取阶段解析为实际 GPU 对象。

---

## 6. 未来演进方向

### 6.1 短期：基础对象与 Transform

1. **引入 `FEntity` / `FActor`**
   - 为场景元素分配唯一 ID（`FEntityID : uint32`）。
   - 支持 `AddComponent<T>()`、`RemoveComponent()`、`GetComponent<T>()`。

2. **添加 `FTransformComponent`**
   - 包含 `Translation`、`Rotation`、`Scale`。
   - 支持局部/世界空间矩阵计算及脏标记（Dirty Flag）缓存。

3. **父子层级（Scene Graph）**
   - `FHierarchyComponent`：存储 `Parent`、`Children` 索引。
   - 支持递归计算 `LocalToWorld`。

### 6.2 中期：ECS 框架（推荐路径）

建议迁移至 **Entity-Component-System** 架构，以提升缓存友好性和扩展性：

```
+------------+     +-------------------+     +------------------+
| FEntity    | --> | FComponentManager | --> | TArray<T> Pools  |
| (ID only)  |     | (Archetype/Chunk) |     | (SoA 紧凑存储)    |
+------------+     +-------------------+     +------------------+
                          ^
                          |
                   +----------------+
                   | ISystem        |
                   | - Update()     |
                   | - Extract()    |
                   +----------------+
```

- **Component**：纯数据（`FTransformComponent`、`FMeshComponent`、`FLightComponent`）。
- **System**：行为逻辑（`FTransformSystem`、`FRenderExtractSystem`）。
- **Archetype/Chunk 存储**：同类 Entity 的 Component 紧凑存储，提升遍历效率。

收益：
- 渲染提取变为对 `FMeshComponent` 连续数组的线性遍历，CPU 缓存命中率高。
- 易于扩展新 Component（Physics、Audio、AI）而不修改 `FWorld` 核心。

### 6.3 中期：资源引用与资产管理

- 用 `FMeshHandle`、`FMaterialHandle` 替代原始 `FRALBuffer*` / `FRALPipeline_Graphics*` 指针。
- 引入 `FAssetManager` 或 `FResourceRegistry`，World 仅存储逻辑句柄，Renderer 在提取阶段通过句柄查询 GPU 资源。
- 支持异步加载、引用计数、热重载。

### 6.4 长期：场景序列化与编辑器集成

- **场景序列化**：支持 YAML/JSON/Binary 格式保存/加载 `FWorld`。
- **世界分区（World Partition）**：大型场景流式加载、Sub-Level 管理。
- **编辑器集成**：Undo/Redo、Scene Diff、属性面板反射（需结合 Core 的反射系统）。

### 6.5 长期：空间加速结构

- 为静态几何体构建 BVH 或八叉树，支持视锥裁剪与遮挡查询。
- 动态物体采用宽松 AABB 树或均匀网格。

---

## 7. 结论

World 模块当前仅实现了**最基础的场景数据容器与渲染提取管道**，验证了“World → Extractor → RenderScene”的分层思路，但缺乏任何生产级游戏引擎必备的核心设施：ECS/Actor 模型、Transform 层级、资源引用、序列化。其当前状态更适合视为**架构占位符（Placeholder）**。

**下一步建议**：
1. 优先引入 `FEntity` + `FTransformComponent` + `FMeshComponent` 的最小对象模型，解决“场景中有什么、在哪里”的基础问题。
2. 随后将 World 中的 RAL 原始指针替换为资源句柄，解除对底层图形 API 的直接依赖。
3. 在此基础上迭代至 ECS 架构，为后续 Physics、Audio、Gameplay 系统的接入奠定可扩展的数据基础。
