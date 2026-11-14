# scene-camera-navigation Specification

## Purpose

让用户在加载的场景中从作者选定的主相机机位出发，用键鼠检查场景内容，并使渲染视图与可见性判断始终使用同一会话机位。

## Requirements

### Requirement: Start from the authored primary camera

Win64 Game SHALL 使用现有主相机选择规则。每次成功加载并发布场景后，漫游 SHALL 从所选相机的世界位置和朝向开始，投影继续使用该相机的视场角和近远裁剪面。一次失败的场景替换 SHALL 保留当前场景和漫游机位。

#### Scenario: Start with a parented primary camera

- **WHEN** 场景包含多个候选相机且所选相机挂在有效的刚性父层级中
- **THEN** 首帧采用已有主相机规则选出的机位，位置和朝向包含父变换；无输入时视图与开始机位一致

#### Scenario: Replace a scene during navigation

- **WHEN** 已移动当前机位，分别验证替换为无效场景和替换为有效场景
- **THEN** 失败时保留当前场景和机位；成功时从新场景的主相机起步，切换前的待消费输入不作用到新机位

### Requirement: Control orientation and movement in the scene

Win64 Game SHALL 在窗口具备输入条件时用 WASD 移动。W/S 沿视线前后移动，A/D 向相机两侧移动，组合方向的移动速度 SHALL 与单轴速度一致。按住右键时，鼠标横向移动控制偏航，纵向移动控制俯仰。场景视图和视锥剔除 SHALL 在同一渲染帧使用更新后的漫游机位。

#### Scenario: Move with WASD and look with the right button

- **WHEN** 有焦点时单独按 W、S、A、D，再组合按键并按住右键移动鼠标
- **THEN** 松开右键时仍可键盘移动，右键捕获期间才可转向；移动方向随朝向改变，相反键抵消，斜向不加速

#### Scenario: Present the updated view and visibility

- **WHEN** 漫游使可见物体移出视锥，并使先前被剔除的物体进入视锥
- **THEN** 该帧使用同一机位计算投影视图和剔除结果；重新转回时，此处的贴图与纯色材质继续按原链路渲染
