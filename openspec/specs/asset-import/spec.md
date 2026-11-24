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

### Requirement: Cook static glTF assets into existing runtime formats

导入工具 SHALL 离线转换一个选定 glTF 2.0 scene 的静态节点、索引三角网格和不透明 Base Color 材质。首版输入为 `.gltf`、根内外置 BIN 和 Base Color PNG，输出为现有 `.leasset` 和可校验的初始 Scene JSON。位置和法线必需，贴图材质使用 `TEXCOORD_0`。导入 SHALL 保留受支持的节点层级、局部 TRS、UV 和材质关联。

#### Scenario: Import a textured scene hierarchy

- **WHEN** 选定的场景包含带旋转的父节点、带平移的子节点和使用同一 Base Color PNG 的不透明网格
- **THEN** 对应实体保留层级和等价变换，网格位置、绕序、法线和 UV 正确，材质解析到同一个纹理资产 ID，生成的场景及引用通过无窗口校验

#### Scenario: Preserve several primitives and instances

- **WHEN** 一个两 primitive、两材质的网格被两个节点引用
- **THEN** 每个节点下的两次绘制各保留自己的材质和可编辑实体 ID，两个节点复用相应的网格资产 ID

#### Scenario: Report material approximation and reject unsupported semantics

- **WHEN** 受支持的模型带法线贴图或金属粗糙度通道，或候选依赖必需扩展、压缩数据、动画、透明材质或域外 URI
- **THEN** 前者导入 Base Color 并报告采用现有不透明着色和采样规则，后者以非零状态退出并指出相关文件、元素或路径

### Requirement: Bind reimport identity through a versioned manifest

导入清单 SHALL 分别保存来源选择器及持久身份。已有逻辑键的资产 UUID、类型和虚拟路径，以及实体 UUID，SHALL 在重导入时得到保留。发生源文档变更时，工具 SHALL 要求人或脚本明确确认当前来源选择器。身份冲突、缺失绑定和隐式移除 SHALL 失败。

#### Scenario: Reexport with reordered source arrays

- **WHEN** 编辑后的 `.gltf` 源文档重排了节点或材质索引，操作者先沿用旧清单，再更新来源选择器并确认新文档版本
- **THEN** 首次请求被拒绝，显式重新绑定后仍得到原有的资产 UUID、虚拟路径及实体 UUID，已保存的场景引用在新产物上仍能解析

#### Scenario: Reproduce an unchanged import

- **WHEN** 用相同输入、清单和工具版本导入两次
- **THEN** 有效资产文件、身份映射和生成的 Scene JSON 字节相同

#### Scenario: Reject destructive or ambiguous mappings

- **WHEN** 清单出现重复 ID、重复来源绑定、缺失映射、已有逻辑键改类型或路径，或者移除已发布的逻辑键
- **THEN** 工具拒绝发布并定位冲突逻辑键或来源选择器，既有引用仍有效

### Requirement: Validate and publish a complete reimport

初次导入与重导入 SHALL 在整批资产及生成场景校验完成后一次发布。已发布的批次 SHALL 可独立读取；导入、校验或发布失败 MUST 保留当前批次及作者 Scene JSON。无写入检查 SHALL 复用导入的源数据、映射及转换校验，并给出结构化诊断和相应退出状态。

#### Scenario: Reject a failed candidate and retry

- **WHEN** 更新包的外置文件截断、候选写入中断、场景引用不匹配或最终发布失败，然后修正问题重试
- **THEN** 失败后当前入口及旧产物字节不变，仍可打开旧场景；重试成功后会得到可单独打开的新批次，旧批次仍可继续使用

#### Scenario: Check an automated edit without publication

- **WHEN** AI 修改导入清单或源输入后调用无写入检查
- **THEN** 工具给出单个 JSON 结果和匹配的退出码，按可用信息指出源码路径、逻辑键或 glTF 元素，且当前产物、源输入和作者场景字节不变

#### Scenario: Reject a competing publication from a stale baseline

- **WHEN** 两个导入操作从同一已发布批次开始，且其中一个操作先发布新候选
- **THEN** 后发布的操作报告基线过期，先发布的完整批次保持可读，后发布的操作可以重新读取基线并重试

### Requirement: Preview imported assets with authored scene edits on Win64

Win64 验收 SHALL 从一个真实 glTF 模型的已发布批次运行 Game，呈现几何、Base Color 和方向光阴影。使用独立的作者 Scene JSON，AI SHALL 能按持久 UUID 改动模型节点，通过 `SceneTool check --json`，并在同一进程按 F5 看到修改后的画面。

#### Scenario: Edit an imported node and recover from an invalid edit

- **WHEN** AI 修改作者场景中一个导入节点的局部变换，然后试写一个无效的父实体引用，再修复并重试
- **THEN** 合法修改通过无窗口检查并在 F5 后呈现；非法修改带实体定位被拒绝，旧画面和漫游机位继续可用，修复后 F5 成功，检查和预览均未回写场景

#### Scenario: Load a newly imported batch with an existing authored scene

- **WHEN** 新批次复用了原资产身份并修改了 Base Color PNG，使用原作者场景重新启动 Game 并选择新批次
- **THEN** 画面使用新纹理，场景中的作者变换和引用仍然有效，另一个已启动并挂载旧批次的进程可继续使用旧纹理
