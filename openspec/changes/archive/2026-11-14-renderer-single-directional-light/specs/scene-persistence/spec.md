# Scene Persistence Delta

## MODIFIED Requirements

### Requirement: Durable scene identity and authored data

场景 SHALL 使用可直接编辑的 UTF-8 JSON 保存实体持久 ID、可选局部 TRS 和父 ID，以及 Mesh 和 Camera 组件。Scene v1 保持原有字段，Scene v2 可额外保存 DirectionalLight 组件。Mesh SHALL 通过持久 Asset ID 引用网格与材质。场景文件 MUST NOT 保存运行时句柄、World 域、GPU 对象、派生矩阵或玩法执行状态。

#### Scenario: Round trip a multi-entity scene across processes

- **WHEN** 保存包含空实体、仅有 Transform 的父节点、两个共享网格和材质的子实体及一个有效主相机的场景，再启动新进程加载
- **THEN** 恢复全部实体 ID、父子关系、等价局部 TRS、相机参数、组件字段及资产引用，两个子实体共享本次会话的资源句柄

## ADDED Requirements

### Requirement: Author and validate a directional light in JSON

Scene v2 SHALL 支持实体的可选 `directionalLight` 对象。存在时 SHALL 包含有限非零的世界空间传播方向 `direction`、每通道在 [0,1] 的线性 RGB `color`、[0,16] 内的有限标量 `intensity` 和布尔值 `active`。场景 MUST NOT 有第二盏启用灯，Scene v1 MUST NOT 接受该组件。

#### Scenario: Validate a script-authored light without graphics

- **WHEN** AI 或脚本按实体 UUID 在 Scene v2 副本中编辑方向、颜色、强度或启用状态，再运行 `SceneTool check`
- **THEN** 合法编辑通过且文件字节不变；无效向量、颜色、强度、字段或第二盏启用灯使工具以非零状态退出，诊断指出文件、字段和相关实体

#### Scenario: Validate corrupt normals as a mesh dependency

- **WHEN** SceneTool 或运行时加载场景引用的 mesh v3，且其中的法线数据损坏
- **THEN** 校验失败并报告实体和网格 Asset ID；若是运行时替换，当前可用场景与漫游机位保持可用

### Requirement: Save and restore authored light fields

保存 SHALL 保留方向光的原始方向、颜色、强度、启用状态及实体身份。带方向光组件的文档 SHALL 写为 Scene v2。完全没有方向光组件的场景 SHALL 写为原有的 Scene v1 规范形式。

#### Scenario: Round trip an active and an inactive light

- **WHEN** 保存含一盏启用灯和一盏停用灯的场景，重新加载并保存同一作者状态
- **THEN** 两个实体和字段值均恢复，单位方向只在渲染提取阶段派生，重复保存产生相同字节

#### Scenario: Reject a second active light during scene replacement

- **WHEN** Game 已发布有效场景和会话相机，候选场景启用了两盏灯
- **THEN** 候选加载以带定位信息的错误失败，原场景、漫游机位和渲染可继续使用

#### Scenario: Save an unchanged legacy scene

- **WHEN** 读取无光源的旧 Scene v1 并保存相同的作者状态
- **THEN** 输出使用原有 Scene v1 规范形式，实体及资产引用不变
