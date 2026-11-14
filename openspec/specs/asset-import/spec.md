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

### Requirement: Import and decode authored OBJ normals

OBJ 导入 SHALL 接受有限非零的 `vn` 和引用完整的 `v//vn` 或 `v/vt/vn` 三角面。每个文件 SHALL 使用一种面格式。法线 SHALL 归一化，并按位置、UV 与法线的索引组合保留两种接缝及原绕序。法线网格 SHALL 使用 mesh v3，保留原资产 ID、类型及逻辑路径。

#### Scenario: Preserve a normal seam independently from a UV seam

- **WHEN** 相邻三角形共用位置，但各自指定不同法线或不同 UV，并以相同参数重复导入
- **THEN** 网格在各接缝处拆分顶点，UV 与法线分别保留，三角形绕序和导入产物字节稳定

#### Scenario: Accept normals with or without UVs

- **WHEN** 分别导入带有效 `v//vn` 和 `v/vt/vn` 三角面的两个文件
- **THEN** 两者都得到 v3 网格和每顶点单位法线，仅后者携带 UV

#### Scenario: Reject malformed normal data without publication

- **WHEN** 源法线为零或非有限、索引缺失或越界、同一文件混用面格式，或产物含非法标志、截断或非法法线
- **THEN** 导入校验或解码失败并提供原因，先前有效的产物和已发布网格仍可用

### Requirement: Preserve meshes without authored normals

仅有位置或位置与 UV 的现有 OBJ SHALL 继续产出原来的 mesh v1/v2，解码其产物时法线集合 SHALL 为空。v3 SHALL 只用于网格，材质与纹理继续使用各自已有的版本。

#### Scenario: Rebuild an old mesh under the same identity

- **WHEN** 使用相同源文件、ID、路径和导入参数重建 v1 或 v2 网格
- **THEN** 产物与原产物逐字节相同，现有引用继续可用
