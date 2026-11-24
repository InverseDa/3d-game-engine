# Design

## Context

`ImportGltf` 通过原子替换 `current.json` 发布独立资产目录。当前 Demo 在启动时挂载一个 Catalog，加载会话借用该 Catalog。F5 构造候选 World 后会重置作者机位。Editor 和 Game 共用 Demo 入口，Builder 已提供 `WITH_EDITOR` 构建标记。

## Goals / Non-Goals

让同一个 Editor 窗口手动预览 Lantern 的不同已发布资产代，沿用库、Scene 预检和 Renderer。工作范围是完整资产代切换。文件监控、Shader 动态编译、按资产更新和异步上传另行安排。

## Decisions

1. **入口。** Win64 继续使用现有 Editor 可执行文件。通过 `LIMITLESS_EDITOR_CONTENT` 指定带 `current.json` 的绝对发布根，并用 `LIMITLESS_DEMO_SCENE` 指定独立的绝对作者场景路径。Editor 读这些配置后解析初始发布代。F6 在 Editor 专用路径刷新资产，F5 沿用场景重载语义。两种刷新请求同帧发生时优先处理 F6。
2. **解析。** 按已有发布协议读有界 UTF-8 小文件，解析规范的小写非空 UUID。用 FileSystem 检查候选目录的路径及发布标记，再由 Catalog 验证 `Assets`。预览只读发布根与作者文件。已活动的 UUID 可直接返回 `unchanged`。
3. **所有权。** 每个代的内容状态共同持有 Catalog、借用它的加载会话，以及使用其句柄的 World。候选状态独立构造，沿用既有 Renderer 资产 provider 和场景预检。完成候选后等待现有在途帧再发布，按 World、会话、Catalog 的顺序释放旧版本。失败时析构候选，活动版本保持可用。
4. **相机。** 用 Scene 持久实体 UUID 在新旧 World 间比较主相机。主相机身份相同且具备活动漫游状态时，保留会话姿态并绑定候选的运行时相机。主相机变更时从候选作者机位开始。F5 继续以磁盘作者机位开始。
5. **诊断。** 用带 `Editor content refresh ` 前缀的单行 JSON 报告 `status`、`code`、`active`、`candidate` 和 `navigation`。`rejected` 和 `unchanged` 当次给出，`presented` 等到新内容实际提交呈现后给出。尚未呈现的切换保留自己的呈现记录，期间更新的候选返回 `refresh_pending`。现有详细日志保留文件和失败阶段。

## Risks / Trade-offs

全量预检和同步 GPU 上传会阻塞本帧。以 Lantern 作为首版样例，后续可基于实际刷新耗时决定是否拆分上传。切换前相机覆盖由持久 ID 映射到新 World，这保证了独立重建后仍能使用正确的临时姿态。
