# Editor Shader Preview

## Purpose

让开发者和 AI 修改文本 Shader 后，在同一个 Win64 Editor 窗口里检查着色结果。编译、接口校验和管线切换各有可追溯的反馈，失败时可继续使用此前生效的 Shader。

## ADDED Requirements

### Requirement: 手动编译 BasePass 像素 Shader

已配置 Shader 源码的 Win64 Vulkan Editor SHALL 在 F7 按下时后台编译 `BasePassShadowPS.lsf` 的 `MainPS` 及其递归 include。编译期间 Editor SHALL 继续处理输入并使用当前管线绘制。

#### Scenario: 修改间接 include 后预览

- **WHEN** 开发者或 AI 修改 `MainPS` 使用的间接 include 并按 F7
- **THEN** 本次请求使用更新后的 include，Editor 可在编译期间继续出图

#### Scenario: 同一请求尚未结束

- **WHEN** 编译、校验或首次出图仍在等待时再次按 F7
- **THEN** Editor 报告当前请求忙碌，并且只处理已接受的请求

#### Scenario: 请求的源码版本已过时

- **WHEN** 源码或 include 在请求采集期间变化，或在编译结束时已与请求使用的版本不同
- **THEN** Editor 拒绝该请求并报告源码已变化，下一次 F7 可使用最新文件

### Requirement: 兼容管线的切换与出图回执

Editor SHALL 仅接受与启动时的 BasePass 像素阶段接口兼容的候选 Shader。候选管线创建成功且旧管线的在途帧完成后，Renderer SHALL 在帧边界切换。Editor SHALL 在使用该版本的帧首次成功 Present 后记录请求成功。

#### Scenario: 同一窗口应用有效着色修改

- **WHEN** 开发者或 AI 只修改 Lantern 的像素着色表达式，并且候选管线创建成功
- **THEN** 同一窗口显示修改后的着色，当前相机、场景和 GPU 资产保持原有状态，日志给出已出图的 Shader 请求 ID

#### Scenario: 切换期间窗口最小化或交换链重建

- **WHEN** 合法候选已完成，但窗口暂时无法 Present
- **THEN** Editor 保留出图待确认状态，直到该版本首次成功 Present

### Requirement: 请求失败时保留已启用的 Shader

工具不可用、编译失败、超时、候选无效、ABI 不兼容或候选管线创建失败时，Editor SHALL 保留已启用的管线，报告请求 ID 和可区分的失败原因。编译器提供源码位置时，诊断 SHALL 定位到原始源码或 include。

#### Scenario: 从语法错误恢复

- **WHEN** 间接 include 出现语法错误，随后被修复并重新发起 F7 请求
- **THEN** 首次请求报告原始 include 路径和行号，期间继续显示上一可用版本，第二次请求可以正常出图

#### Scenario: 编译成功但资源接口发生变化

- **WHEN** 源码改动了正在使用的资源 binding、常量布局或阶段接口，且仍可编译为 SPIR-V
- **THEN** Editor 报告接口不兼容并保留当前效果，候选字节码不会进入管线创建

#### Scenario: 候选管线创建失败

- **WHEN** 候选已通过校验但 Renderer 无法创建管线
- **THEN** Editor 报告管线创建失败，继续使用此前的 Shader 和管线绘制

### Requirement: 预览与场景、资产和打包流程独立

Shader 预览 SHALL 仅在当前 Editor 会话内生效，预览产物使用独立目录。F5 和 F6 SHALL 各自沿用现有场景加载与资产刷新行为，且沿用当前 Shader 版本。Game 和 Editor SHALL 能从打包的离线 Shader 启动。

#### Scenario: 预览成功后执行 F5 和 F6

- **WHEN** 新 Shader 已出图，用户通过 F5 加载 AI 修改且通过 SceneTool 校验的 Scene JSON，并通过 F6 刷新兼容的资产版本
- **THEN** 场景改动和新资产都能出图，Shader 效果随之保留，预览未写回场景文件或离线字节码

#### Scenario: 从独立目录启动离线包

- **WHEN** 关闭预览 Editor 后，在缺少源码和开发工具的环境中运行打包的 Game 或 Editor
- **THEN** 应用可使用原离线 Shader 出图，打包 Shader 文件的散列值与预览前一致
