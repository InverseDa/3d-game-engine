# renderer-directional-lighting Specification

## Purpose

让场景作者用一个世界空间方向光照亮具备顶点法线的不透明网格，并使同一份网格、材质与灯光输入在相机移动及场景重启后仍能得到可复现的着色结果。

## Requirements

### Requirement: Publish one directional light per render snapshot

场景快照 SHALL 包含零或一个启用的方向光。灯光 SHALL 携带单位世界空间朝光方向和线性 RGB 强度。填充失败或发现多盏启用灯时 SHALL 拒绝该快照。灯光数据 MUST NOT 依赖相机、光源实体的位置或父变换。

#### Scenario: Extract the same light across camera motion

- **WHEN** 从有一盏启用灯和若干停用灯的场景构建快照，再漫游相机或移动灯光实体
- **THEN** 两次快照携带同一个启用光源及等价的光照值，朝光方向与作者所填的光线传播方向相反

### Requirement: Shade authored normals in linear space

BasePass SHALL 在有启用灯且网格提供法线时，以变换并归一化后的顶点法线插值计算 Lambert 漫反射。世界空间法线 SHALL 使用模型线性变换的逆转置。光照 SHALL 在线性空间乘到现有 Base Color 结果，输出保持不透明。

#### Scenario: Facing, perpendicular, and opposite normals

- **WHEN** 同一场景中有朝光、侧对光和背光的法线，并在保持相机和材质不变时反转光线
- **THEN** 朝光和背光区域的亮暗互换，垂直区域保持零直接漫反射，带贴图和纯色材质都符合预期线性乘色

#### Scenario: Illuminate a tilted normal under nonuniform scale

- **WHEN** 带倾斜作者法线的网格应用非均匀缩放或有效镜像变换
- **THEN** 着色与逆转置参考结果一致，漫游相机时同一表面点的照明保持一致

#### Scenario: Reject a draw that has no valid normal transform

- **WHEN** 有启用灯的候选场景包括带顶点法线、但法线变换不可逆或结果非有限的网格
- **THEN** Game 报出该实体和原因并拒绝发布候选场景，当前可用场景继续呈现

### Requirement: Preserve legacy draws and frame isolation

无启用灯或网格无顶点法线时，BasePass SHALL 使用原有贴图和纯色输出。启用灯强度为零时，有法线网格的直接漫反射 SHALL 为零。连续帧的灯光数据 SHALL 归属于各自的提交帧。

#### Scenario: Mix lit and legacy meshes

- **WHEN** 同一帧绘制 v3 法线网格和原 v1/v2 无法线网格，再停用方向光
- **THEN** 光照只影响启用期间的 v3 绘制，无光照绘制保持原像素结果

#### Scenario: Render through frame and window transitions

- **WHEN** 在两个帧槽间呈现不同的灯光值，再调整窗口尺寸、最小化后恢复并退出
- **THEN** 每一帧只读取本帧的灯光值，resize 不触发纹理重传，Vulkan 验证开启且错误为零
