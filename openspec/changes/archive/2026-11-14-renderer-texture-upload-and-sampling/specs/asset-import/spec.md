# Asset Import Delta

## MODIFIED Requirements

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

## ADDED Requirements

### Requirement: Compatible UV and texture material payloads

读取器 SHALL 支持已有 v1 网格、材质、纹理，以及新增的 UV 网格与纹理材质版本。UV 或纹理引用的重新导入 SHALL 可以沿用原 Asset ID、类型和逻辑路径。未知版本或类型与版本组合 SHALL 报错。

#### Scenario: Reimport a material with a texture

- **WHEN** 用原 ID、类型和路径给 v1 纯色材质增加合法纹理 UUID 并重新导入
- **THEN** 得到新格式材质，既有场景引用仍有效，现有 v1 纹理仍可被它读取
