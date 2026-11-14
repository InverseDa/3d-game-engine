# Synchronous Asset Loading Delta

## ADDED Requirements

### Requirement: Resolve a material color texture before publication

同步加载器 SHALL 在发布有纹理的材质前按 UUID 解析并验证其 CPU 纹理依赖，再把数据交给材质构造方。独立 CPU 纹理加载 SHALL 继续可在没有 GPU 的情况下使用。依赖失败 SHALL 诊断材质和纹理身份，保留已发布句柄及输出值。

#### Scenario: Resolve a shared CPU dependency

- **WHEN** 两个材质或一次独立 CPU 请求在同一会话中使用同一个纹理 ID
- **THEN** 成功请求复用已验证的 CPU 条目，材质构造方不需要重入公共加载接口

#### Scenario: A texture dependency is invalid

- **WHEN** 纹理不存在、类型不符、数据损坏或构造失败
- **THEN** 材质不可解析，本次未完成的资源被清理，先前发布的资源仍有效；修复后可重试
