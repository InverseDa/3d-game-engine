# Tasks

## 1. RHI 上传与 GPU 完成状态

- [x] 1.1 接入受检的 Buffer→Texture2D 拷贝和 Vulkan 实现，补齐录制、提交和 fence 完成状态。记录支持范围，验证错误设备、usage、格式、shape、字节范围、渲染 scope 或无效 Vulkan handle 被拒绝。
- [x] 1.2 实现 Renderer 同步上传。用 fake RHI 检查像素字节、屏障与拷贝顺序、完成前不发布，以及 create/map/record/submit/wait 各阶段注入失败后的资源所有权；确认 wait 失败不会提前释放可能仍在执行的提交。

## 2. 资产导入和同步依赖

- [x] 2.1 增加 UV 网格和纹理材质的 v2 codec，记录布局及兼容规则。验证 v1 三种资源、v2 round-trip、未知版本组合、截断与非法数据。
- [x] 2.2 扩展文本 OBJ 和材质指令，确保 OBJ 在 Git 中以文本展示。用 UV seam、V 方向、重复指令和坏索引用例验证 `--check`、诊断、确定性重建及同 UUID 原子重导入。
- [x] 2.3 同步解析一跳 Material→CPU Texture 依赖，维护现有 CPU 加载接口和重入限制。验证 ID/类型/内容错误、已有条目复用、输出值保留及 provider/登记失败回滚，更新加载文档。

## 3. Renderer 材质采样与场景接线

- [x] 3.1 创建按会话和 ID 复用的 GPU image/view 及材质 bind group，并处理白纹理和共享 sampler。验证两个材质对同一贴图只触发一次上传，登记失败撤回候选引用，正常回收先销毁绑定再销毁图像。
- [x] 3.2 串接顶点 UV、WorldRenderer 快照、RFG Pixel 读声明和 BasePass shader。验证多实体共享同一 texture 只产生一次 RFG 读取，纹理 A→纯色→B→A 各 draw 绑定正确，v1 纯色输出保持，采样色空间和 alpha 行为与规格一致。
- [x] 3.3 接通 SceneTool 与候选场景发布。用无窗口用例验证缺失、错类型、损坏依赖及无 UV 的组合，再用运行时测试确认失败保留旧场景和资源。记录如何编辑文本并运行验证。
- [x] 3.4 把非对称 2×2 PPM、UV 网格和交错材质纳入样例导入与 staging。验证使用现有 UUID 的文本修改能重建，并通过原有 JSON 场景读取；保存与恢复保持原引用。

## 4. 集成验收

- [x] 4.1 构建 Win64 Debug Program/Game/Editor/Test，运行 Builder 和 C++ 测试及规范扫描。使用实际 Vulkan 捕获比较四角内部区域、线性乘色与纯色对照，记录期望颜色和容差。验证多帧、resize、最小化恢复、正常退出和保存后重启，上传计数在 resize 前后不增加且 Khronos Validation 错误为零。
