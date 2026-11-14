# Tasks

## 1. 文件操作与资源格式

- [x] 1.1 实现 ContentIO 的有界读、路径解析和同目录原子替换。验证 Unicode、不同当前目录、根逃逸、symlink/junction 及替换失败保留原文件；记录平台语义。
- [x] 1.2 定义 Mesh、Material、CPU Texture 的 v1 资源格式、稳定类型 ID、显式字节编解码和大小限制。验证固定输入编码一致，截断、错误长度、溢出、非法数值及未知版本被拒绝；记录字段和限制。

## 2. 离线导入

- [x] 2.1 实现 AssetImport 的受限 OBJ 转换。验证坐标、索引、绕序、包围盒，以及多边形、无效坐标和越界索引的报错。
- [x] 2.2 实现文本 PPM P3、二进制 PPM P6 和纯色 Material 转换。验证像素值、尺寸、alpha、P6 首像素字节恰好为空白字符、截断与非法 RGB。
- [x] 2.3 注册 AssetImport Program 和文件发布命令，文档列出源格式子集与重建命令。使用可编辑材质样例和固定 UUID 导入两次，对比产物；验证无写入校验、源错误定位、目标身份冲突或写入失败保留旧产物，Program 构建图没有 RHI/Renderer。

## 3. 同步加载与 Renderer 接线

- [x] 3.1 实现显式挂载和可替换的只读目录索引。验证 ID/规范化路径定位同一条目，跨工作目录一致，重复 ID、路径、大小写别名和文件头不匹配时保留旧索引。
- [x] 3.2 实现类型检查、缓存、CPU Texture 及 provider 发布边界，记录输出句柄和借用生命周期。用 fake provider 验证同会话复用、错误类型、丢失文件、索引后篡改、注册失败和构造失败；核对临时资源释放与重试。
- [x] 3.3 接入 Renderer 的 VB/IB 和纯色材质 provider，让固定 BasePass pipeline 脱离尺寸资源，删除 resize 时对单个实体做 Material Replace 的流程。用测试验证第二个 GPU 缓冲创建失败时回收第一个，两个实体共享的材质在 resize 后句柄有效，销毁前完成相关帧。

## 4. 场景保存和恢复

- [x] 4.1 定义并记录 UTF-8 JSON 场景 v1 字段、持久实体 ID 与版本拒绝规则。验证稳定格式、记录限制、重复键和 ID、未知字段、悬空父引用、环、非法 TRS 和相机数据。
- [x] 4.2 实现 Scene owner、实体 ID 存储和 Idle 快照捕获，使用 Asset ID 持久化资源引用。验证空实体、Transform 父节点和 Mesh/Camera 字段；未提交状态、无 ID 实体、坏句柄或 I/O 失败不得覆盖有效存档。
- [x] 4.3 实现两遍候选恢复，先构造实体与组件再连接父级。用独立 registry 的测试覆盖资源解析失败、类型不匹配、分配或应用失败，验证旧 World 和资产不受影响。用独立进程加载保存结果，检查持久 ID、TRS 容差、父子关系和共享句柄。

## 5. 多实体 Demo 与发布验收

- [x] 5.1 接入可重建的源资产、可直接编辑的场景样例及无窗口场景校验命令。将导入和 staging 接入 Win64 Debug Demo 的构建流程，依赖图包含输入及产物；用干净输出目录构建并核对实际可执行文件旁的内容。
- [x] 5.2 让 Demo 从 staging 加载候选场景并在 Idle 发布，提供可重复的局部 Transform 修改、保存及重启加载入口，文档给出命令与成功日志。验收坏文件不发布、重启恢复编辑、两个可见实体共享磁盘资产以及有效主相机。
- [x] 5.3 执行 Win64 Debug Program/Game/Editor/Test 构建、Builder 测试、C++ Test target 和 C++ 规范扫描。启动 Game 验证首帧、多物体显示、resize、最小化、恢复、正常退出和保存重启；确认 Vulkan validation messenger 已安装且 `[VK Validation][Error]` 为零。记录命令、设备和验收结果。
