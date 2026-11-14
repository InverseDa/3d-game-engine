# Asset Import Delta

## ADDED Requirements

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
