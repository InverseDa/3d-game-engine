# Design

## Context

- [Asset](../../../../Docs/Asset.md) 已有持久 ID、规范化虚拟路径、类型化句柄与同步 registry。注册表仅持有已构造对象，支持通过 provider thunk 在创建方释放。
- [World](../../../../Docs/World.md) 已有 Transform 层级、Mesh/Camera 组件和 Idle 提交边界。World 不可移动，实体句柄只在所属 World 实例中有效。[Reflection P0](../../../../Docs/Reflection.md) 提供属性数据编解码，还没有数组与通用原生对象恢复。
- Demo 现在创建内置 VB/IB 和材质，把借用 payload 注册进 registry，并在 resize 时重建 BasePass pipeline 和替换一个实体的材质句柄。M1 会加载多个实体，已有单实体替换逻辑需要移除。

## Goals / Non-Goals

- **Goals**：保持内容格式和场景逻辑独立于图形设备，复用 Asset registry、World 及现有渲染路径。在同步发布边界定义清楚文件、World、资源对象与 GPU 命令的生命周期。
- **Non-Goals**：通用 VFS、包文件、增量 cook、运行时重导入、资源依赖图、热更新、多线程加载、纹理 GPU 上传、场景版本迁移、通用组件序列化和编辑器。

## Decisions

### 1. 按文件 I/O、资源格式、对象加载和场景职责划分模块

| 模块 | 责任与直接依赖 |
|---|---|
| 新增 `ContentIO` | 依赖 Core。负责路径约束、受限读文件、同目录临时文件和原子替换。原生文件类型和操作系统实现保留在私有边界。 |
| 新增 `AssetFormat` | 依赖 Core、Asset。定义 v1 资源头与 Mesh、Material、Texture 的 CPU 数据和纯内存 codec。Texture 的稳定逻辑类型 ID 和句柄沿用 Asset 的类型约定，CPU payload 定义放在 AssetFormat。 |
| 新增 `AssetImport` Program | 依赖 Core、Asset、ContentIO、AssetFormat。负责源格式转换和离线产物发布，无 World、Renderer 或 RHI 依赖。 |
| 新增 `AssetLoading` | 依赖 Core、Asset、ContentIO、AssetFormat。负责挂载、目录索引、类型化加载和 registry 缓存。通过显式 provider 接收 Renderer 的构造/销毁函数，CPU Texture 可在本模块内构造。 |
| 现有 `Renderer` | 增加对 AssetFormat 的依赖，从已验证的 CPU Mesh/Material 数据创建现有 payload。Demo 注册 Renderer provider；Renderer 不依赖加载器。 |
| 新增 `Scene` | 依赖 Core、World、Asset、ContentIO。拥有场景格式、实体持久身份和 World 候选。使用应用传入的类型化资源解析回调，并从候选或当前 registry 查询持久 Asset ID。 |
| 现有 `DemoApplication` | 组装挂载、provider、加载会话、World 候选和 Renderer，负责空闲帧边界发布与 GPU 资源回收。 |

把读文件与 GPU 构造并入 Asset 会让当前 Core-only 模块承担平台和图形依赖。以上边界还允许纯 CPU 测试驱动导入器、目录和 Scene。跨 DLL 的对象由创建模块释放，公开边界使用已有引擎类型。

### 2. 首版采用小而明确的离线格式

- OBJ 只支持 `v x y z`、正数一基的 `f i j k`、空行和注释。保留坐标和绕序，导入默认白色顶点色，生成 uint32 索引和局部 AABB。其他几何记录、UV、法线、负索引及多边形在 v1 报错。
- PPM P3/P6 使用最大分量值 255，支持 header token 间空白和注释。样例采用可直接编辑像素值的 P3 文本。P6 最大值后的栅格边界明确，不能继续跳过可能属于首个像素的字节。按原行序扩成 RGBA8，无 mip。纯色材质使用一条 `base-color r g b` 文本指令，RGB 有限且非负。
- 三类资源各输出一个 `.leasset`，使用显式小端字段编码。文件头包含 magic、版本、类型 ID、Asset ID、canonical path 和 payload 长度。Mesh 保存显式 float32 位置和 uint32 索引，Material 保存 RGB，Texture 保存宽高及像素。派生的 bounds 由 Mesh codec 按顶点确定性构造，不使用原生结构体内存布局。
- CLI 接受源路径、持久 UUID 和目标逻辑路径，并提供不写入文件的检查模式。将样例的源文件和固定导入参数纳入 Git，同一身份和输入可重建同字节。版本、记录长度、计数、乘法溢出、有限数值和边界在分配或读 payload 前检查，并为数据规模设置上限。
- 以可读源文件和固定参数作为测试样例的权威输入，产物可重建。glTF、PNG、通用第三方解析器或 GPU 纹理采样会扩大首版范围，留给后续有实际消费方的阶段。

### 3. 挂载与同步加载在会话内完成

- 挂载显式提供虚拟 mount 与物理根目录。应用先确定绝对根目录，样例使用构建输出旁的 staging 目录，测试用显式根路径。文件枚举限制在根内并拒绝 symlink/junction 等可重定向项。目录按稳定顺序读取 `.leasset` 头，验证逻辑路径等于 mount 加去掉后缀的相对路径。v1 逻辑路径使用便于跨平台移植的 ASCII 子集并拒绝大小写别名，物理挂载目录可使用 Unicode。完整索引成功后才发布。
- 请求先以 ID 或 canonical path 查同一目录条目并校验类型，然后查会话 registry。未命中时读并重新校验实际头和数据，构造 CPU/GPU 候选对象，最后登记并返回句柄。失败只释放本次候选对象，保留已发布条目。禁止跨 registry 使用句柄或在 mutation 后保留借用 view。
- Renderer 使用现有 host-visible Upload VB/IB 路径填充 Mesh；不增加 transfer queue。纯色材质借用固定 BGRA8 sRGB + D32 BasePass pipeline，pipeline 的动态 viewport/scissor 允许它独立于窗口 extent。Resize 只更新随尺寸变化的资源和 Composite 绑定，正常 resize 不触发 Material Replace。
- Registry 接管 GPU payload wrapper 后，由 provider thunk 释放缓冲；pipeline 由 Renderer/应用 owner 持有并晚于所有材质销毁。应用先等待引用旧资源的帧，再注销 payload、销毁 pipeline 和设备。缓存到会话结束，后续再引入单资源卸载。

使用目录扫描和同步构造足以验证 M1，也能把资源源格式留在离线侧。集中清单、跨场景缓存和异步发布可以在资源规模证明有需要后引入。

### 4. 用专用场景文档恢复候选 World

- 场景 v1 使用 UTF-8 JSON 文档，顶层包含 `version` 和 `entities`。记录含 UUID、可选局部 TRS 和父 UUID、Mesh/Material Asset ID、Mesh 调试名称和排序键、Camera 调试名称及 FOV/near/far/active/priority。64 位无符号排序键使用十进制字符串，确保通用编辑器能无损修改。公开字段与示例并校验未知字段、重复键和数据上限，诊断包含文件路径与字段位置。按固定顺序缩进保存，便于 Git diff。场景时间、玩法调度、派生矩阵和运行时句柄由运行时管理。
- Scene owner 维护附加在 World 的 ID 组件，创建可保存实体时一并赋予唯一 UUID。保存校验完整的一对一成员关系，包括空实体；直接创建且无 ID 的实体使保存失败。Scene owner 的地址在生命周期内稳定，保证附加存储先脱离或在 World 销毁时仍有效。Mesh/Camera/Transform 之外的附加业务组件按运行时数据处理，场景文档遇到未知组件标记则报错。
- Restore 先完整解码并校验记录、父链和受支持组件。对所需的不同资源 ID 在新的加载会话逐一解析。随后按 UUID 顺序创建全部实体和组件，第二遍连接父子，再更新 Transform。场景组件与父链按现有 World API 应用，选中相机的可渲染性由 Demo 在发布前以 WorldRenderer snapshot 检查。
- 候选 World 与资源 registry 由应用共同持有，使用同一资源索引和共享设备及 pipeline。成功时应用等待旧帧后在 Idle 边界交换 owning pointer，再销毁旧会话。失败时销毁候选会话，旧 registry 的借用 view 和资源也不受候选修改影响。样例使用一个有效主相机以避开依赖运行时 ID 的同优先级相机选择。
- Save 仅在 Idle 读取身份存储和 World，检查可持久化的成员与组件，并通过 registry 将句柄映射回 Asset ID。按 UUID 排序并编码，临时文件写完、关闭后原子替换。若平台无法完成替换则返回失败，不能先删旧文件。对同一个 DTO 验证字节稳定；经过 World 的 TRS 往返按语义与数值容差比较，因为写入 Transform 会归一化旋转。

通用 Reflection 恢复需要原生对象恢复、数组和跨对象引用策略。显式 schema 更适合先验证场景与资源的边界。候选场景使用独立 registry 的代价是切换时可能暂时持有两套网格；可以先保证失败隔离和 GPU 安全，后续有内存数据后再共享缓存。

## Risks / Trade-offs

- 绝对路径拼接可能让资源逃出挂载点 → 拒绝重定向路径，校验目录根约束和虚拟路径与产物头的一致性；仅支持由应用显式挂载的本地内容目录。
- 失败时已创建部分 GPU 对象 → 发布前由候选 owner 回收；任何曾提交给 GPU 的对象仅在相关 fence 完成后回收。
- 自动编辑可能写出语法正确但引用无效的文档 → 提交可编辑源资产、场景和固定 UUID，提供 CLI 校验，先完成格式和引用校验再发布。Cook 产物可重建。
- 原子替换只覆盖可观测的完整文件切换 → 覆盖正常 I/O 和替换失败，不承诺断电后的持久性。

## Migration Plan

1. 先接入文件操作、codec、离线 Program 和测试，现有 Demo 继续作为渲染基线。
2. 加入同步加载及 Renderer provider，解耦 BasePass pipeline 与 resize。用测试验证资源在 resize、失败和退出时的生命周期。
3. 接入 Scene 候选保存恢复和可重建样例，通过验证后将 Demo 的硬编码三角形与旋转系统替换为磁盘场景。启动缺少内容时给出路径和原因，由构建/验收命令提供样例。
4. v1 作为首个文件格式。未知版本明确拒绝；发布前如需回退，回退 Demo 接线并保留源文件与导入参数，产物可重新生成。
