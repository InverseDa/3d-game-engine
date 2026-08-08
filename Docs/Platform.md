# Platform 模块

## 边界与依赖

Platform 提供 Runtime 的操作系统边界，并且只公开引擎自有类型。公共头文件不会
暴露 `HWND`、`NSWindow`、`NSView` 或 `CAMetalLayer` 类型。

```text
RAL ------> Platform ------> Core
Launch ---> Platform
```

Platform 定义 `FPlatformSurface`，RAL 消费它来创建图形 surface。这样 Platform
无需了解 RAL，依赖方向保持单向且无环。

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

输入设备事件尚不属于 Task 4，将在后续 Input/UI 阶段加入独立契约。

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
