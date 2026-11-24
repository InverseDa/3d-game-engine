# Editor Content Preview Delta

## Purpose

让人和 AI 在同一个 Win64 Editor 预览进程中验证已经离线发布的资产更新，并在预检失败时继续使用上一次已呈现的画面与临时漫游状态。

## ADDED Requirements

### Requirement: Refresh a published asset generation in Win64 Editor

Win64 Editor SHALL 使用配置的发布根和独立作者 Scene JSON 打开已发布资产代。窗口可交互时，F6 的新按下事件 SHALL 查询 `current.json`，预检该代资产及同一作者场景，并在候选就绪且旧提交帧安全完成后切换。F5 SHALL 只从当前资产代重新加载作者场景。Game SHALL 继续通过原入口运行。

#### Scenario: Refresh an imported texture and preserve the preview camera

- **WHEN** 已预览 Lantern，导入工具发布使用原 UUID 的不同贴图，且作者 Scene JSON 中同一个主相机的位置已被修改
- **THEN** F6 后同一进程显示新贴图并保留当前预览机位，作者 JSON 字节不变；随后 F5 使用新资产代，并采用文件中的作者机位

#### Scenario: Refresh when the current generation is already active

- **WHEN** `current.json` 仍指向此 Editor 已挂载的版本
- **THEN** F6 返回 `unchanged`，不重建或上传资产，也不重新读取作者场景或重置预览机位

### Requirement: Preserve the active preview when an asset refresh fails

如果指针或所指目录无效，或者候选资产、场景校验、GPU 资源准备失败，Editor SHALL 保留之前已显示的版本、画面和漫游机位。每次刷新 SHALL 记录机器可读的状态、稳定诊断代码、当前代及已知候选代。成功呈现后 SHALL 可通过报告确认实际出图。

#### Scenario: Reject a bad candidate and refresh after correction

- **WHEN** 在 Editor 运行时使候选贴图缺失，或提供有无效引用的作者场景，再修复并请求 F6
- **THEN** 失败报告指出拒绝原因和可得的代 ID，上一版画面和视角继续可用；修复后同一进程能呈现新代，原作者文件始终保留校验前字节
