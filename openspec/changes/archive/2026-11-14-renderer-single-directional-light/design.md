# Design

## Context

`AssetImport` 当前解码受限三角 OBJ，mesh v1 包含位置和索引，mesh v2 加入 UV。Scene v1 按实体 UUID 保存原始组件数据，`SceneTool check` 无窗口校验文档和资源依赖。WorldRenderer 已从提交后的 World 提取绘制与相机，`FRenderScene::Lights` 预留了灯光代理。

BasePass 当前使用位置、顶点颜色和 UV，逐对象 push constants 为 80 B。已有 RHI 的单段 push constants 上限为 128 B。示例按两个受 fence 管理的帧槽提交。 [Khronos Vulkan limits](https://docs.vulkan.org/spec/latest/chapters/limits.html#limits-minmax)

## Goals / Non-Goals

让显式法线和场景灯光在原有不透明管线中得到可验证的结果，保持旧输入、旧场景、纹理绑定和 M2 会话相机的兼容。光照范围为一盏方向光的直接漫反射。阴影、多个启用光源、环境光、HDR 管线、PBR 和自动法线生成留给后续工作。

## Decisions

### 显式法线和 mesh v3

- 继续支持有限 float32 `v`、`vt` 和三角面，并新增有限非零 `vn x y z`。面格式为 `v`、`v/vt`、`v//vn` 或 `v/vt/vn` 四种之一。同一网格只用一种格式，索引沿用现有正数和上限规则。包含 `vn` 的网格须在每个面角引用法线。保留 OBJ 绕序及现有 UV 原点转换。
- 法线用 double 算长度并归一化，存为 float32。导入按面角首次出现顺序展开 `(位置, UV 或 0, 法线或 0)`，保留不同的索引组合。旧格式沿用原路径，输出字节、身份与逻辑路径保持兼容。选择显式法线是为了让作者控制接缝和表面朝向，AI 也可通过 OBJ 和导入日志核对结果。
- 保留现有 LEAS 容器。mesh v3 小端 payload 依次是 `vertexCount:u32`、`indexCount:u32`、`flags:u32`、位置 `float32[3] × N`、可选 UV `float32[2] × N`、法线 `float32[3] × N` 和 `u32[M]` 索引。标志 bit 0 表示 UV，其余位必须为零。法线平面始终完整，每项有限且 `abs(dot(n,n)-1) <= 1e-4`。v3 仅接受 Mesh 类型。延用现有尺寸、索引、AABB、缓冲区溢出及精确 payload 长度检查。
- `FMeshAssetData` 增加可空的法线数组，v1/v2 解码结果为空。同步 AssetLoading、SceneTool 和实际加载都用同一份 typed decode。Renderer 构造加入 Normal 属性的共同顶点布局，旧数据填零并明确标记无作者法线。构造失败延用现有清理和发布规则。

### Scene v2 存作者值

`directionalLight` 附在实体上。在顶层 `version: 2` 的场景中，把下列实体加入 `entities`。纯灯光实体只需 `id` 和该组件。

```json
{
  "id": "8a0a5d74-17ef-48e0-a46b-f7105518e707",
  "directionalLight": {
    "direction": [0, 0, -1],
    "color": [1, 0.5, 0.25],
    "intensity": 0.5,
    "active": true
  }
}
```

上例的光线沿世界 −Z 传播，因此 +Z 法线朝光。方向长度可由作者保留，提取时归一化并取反作为朝光方向。灯光位置、实体 TRS 和父层级不参与方向。颜色用 0 到 1 的线性 RGB。强度为 0 到 16 的无单位系数，0 是有效值。场景可包含若干停用灯，所有灯字段均接受完整校验，第二盏启用灯报错并标出其实体 UUID 与 `active` 字段。

SceneDocument 共用解析和文档校验。顶层字段可乱序，因此读完整个文档后再以实际版本验证灯组件并保留字段位置。v1 继续严格读取原字段，v2 接受可选灯。有任一灯组件的场景保存为 v2，即使全部停用。其余场景按原来的 v1 规范形式保存。Scene 继续按 UUID 顺序输出原始作者字段并原子替换文件。SceneTool 沿用命令 `SceneTool check SCENE.json MOUNT ABSOLUTE_ASSET_ROOT`，在 CPU 上校验文档及引用网格的 v3 数据。有效文档通过后，Game 的候选快照再检查受光网格的世界法线矩阵。

### World 与 WorldRenderer 的职责

World 持有逻辑方向光组件。把它纳入现有 World 初始化、实体销毁、Reset 和组件事务路径，Scene 的 Restore/Capture 按 UUID 恢复与收集。World 类型无 Renderer 或 RHI 依赖。

WorldRenderer 从提交后的 World 验证并复制唯一启用灯，复用 `FRenderScene::Lights`，生成带身份、单位世界朝光方向与线性 RGB 强度的帧级代理。WorldRenderer 也检测绕过 Scene 文件创建的非法或重复启用灯。方向不依赖当前主相机或会话相机。

对于本帧应受光的网格，在剔除前用模型 3×3 线性部分计算 `N = inverse(transpose(M))`，用 double 和相对列尺度检查退化并保留行列式符号。正常路径可把矩阵乘一个公共正系数再转成 float，片元归一化会消去该比例。不可逆、数值退化（`abs(det) <= 1e-8 × 三列长度之积`）或非有限结果使快照失败，并报告实体及网格。无需光照的旧网格与旧场景继续执行原有变换检查。Game 用现有候选快照预检拒绝不可发布场景，保留已有的场景和漫游机位。

### BasePass 与 GPU 布局

| 数据 | 所有权和使用 |
| --- | --- |
| 顶点 Position、Color、UV、Normal | Renderer 资产构造；VS 增加 Normal attribute，PS 收到插值后的世界法线。 |
| 128 B 对象 push constants | 原 64 B LocalToClip、16 B BaseColor（RGB 沿用，W 用作网格法线存在标志）、三个 16 B 法线矩阵行。VS 和 PS 使用一致偏移，仍整段写入现有 RHI。 |
| 32 B 帧灯光常量 | 独立 uniform buffer，两个 `float4` 记录朝光方向与启用标志、线性 RGB 强度。BasePass 用 set 1 binding 0，材质纹理和采样器继续用 set 0。 |

灯光常量按现有 `FRenderFrameScope::GetSlotIndex()` 分配两个 slot-local buffer 和绑定组。该槽的 fence 完成后，在录制前更新本槽内容，所有绘制消费同一灯光值。即便没有灯也绑定并写入明确的停用值，避免跨帧污染。使用现有 UniformBuffer、Upload 和绑定组接口，RFG 声明像素阶段的 ConstantBuffer 读取。缓冲与 resize 后的交换链附件生命周期独立，退出时等帧完成再释放。创建或映射失败须带诊断并走现有失败路径。将灯光字段追加到对象常量会超过现有 RHI 的 128 B 上限，分开存放也便于观察帧槽隔离。 [Khronos Vulkan limits](https://docs.vulkan.org/spec/latest/chapters/limits.html#limits-minmax)

VS 变换带法线网格的法线，PS 插值后归一化，退化插值贡献零。光照公式如下，`baseLinear` 沿用当前纹理 sRGB 解码与材质、顶点颜色相乘的结果，`L` 指向光源。

```text
factor = lightColorLinear * intensity * max(dot(normalize(worldNormal), L), 0)
rgb = saturate(baseLinear * factor)
```

输出由现有 sRGB 目标编码，alpha 为 1。对受光路径，先把纹理、顶点颜色和灯光合成非负倍率。当倍率大于 1 时，先将材质色限制到 `1 / 倍率` 再乘，保持最终钳制结果并避免有限 HDR 材质乘法溢出。其余通道直接相乘。无启用灯或无顶点法线时使用原有公式，保持旧截图基准。正反面使用插值的作者法线。相机移动不改变光照方向。

## Risks / Trade-offs

- Scene v2 和 mesh v3 对旧版可执行程序是新格式。新工具继续读取 v1/v2 网格和 Scene v1，默认贴图样例保留旧文件，光照样例单独暂存。
- sRGB 图片视觉上容易把颜色空间误差和灯光强度混淆。用已知纹理中间色、独立无纹理面和计算出的参考像素定位差异。
- 法线方向、非均匀变换和镜像易混淆。固定相机，通过可区分逆转置与普通模型矩阵的样例验证数值，再通过 M2 漫游验证同一表面照明稳定。
- 帧常量跨槽误用可能只表现为偶发上一帧颜色。以交替灯值检查连续提交，验证无逐帧设备等待，保留 resize 和重启回归。

## Migration Plan

保留已有默认场景及旧 cooked 数据。独立光照样例含 OBJ、材质、PPM、Scene JSON 和暂存清单，输出到独立内容目录，Game 通过现有 `LIMITLESS_DEMO_CONTENT` 指向它。作者可按需要用原资产身份重新导入含法线的 OBJ。移除全部灯光组件后，场景经过保存可恢复为 v1 规范形式。计划按资产编解码、场景校验与保存、快照及着色、实机验收的顺序实施。
