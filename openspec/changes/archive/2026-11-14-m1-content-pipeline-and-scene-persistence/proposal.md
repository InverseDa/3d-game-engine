# Proposal

## Why

当前 Demo 在启动时手写创建顶点、材质和场景实体，Asset 只能登记已在内存中的对象，World 也没有持久化的场景格式。M1 先让开发者和 AI 直接编辑可纳入 Git 的内容源，通过相同的命令行检查和构建流程，再让场景在进程重启后恢复并由现有 BasePass 显示。

## What Changes

- 提供无需图形设备的离线导入命令，读取受限 OBJ 静态网格、受限 PPM P3/P6 纹理和可编辑的纯色材质源。保留可重建文本样例、固定资源 ID 与明确的错误位置。输出有版本、类型和持久 Asset ID 的引擎资源文件。
- 支持显式目录挂载，按持久 ID 或规范化虚拟路径同步加载已导入的资源。重复加载复用类型化运行时句柄。网格和材质能进入 Renderer，纹理在 M1 加载为 CPU 像素数据。
- 提供 UTF-8 文本场景和保存恢复 API。场景 v1 持久化实体 ID、父子关系、局部 Transform、Mesh/Material 引用和相机。保存采用原子替换，恢复先构建候选场景，再发布。
- 用磁盘上的多实体场景驱动 Demo，验收导入、加载、渲染、保存和重启恢复。纹理 GPU 上传与采样、复杂材质、异步加载、热重载、编辑器和任意组件反射恢复留给后续里程碑。

## Capabilities

### New Capabilities

- `asset-import`: 将限定格式的源内容转换为有版本和持久 ID 的引擎资源文件。
- `synchronous-asset-loading`: 通过显式挂载和目录索引，同步加载、缓存并管理类型化资源。
- `scene-persistence`: 保存受支持的场景内容，从磁盘完整恢复并交给现有渲染链路。

### Modified Capabilities

无。当前 OpenSpec 主规格库为空。

## Impact

- 新增内容文件 I/O、同步资源加载、场景持久化模块和离线导入 Program。现有 Asset registry 继续负责运行时资源身份与生命周期。
- Renderer 提供网格和材质的 GPU 构造入口；World 提供场景所需的已有组件接口；DemoApplication 负责组装加载、发布、渲染和退出顺序。
- 增加内容格式、失败回滚、跨进程恢复及 Vulkan Demo 验收。通过 LimitlessBuilder 的 Program、Game、Editor 和 Test 入口接入。
