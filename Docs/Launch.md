# Launch 模块

## 职责

Launch 现在只负责最终 OS 入口和通用 bootstrap：

- `Launch.cpp` 提供 Windows `WinMain` 或其他平台的 `main`。
- 初始化/关闭 Core 日志设施。
- 从 `DemoApplication` 工厂取得带 creator-side deleter 的 Application owner。
- 初始化并驱动 `FEngineLoop`，最后显式 Shutdown。

Launch 不再包含窗口实现、平台消息循环、shader compiler、RAL/RFG/Renderer/World demo
资源或平台分叉的主循环。

```text
WinMain / main
    -> GuardedMain
    -> LE_INIT
    -> CreateDemoApplication
    -> FEngineLoop::Initialize
       -> RegisterModules / dependency validation / StartupAll
       -> IApplication::Initialize
    -> while FEngineLoop::Tick == Continue
    -> FEngineLoop::Shutdown
    -> destroy FApplicationPtr in DemoApplication
    -> LE_SHUTDOWN
```

## 依赖

```text
Launch -> Core
       -> DemoApplication -> Application
                          -> Platform
                          -> RAL / RFG / Renderer / World
```

Launch 对 Application 只有 bootstrap 层的依赖，不包含上述实现类型。Application 生命周期、
帧阶段、时间与所有权契约见 [Application.md](Application.md)。

## 当前限制

- 运行产品仍使用当前 RFG composite triangle 作为 demo Application。
- shader runtime compiler 仍依赖外部 `glslangValidator`，但实现已归属
  `DemoApplication/Private`。
- 输入事件尚未扩展到键鼠等完整集合。
- demo Shutdown 和 resize 路径仍有 `WaitIdle`；Phase 3 才处理显式 frames-in-flight。
- runtime module 当前不支持运行中增删、重启、DLL unload 或 hot reload。
