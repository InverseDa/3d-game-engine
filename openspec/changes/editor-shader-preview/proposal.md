# Proposal

## Why

Win64 Editor 已能在一个窗口里预览重导入的资产。修改 BasePass Shader 仍要重编打包产物并重启，着色效果的反馈不在同一条编辑链路上。

## What Changes

- 开发模式下在 Win64 Editor 按 F7，后台编译当前 `BasePassShadowPS.lsf` 的 `MainPS`，包含它的递归 include。编译期间继续使用已启用的管线。
- 校验候选 SPIR-V 与当前 BasePass 的资源绑定、常量布局和阶段接口。候选管线通过校验并构建成功后，在帧边界替换，成功报告以实际出图为准。
- 对编译失败、接口不兼容及候选管线创建失败，保留上一版可用画面和机位。报告请求 ID、错误码，以及编译器提供的源码位置。
- 用 Lantern 验收同一窗口换色、错误后恢复、F5/F6 联动，以及 Game 离线包的独立运行。

## Capabilities

### New Capabilities

- `editor-shader-preview` — Editor 内手动编译、验证和试用兼容的 BasePass 像素 Shader。

### Modified Capabilities

## Impact

Builder 继续负责离线产物，并提供可复用的编译入口。Editor 管理预览任务和诊断，Renderer 管理 GPU 管线切换。首版使用会话级预览目录，范围为固定的 Win64 Vulkan BasePass 像素阶段。文件监听、其他 Shader 阶段和资源布局升级另行安排。
