# Platform 模块

## 边界与依赖

Platform 提供 Runtime 的操作系统边界，并且只公开引擎自有类型。公共头文件不会
暴露 `HWND`、`NSWindow`、`NSView` 或 `CAMetalLayer` 类型。

```text
RHI ------> Platform ------> Core
Launch ---> Platform
```

Platform 定义 `FPlatformSurface`，RHI 消费它来创建图形 surface。这样 Platform
无需了解 RHI，依赖方向保持单向且无环。

## 窗口与所有权

`CreatePlatformWindow(FPlatformWindowDesc)` 返回
`UniquePtr<FPlatformWindow>`。native 初始化或分配失败时返回空指针；半初始化对象
会在 Platform 内清理。UniquePtr 的 destroy thunk 也定义在 Platform 模块中，确保
对象跨 DLL 边界时仍由创建它的模块执行销毁。

`FPlatformWindow` 提供：

- 稳定的进程内 `FPlatformWindowId`；
- `IsValid()` 与 `IsCloseRequested()`；
- `GetSurface()`、`GetSize()`、`IsMinimized()`；
- `PumpEvents(FPlatformEventQueue&)`。

Windows 使用 `CreateWindowEx` 的 `lpParam` 和 `GWLP_USERDATA` 将 WndProc 绑定到实例，
没有动态初始化的全局窗口表。macOS 的 AppKit delegate 和 Metal layer 均留在私有
Objective-C++ 实现中。

## 事件契约

`FPlatformEventQueue` 是 FIFO value queue。当前事件类型为：

- `CloseRequested`
- `Resized`
- `Minimized`
- `Restored`

事件携带稳定 `WindowId` 和事件发生时的窗口尺寸，不携带借用的 window pointer，
因此窗口销毁后读取已经转移到调用方队列的事件不会造成悬空引用。窗口析构会清空
仍留在实现内部的 pending queue。

调用方通过 `FPlatformWindowDesc::bEnableInput` 为 Win64 Demo 窗口启用输入。同一个事件队列还会报告 `KeyPressed`、`KeyReleased`、`MouseCaptureStarted`、`MouseCaptureEnded`、`MouseMoved` 和 `InputCanceled`。键盘值为引擎的 W/A/S/D，重复按下消息携带 `bRepeat`，鼠标移动携带有符号的相对位移。队列按消息顺序读取，批量移动与键盘、捕获及取消事件保持原有顺序。

每次消息泵开始前调用 `FPlatformWindowInput::BeginFrame()`，再按顺序用 `Apply()` 处理本窗口的事件。`GetFrame()` 提供当帧值。按住与捕获状态跨帧保留，按下及松开边沿与鼠标累计位移在新帧归零。同帧按下再松开会报告两个边沿，最终按住状态为松开。重复按下不会增加边沿，也不会在取消后重新开始移动。

Win64 后端在窗口有焦点、客户区有效时用右键申请鼠标捕获。前台 Raw Input 提供相对移动，光标到达屏幕边缘后仍可转向。常规右键松开结束捕获，保留本帧已接收的相对位移与键盘状态。失焦、最小化、零尺寸、系统取消、意外丢失捕获和关闭会清除按住状态及未消费位移，恢复后等待新的按下。相对输入注册或捕获失败时记录 Platform 日志，键盘仍可使用。macOS 继续发出窗口生命周期事件。

## 时间契约

`FPlatformTime::Seconds()` 使用 steady clock，返回值的 epoch 未指定，只能用于计算
时间差，并且不受系统墙钟调整影响。`SleepForMilliseconds()` 是当前 demo 在最小化
或零尺寸窗口时的节流入口。

## 平台构建

`Platform/Build.ts` 保证每个平台只编译对应实现：

- Win64：`Private/Windows/WindowsPlatformWindow.cpp`
- macOS：`Private/Mac/MacPlatformWindow.mm`，并链接 AppKit 与 QuartzCore

公共事件队列、surface 和 time contract 可由 `LimitlessTests` 在不创建 Vulkan
设备或 native 窗口的情况下验证。
