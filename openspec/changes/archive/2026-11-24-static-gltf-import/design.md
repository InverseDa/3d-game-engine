# Design

## Context

`AssetImport` 当前按单个资源转换并写出 `.leasset`。`AssetFormat` 已能表达索引、法线、UV、RGBA8 及单张 Base Color 纹理引用。每个 Scene 网格组件只能引用一对网格和材质，因此一个 glTF 节点可能需要生成多个可绘制实体。当前 Game 启动时建立资产目录索引，F5 创建候选 World 和资产会话，继续使用启动时的目录索引。

## Goals / Non-Goals

一次导入产出可独立校验和启动的完整内容版本。源数据、显式身份映射和作者场景都可用文本审阅。首版沿用现有运行时格式、材质和渲染边界，GLB、动画、PBR 和运行中切换烘焙资产版本留给后续变更。

## Decisions

### 1. 首版输入和转换规则

| 范围 | 首版规则 |
| --- | --- |
| 容器与文件 | glTF 2.0 的 `.gltf`、相对路径的外置 `.bin` 和 Base Color `.png`。按清单选择一个 scene，仅转换其可达节点和资源。输入根外路径、URL、data URI、GLB、内嵌图片和重解析点报错。 |
| 几何 | 有索引的 `TRIANGLES`。`POSITION` 与 `NORMAL` 为有限 Float32 VEC3，`TEXCOORD_0` 为有限 Float32 VEC2，贴图网格必需。支持 u8/u16/u32 索引、常规 byte offset 和交错 stride。拒绝稀疏/量化 accessor、压缩、越界、计数不符、无有效法线或顶点形变。输出现有 mesh v3。 |
| 节点 | 保留父子关系和局部 TRS，采用 glTF 的缺省值。首版拒绝显式 `matrix`、动画、skin、morph 和扩展。按源模型的静态层级导入，预览相机和灯由引擎场景提供。 |
| 坐标 | 采用 `B = diag(-1, 1, -1)` 将模型约定由 glTF 的 `+Z` 朝前转换到引擎 `-Z` 朝前。位置、法线和平移乘 `B`，旋转用 `B R B⁻¹`，缩放和三角绕序保留。UV 和 PNG 行都以左上为原点，直接保留。 |
| 材质 | `OPAQUE` 或省略 `alphaMode`，使用线性 `baseColorFactor.rgb`，可选一张由 `TEXCOORD_0` 采样的 sRGB Base Color PNG。缺省材质用白色且设独立清单绑定。输出沿用引擎的 nearest、clamp、mip 0 和 Lambert 着色。丢弃的切线、其他顶点通道、法线/金属粗糙度/遮蔽/自发光通道和采样差异以结构化警告列出。`MASK`、`BLEND`、其他 Base Color UV 集和材质扩展报错。 |

PNG 解码为未预乘 RGBA8，像素转移函数由 Base Color 用途决定。沿用 AssetFormat 的每网格 100 万顶点、300 万索引、纹理最长边 4096，以及 Scene 的 65,536 实体上限。源文件读取和本次导入总内存也设置上限。访问器在读取前验证 offset、stride、count 的溢出和实际范围，PNG 在解码前检查尺寸并限制解压输出。依据 [Khronos glTF 2.0 规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html) 和 [引擎坐标约定](../../../Docs/CoreMath.md#coordinates-and-angles)。

### 2. 模块边界与命令入口

| 模块 | 职责 |
| --- | --- |
| `Programs/AssetImport` | 增加静态 glTF 离线命令。私有适配层使用固定版本的 `cgltf` 解析源文件，使用 `LodePNG` 的 C 接口解码所需 PNG。适配层通过 `FileSystem` 读取受限内存数据，向引擎侧交付现有 DTO。`inspect --json` 返回源文档摘要、可达选择器和名称，`check --json` 以同一转换逻辑在内存验证清单并尝试编码，cook 写出候选文件。 |
| `AssetFormat` 与 `Scene` | 复用已有资产编码器和 `FSceneDocument` 编码。每个网格记录继续使用一组 Mesh/Material 引用和现有 Scene 版本。工具只用 DTO，构建阶段无需创建 World 或 RHI 设备。 |
| `Builder/Source/Content` | 提供导入/重导入的编排入口。建立候选目录、运行工具、安排整批验证和最终发布。复用现有 Win64 Program 构建入口，为 Game/Editor 暂存样例提供同一条路径。 |
| `SceneTool` | 通过现有 `check --json` 检查候选 Scene 与候选资产根。编排器通过工具进程消费退出状态和报告，避免在导入器再实现一套场景依赖校验。 |
| `FileSystem` | 提供受限读和根内路径解析，以及单文件原子替换。glTF 规则、导入身份和发布策略在工具域。 |
| Game、Editor、Renderer、RHI | 继续消费已发布的通用内容格式。预览启动脚本解出具体内容版本路径后使用现有环境变量，运行时无需解析 glTF、清单或发布指针。 |

glTF 和 PNG 的第三方类型及分配约束留在 AssetImport 私有适配层。JSON 报告包含 `ok`、稳定的 `code`、可用的 `file`、`selector` 或 `key`，以及警告。旧 `mesh`、`texture`、`material` 命令沿用原工作流。[cgltf](https://github.com/jkuhlmann/cgltf) · [LodePNG](https://lodev.org/lodepng/)

### 3. 稳定身份映射

版本化 `Import.json` 是身份来源，提交到 Git。它记录导入集 ID、挂载名、选定的 scene 索引、`sourceDocumentSha256`、来源选择器和持久身份。摘要计算使用送入解析器的同一份 `.gltf` 原始字节。它用于确认选择器针对当前源文档，和 UUID 生成无关。

| 清单记录 | 来源选择器 | 项目侧身份 |
| --- | --- | --- |
| 网格 | `(meshIndex, primitiveIndex)` | 稳定逻辑键、Asset UUID、类型、规范化虚拟路径 |
| 材质 | `materialIndex` 或专用的缺省材质标记 | 稳定逻辑键、Asset UUID、类型、规范化虚拟路径 |
| Base Color 纹理 | `imageIndex`，从材质的 texture 引用解析 | 稳定逻辑键、Asset UUID、类型、规范化虚拟路径 |
| 变换节点 | `nodeIndex` | 稳定逻辑键、实体 UUID |
| 可绘制实体 | `(节点逻辑键, 网格 primitive 逻辑键)` | 稳定逻辑键、实体 UUID，作为变换节点下一个单位局部变换的子实体 |

例如 `lantern-chain` 可以从节点索引 `1` 重绑到 `3`，其实体 UUID 继续使用清单原值。同一 primitive 可被多个节点引用，共用网格 UUID。每个绘制实例有自己的实体 UUID 和材质绑定。即使节点只有一个 primitive，也统一用独立的绘制子实体，追加第二个 primitive 时，原来可编辑的节点和实例不换身份。

`inspect` 提供当前摘要和定位线索。重新导出的 `.gltf` 即便只改了空白也需要显式确认摘要和选择器，便于审阅索引重排，显示名称和数组位置不参与持久身份分配。只修改仍被引用的 BIN/PNG 时沿用已确认的映射。每次重导入与当前发布的身份快照比对；允许添加带完整绑定的新项目，拒绝已发布逻辑键消失或改 UUID、类型、虚拟路径，拒绝同类型重复选择器、UUID 或路径冲突。实体和资产 UUID 各使用现有身份域。删除、重命名逻辑键和场景迁移另做显式流程。

### 4. 整批发布与恢复

每个导入集的输出根有 `current.json` 和 `generations/<generation>/`。每代包含 `Assets/`、生成的 `Scene.json` 和已验证清单快照。作者 Scene 放在版本目录之外，可以由第一次生成的场景另存得到。预览脚本必须只挂载某一代的 `Assets/`。

1. 编排器读取当前指针及身份快照，记录当前发布代的 UUID。首次导入期望指针不存在。`AssetImport` 在新目录转换全部数据，校验并写出资产和生成场景。预览可传入一份由引擎作者维护的 Scene 模板，合入相机、灯和地面，模板的资产亦纳入当前候选。
2. 运行 `SceneTool check --json` 预检生成场景。若调用者提供当前作者 Scene 路径，再用同一候选资产根校验这份场景的引用，但不复制或回写它。预检成功的候选目录关闭写入，后续读者使用只读约定。
3. 发布入口在 Win64 使用进程退出即释放的独占文件句柄串行提交。持锁重读指针并核对原发布代的 UUID，过期请求报错重试。通过后用 `WriteContentFileAtomically` 替换 `current.json`，指向具有新 UUID 的已完成候选。这是唯一的发布点，由 Builder 调用 AssetImport 内部入口执行。
4. 转换失败、预检失败或指针替换失败时，当前入口及上一版本均保留。只回收本次确定未发布的候选目录；异常退出后可留下未被指针引用的目录供排查。首版不自动清理曾发布的代，正在运行的进程继续挂载原目录。手动恢复时用原代路径重新启动。这里保证导入进程失败时的可见性，异常断电恢复另做验证。

### 5. 预览和 AI 编辑

Win64 主用例选用 Khronos Lantern 的外置资源版本。模型有旋转根节点、三个网格子节点和一张共享 Base Color 图。实施时固定上游提交和所用文件校验值并记录来源。样例通过同一条导入流程烘焙，使用 Game 已有的 `Game` 挂载名，另附带独立相机、方向光和可受影的地面。单网格多材质及实例共享由小型补充用例覆盖。[Lantern 源文件](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/Lantern/glTF/Lantern.gltf#L203-L358) · [模型授权](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/Lantern/LICENSE.md)

`LIMITLESS_DEMO_CONTENT` 指向具体版本根，`LIMITLESS_DEMO_SCENE` 指向可编辑副本。用 `SceneTool check --json <作者场景> <mount> <该代 Assets 的绝对路径>` 检查。F5 用当前挂载的资产重新加载作者场景。验证换用重导入产物时，重新启动进程并挂载新代，可继续沿用同一作者场景。

## Risks / Trade-offs

- 导出器重排会增加清单更新工作。只读 `inspect` 提供定位线索，重绑定由人或 AI 在清单中显式写入并留下可审阅的 diff。
- 当前采样和光照会与 glTF 查看器有差异。画面验收关注形体、UV、Base Color、方向光和投影，匹配这些语义的简单测试件用于精确比对。
- 保留旧版本需要磁盘空间。首版用独立导入集目录隔离版本，发布不回收正在使用的资源，清理策略依据后续实际使用另定。

## Migration Plan

先把 Lantern 作为独立的 Win64 样例接入，原有 OBJ/PPM 样例仍按既有素材清单和命令构建。已发布的新样例无法通过验收时，可选择旧资产代重新启动 Game。恢复不要求修改场景 UUID 或原项目内容。
