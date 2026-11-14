# Scene Persistence

## Purpose

场景文件保存可编辑的场景数据和持久资源引用，使场景能够在进程重启后重建，并继续由现有渲染链路消费。保存和恢复的失败不破坏上一个可用结果。

## ADDED Requirements

### Requirement: Durable scene identity and authored data

场景 v1 SHALL 使用可直接编辑的 UTF-8 JSON 保存每个实体的持久 ID、可选局部 TRS 和父 ID，以及受支持的 Mesh 和 Camera 组件。Mesh SHALL 通过持久 Asset ID 引用网格与材质。场景文件 MUST NOT 保存运行时句柄、World 域、GPU 对象、派生矩阵或玩法执行状态。

#### Scenario: Round trip a multi-entity scene across processes

- **WHEN** 保存包含空实体、仅有 Transform 的父节点、两个共享网格和材质的子实体及一个有效主相机的场景，再启动新进程加载
- **THEN** 恢复全部实体 ID、父子关系、等价局部 TRS、相机参数、组件字段及资产引用，两个子实体共享本次会话的资源句柄

### Requirement: Recoverable scene restore

加载 SHALL 验证场景版本、实体身份、层级、组件数据及资源引用，并在候选 World 完成后才发布。失败 SHALL 提供原因并保留当前 World、已发布资源和可用的渲染状态。

#### Scenario: Reject invalid scene content

- **WHEN** 场景有不支持的版本或组件、重复 ID、悬空父引用、环、非法 TRS 或相机数据、缺失或类型不符的资源
- **THEN** 返回带文件及相关实体或资源的错误，当前场景仍能继续更新和渲染

#### Scenario: Recover after a partial candidate load

- **WHEN** 候选场景已加载第一个资源但随后读取、分配、构造或恢复失败
- **THEN** 释放候选场景及其资源，已发布场景和资源保持可用，重试可从有效文件恢复

### Requirement: Deterministic and atomic scene save

保存 SHALL 从已提交状态生成完整场景文档，采用稳定实体顺序和明确的版本。相同文档 SHALL 得到相同字节。编码、写入或文件替换失败时 SHALL 保留之前已保存的文件。

#### Scenario: Save a supported edit

- **WHEN** 对已提交场景中的一个实体修改局部 Transform 并保存
- **THEN** 重新加载得到更新后的 Transform 和原有实体 ID、层级及资源引用

#### Scenario: Edit a scene without starting the renderer

- **WHEN** 直接编辑文本场景中的 Transform 并运行无窗口校验
- **THEN** 工具校验文档与资源引用，失败时定位到字段或实体，成功时原文档保持可继续编辑

#### Scenario: Preserve the previous file after save failure

- **WHEN** 目标已有有效文件，而本次保存遇到未提交状态、未受支持的实体数据或写入或替换失败
- **THEN** 返回错误，目标仍能读取为上一次有效版本

### Requirement: Render the scene loaded from disk

Win64 Debug Game Demo SHALL 从磁盘加载 M1 样例，并用现有 WorldRenderer 和 BasePass 到 Composite 链路渲染。窗口尺寸变化 SHALL 保持多实体的材质和网格引用有效。

#### Scenario: Display and resize an imported scene

- **WHEN** 启动 Demo，加载含两个可见子网格和主相机的样例，并调整窗口尺寸、最小化后恢复再正常退出
- **THEN** 两个网格在可渲染帧中使用从磁盘加载的资源，窗口恢复后仍可见，Vulkan 验证启用且日志没有 validation error
