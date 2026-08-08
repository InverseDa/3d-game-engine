# Application 与 FEngineLoop 生命周期

## 职责边界

`Application` 模块提供运行时生命周期 kernel，不包含窗口后端、RAL backend 或 demo
资源。公共接口包含 `IApplication`、`FEngineLoop`、按值传递的帧上下文，以及
`FRuntimeModuleRegistry`。
`FEngineLoop` 只借用 `IApplication`；它会调用生命周期函数，但不会销毁对象。

当前 RFG composite triangle 是第一个真实 consumer，位于独立的
`DemoApplication` 模块。其工厂返回带 creator-side destroy thunk 的
`FApplicationPtr`，因此 Editor 构建中对象始终回到 `DemoApplication.dll` 销毁。

```text
Launch (entry/bootstrap)
    -> owns FApplicationPtr
    -> FEngineLoop borrows IApplication
         -> IApplication::RegisterModules
         -> FRuntimeModuleRegistry::StartupAll
         -> IApplication::Initialize
         -> Tick: ProcessPlatformEvents -> Update -> Render
         -> IApplication::Shutdown
         -> FRuntimeModuleRegistry::ShutdownAll

DemoApplication -> Platform / RAL / RFG / Renderer / World
Application     -> Core / PlatformTime implementation
```

## Runtime module 注册与所有权

Runtime module 通过显式 `FRuntimeModuleDescriptor` 注册，不使用 static initializer、全局
registrar 或 DLL 扫描。descriptor 的 `Name` 和 `Dependencies` 是只在 `RegisterModule` 调用
期间借用的 view；registry 立即将其深拷贝为自有 `LE::String`。模块名是稳定的人类可读
identifier：非空、首字符为 ASCII letter/digit，其余字符只能为 ASCII letter/digit 或
`.`、`_`、`-`；控制字符、空白和嵌入 NUL 均不合法。

`Context` 可为 null，以支持 stateless thunk。非空 context 和 Startup/Shutdown 函数指针
由 provider 借出，必须活到 registry 完成 Shutdown；在 Editor DLL 图中，thunk 会回到
provider DLL 执行。`FRuntimeModuleRegistry` 使用 Application.dll 内创建和销毁的 PImpl，
析构会兜底调用幂等的 `ShutdownAll`。当前不支持运行中注册、移除、重启、DLL unload 或
hot reload。

`RegisterModules` 是无副作用描述阶段，禁止在其中创建窗口、设备或其他 runtime 资源。
完整注册后，Startup 前先验证 invalid/duplicate、missing dependency 和 cycle，因此这些
错误保证零 Startup callback。拓扑排序不依赖 HashMap iteration 或注册顺序：每个 ready
set 总是选择 lexicographically smallest module name。若某 module Startup 返回 false，
registry 先调用该失败 module 的 Shutdown 清理 partial state，再对已成功 module 严格
逆序 rollback。正常 Shutdown 也严格逆序且幂等；重复 Startup 和 Running 后 Register
返回状态错误。

首个 registration failure 会锁存（即使之后仍有成功注册），详细失败名称由 registry
自有诊断字符串保存；getter 返回的 `StringView` 在 registry 析构前有效。

## EngineLoop 状态机

- 新对象从 `Uninitialized` 开始，只能成功初始化一次。
- `Initialize` 成功后进入 `Running`。
- module 必须全部成功启动后才调用 Application Initialize。
- invalid registration、dependency validation 和 module Startup failure 分别映射为粗粒度
  EngineLoop initialize result；详细 missing/cycle/failing module 可查询 registry。
- module failure 不会调用尚未进入生命周期的 Application Shutdown；失败 module 自清理并
  rollback 已成功 module，随后不可 Tick。
- Application 初始化失败会先调用一次 `Application::Shutdown` 清理部分状态，再逆序关闭
  module，返回 `ApplicationInitializeFailed` 并进入 `ExitRequested`。
- 任一帧阶段返回 `Exit` 时，剩余阶段被短路并进入 `ExitRequested`。
- `Shutdown` 可在任意初始化结果后调用且幂等；Application 最多收到一次 Shutdown，且任何
  已启动 module 都在 Application 之后关闭。
- `FEngineLoop` 不拥有 Application，Application owner 必须让对象活到 loop Shutdown 完成。
- 进入 `ExitRequested` 或 `Shutdown` 后再次 Tick 直接返回 Exit，不采样时钟，也不回调
  Application。

## 帧上下文

每个被接受的顶层 `FEngineLoop::Tick` 只采样一次时钟，并依次发送三个显式阶段：

1. `ProcessPlatformEvents`
2. `Update`
3. `Render`

三个阶段共享同一个 `FrameIndex`、`DeltaSeconds` 与 `ElapsedSeconds`。首帧 index 为 0，
Delta 和 Elapsed 均为 0。每次实际接受的 Tick attempt 消耗一个 frame index；即使某个
阶段请求 Exit，该 index 也会递增一次。时钟倒退被 clamp 为零，不会倒退或重复累计
Elapsed。

生产环境使用 `FPlatformTime::Seconds`。测试可通过无所有权函数指针注入 clock；回调必须
在 loop Shutdown 前保持有效。

## 当前 demo module 映射与限制

- `Demo.Window`：真实 Startup 创建 Platform window；负责消息泵和关闭请求；Shutdown 释放
  window 和 event queue。
- `Demo.Render`：依赖 `Demo.Window`；真实 Startup 创建 RAL/swapchain/shader/pipeline/
  renderer 与两槽 `FRenderFrameScheduler`。每槽拥有 allocator、command list、acquire/
  render-finished semaphore、initially-signaled fence 和 deferred releases。Update 负责 minimize
  与显式 resize/retry；Render 驱动两槽 Acquire/record/Submit/Present 并处理
  Suboptimal/OutOfDate/Error。
- 正常成功帧不再调用 `WaitIdle`。resize 先等待实际 InFlight 槽，swapchain 底层重建和
  terminal shutdown 仍允许 exceptional idle。Renderer 将 RFG-owned texture/buffer transient
  的 ownership 转交给当前 frame slot；它们只在该槽 fence 完成后、allocator reset 前回收。
  imported resources 始终保持外部所有权。
- 所有 RAL 对象通过 `RAL::DestroyResource` 回到 RAL 模块走 virtual destructor；上层 DLL
  不直接 delete backend 分配的对象。

自动化 C++ 测试覆盖确定性 branched DAG、深拷贝 descriptor、invalid/duplicate/missing/
cycle 零启动、失败 module partial cleanup、严格 rollback/逆序关闭、重复调用状态、
EngineLoop 与 module/application 的相对顺序、各阶段短路、失败后零额外 Tick/clock、
  frame/time 契约、倒退时钟、null RAL destroy contract，以及显式帧同步的状态分支、调用顺序、
  精确 wait/signal/fence 传递和 RFG transient 的 frame-slot 回收顺序。
