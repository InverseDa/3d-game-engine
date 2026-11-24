# Static glTF Import Delta

## ADDED Requirements

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
