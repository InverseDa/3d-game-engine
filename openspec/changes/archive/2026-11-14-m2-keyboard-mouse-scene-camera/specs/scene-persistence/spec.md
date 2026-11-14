# Scene Persistence Delta

## MODIFIED Requirements

### Requirement: Deterministic and atomic scene save

保存 SHALL 从已提交状态生成完整场景文档，采用稳定实体顺序和明确的版本。相同文档 SHALL 得到相同字节。编码、写入或文件替换失败时 SHALL 保留之前已保存的文件。场景相机的漫游只作用于当前会话。漫游 MUST NOT 改写源文件或进入显式场景保存的结果。

#### Scenario: Save a supported edit

- **WHEN** 对已提交场景中的一个实体修改局部 Transform 并保存
- **THEN** 重新加载得到更新后的 Transform 和原有实体 ID、层级及资源引用

#### Scenario: Edit a scene without starting the renderer

- **WHEN** 直接编辑文本场景中的 Transform 并运行无窗口校验
- **THEN** 工具校验文档与资源引用，失败时定位到字段或实体，成功时原文档保持可继续编辑

#### Scenario: Edit the authored camera from JSON

- **WHEN** AI 或脚本按稳定实体 UUID 直接修改 Scene v1 副本中的主相机 `transform` 及 `camera.verticalFieldOfViewRadians`，以相同的已导入资产挂载运行 `SceneTool check`，再启动 Win64 Game 加载副本
- **THEN** 合法修改通过无窗口校验，校验期间文件字节不变，Game 首帧使用新姿态和视场角；若将视场角设为 0 或将旋转四元数设为零，工具以非零状态退出并定位文件中的相机字段或实体

#### Scenario: Preserve the authored pose after navigation

- **WHEN** 按场景原有的显式保存流程保存同一作者状态，一次未经漫游，一次经过键鼠漫游
- **THEN** 两份存档字节相同，源场景未被改写；重新启动加载源场景或存档均从作者机位起步

#### Scenario: Preserve the previous file after save failure

- **WHEN** 目标已有有效文件，而本次保存遇到未提交状态、未受支持的实体数据或写入或替换失败
- **THEN** 返回错误，目标仍能读取为上一次有效版本
