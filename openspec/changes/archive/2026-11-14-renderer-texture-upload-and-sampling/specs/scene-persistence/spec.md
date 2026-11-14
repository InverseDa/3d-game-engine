# Scene Persistence Delta

## ADDED Requirements

### Requirement: Validate textured scene references without graphics

场景检查和恢复 SHALL 沿 Mesh、Material 引用检查 Base Color 纹理的 ID、类型和有效数据。纹理材质引用的网格 SHALL 有完整 UV。JSON v1 继续保存原有 Mesh/Material Asset ID。无窗口检查 SHALL 不创建 GPU 对象，失败时 SHALL 标出实体、资源及原因。

#### Scenario: Reload an existing scene with updated material

- **WHEN** 在相同 UUID 下给网格加入 UV、给材质添加有效纹理引用，再校验并加载之前的场景文件
- **THEN** 原场景 JSON 可直接使用，运行时得到新的纹理材质

#### Scenario: Reject a broken material dependency

- **WHEN** 纹理缺失、类型或数据错误，或者纹理材质用于无 UV 网格
- **THEN** 无窗口校验失败并定位问题，运行时拒绝发布候选场景，已有场景保持可用
