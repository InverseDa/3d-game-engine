# asset-import Specification

## Purpose

离线工具将源内容转换为运行时能独立读取的资源，产物带持久标识、类型和版本。项目可提交源文件和导入参数，并在另一台机器重建等价产物。

## Requirements

### Requirement: Offline conversion of M1 assets

导入工具 SHALL 支持受限 OBJ 静态三角网格、受限 PPM P3/P6 图像和不透明材质，并输出各自版本化的资源文件。OBJ 可包含 UV，材质可引用一个 Base Color 纹理 UUID。转换 SHALL 无需创建窗口、图形设备或 World。

#### Scenario: Import a static triangle mesh

- **WHEN** 导入仅使用有限位置坐标和有效三角形位置索引的受支持 OBJ
- **THEN** 输出具有三角索引和顶点数据的网格资源，解码时得到确定的包围盒，可交给现有 BasePass

#### Scenario: Import CPU texture pixels

- **WHEN** 导入最大分量值为 255 的受支持 PPM P3 或 P6
- **THEN** 输出带尺寸的 RGBA8 纹理数据，保持行序和 RGB 值，alpha 为 255

#### Scenario: Author an opaque material

- **WHEN** 文本材质源提供三个有限、非负的 RGB 分量
- **THEN** 工具输出可用于现有 BasePass 的纯色不透明材质资源

#### Scenario: Preserve a UV seam

- **WHEN** 受支持的 OBJ 在相邻三角形中给同一位置指定不同的有效 UV
- **THEN** 导入结果保留两侧 UV，重复导入得到相同字节

#### Scenario: Author a texture dependency in text

- **WHEN** 文本材质同时包含有效的纯色色值和一条有效的 Base Color 纹理 UUID
- **THEN** 导入结果保留两者，无写入检查使用同一规则；无效 UUID、重复指令或越界 UV 索引报告源文件和位置

### Requirement: Stable identity and safe output

导入工具 SHALL 接受显式持久 Asset ID 和符合 v1 跨平台 ASCII 命名约定的规范化虚拟路径。相同源字节、ID、路径和导入参数 SHALL 得到相同产物。输入无效或写入失败时 SHALL 报错并保留原有有效文件。

#### Scenario: Rebuild an existing asset

- **WHEN** 用原 ID、路径、类型和相同输入重新导入
- **THEN** 产物字节保持一致，已有场景引用仍使用同一 Asset ID

#### Scenario: Reject invalid input or identity conflict

- **WHEN** 源内容超限、截断、索引越界、格式不受支持，或目标文件已有不同 ID、路径或类型
- **THEN** 命令返回非零并报告文件和原因，原有产物保持可读

### Requirement: Editable sources can be validated from the command line

资产源和样例重建参数 SHALL 可直接编辑和版本管理。导入工具 SHALL 提供无需启动引擎、无写入副作用的校验模式。格式错误 SHALL 报告源文件及可获得的行号或字节位置。

#### Scenario: Validate an automated source edit

- **WHEN** 修改样例材质文本后运行校验命令，再用相同 ID 与路径构建
- **THEN** 校验阶段不创建产物，随后构建产物的材质数据反映编辑，原资产引用仍有效

### Requirement: Compatible UV and texture material payloads

读取器 SHALL 支持已有 v1 网格、材质、纹理，以及新增的 UV 网格与纹理材质版本。UV 或纹理引用的重新导入 SHALL 可以沿用原 Asset ID、类型和逻辑路径。未知版本或类型与版本组合 SHALL 报错。

#### Scenario: Reimport a material with a texture

- **WHEN** 用原 ID、类型和路径给 v1 纯色材质增加合法纹理 UUID 并重新导入
- **THEN** 得到新格式材质，既有场景引用仍有效，现有 v1 纹理仍可被它读取
