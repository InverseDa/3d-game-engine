# Tasks

## 1. Editor asset generation refresh

- [x] 1.1 接入 F6 按键边沿和发布指针解析，覆盖重复按键、错误指针、失效目录以及基本的 Game 构建兼容性。
- [x] 1.2 实现隔离候选 Catalog、加载会话和 World，帧边界切换与稳定相机身份重绑定。验证失败、重复代与 F5 路径，提供机器可读结果和启动文档。

## 2. Win64 acceptance

- [x] 2.1 构建 Win64 Editor、Game 和 Test，运行输入、WorldRenderer 和现有 Builder/资产集成回归。用实际 Lantern 预览验证同一进程呈现新贴图，F6 保持机位，F5 应用作者机位。
- [x] 2.2 制造失败候选，再切换有效代。核对结构化拒绝和呈现日志、失败画面稳定、重复代不重复上传、作者 JSON 哈希及 Vulkan 验证，保留可复查的日志和画面。
