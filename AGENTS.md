# Limitless Engine 开发约定

- 新增或修改引擎 C++ 调用时，通过引擎接口使用 C++ 标准库和第三方 C++ 库。优先复用已有接口，缺少通用能力时补到 Core 对应模块，专用依赖留在所属模块的适配层。数学工具沿用 `LE::Math` 命名空间，例如用 `LE::Math::Max` 封装 `std::max`。
- 标准库实现和第三方互操作的边界遵循 [ADR-0001 第 10 节](Docs/ADR/0001-modern-cpp-core-types-and-containers.md#10-standard-library-使用边界)。
- 使用 OpenSpec 开展提案、规格、任务或实现工作时，遵循 [OpenSpec 项目配置](openspec/config.yaml) 中的项目约束。
