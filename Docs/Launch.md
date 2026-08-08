# Launch 模块设计文档

## 1. 概述

Launch 模块是 Limitless Engine 的**程序入口与平台适配层**，承担以下三类职责：

| 职责 | 说明 |
|------|------|
| 平台入口 | 提供 `WinMain` / `main` 入口，屏蔽操作系统差异，统一转入 `GuardedMain()`。 |
| 窗口系统 | 提供跨平台窗口抽象 `FPlatformWindow`，封装 Win32 `HWND` 与 macOS `NSWindow`。 |
| 启动流程 | 在 `GuardedMain()` 中完成日志初始化 → 窗口创建 → RAL 设备初始化 → 交换链创建 → 渲染主循环 → 资源清理的全链路。 |

---

## 2. 平台支持

| 平台 | 入口文件 | 窗口实现 | 构建系统处理 |
|------|----------|----------|--------------|
| Win64 | `Launch.cpp` (WinMain) | `WindowsWindow.h` / `WindowsWindow.cpp` | 默认包含，macOS 时排除 |
| macOS | `Launch.cpp` (main) | `MacWindow.h` / `MacWindow.mm` | 默认排除，仅 macOS 时包含 |

macOS 构建额外链接系统框架：`AppKit`、`QuartzCore`（`CAMetalLayer` 所需）。

---

## 3. 启动流程

```
操作系统入口
    ├─ Win64 : WinMain()       [Launch.cpp]
    └─ macOS : main()          [Launch.cpp]
           │
           ▼
    GuardedMain()              [LaunchWindows.cpp / LaunchMac.cpp]
           │
           ├─ LE_INIT()        (日志系统初始化)
           ├─ 创建 FPlatformWindow
           ├─ RAL::CreateDevice()
           ├─ CreateSwapchain()
           ├─ ShaderRuntimeCompiler::CompileHlslToSpirv()  (运行时编译 HLSL → SPIRV)
           ├─ CreateGraphicsPipeline()
           ├─ 主循环 while (Window->ProcessMessages())
           │       ├─ FWorld 组装
           │       ├─ FWorldRenderSceneExtractor::ExtractRenderScene()
           │       └─ FRenderer::RenderFrame()
           │
           ├─ WaitIdle()
           └─ 逆序清理资源 → LE_SHUTDOWN()
```

### 3.1 入口分离机制

`Launch.cpp` 仅负责**转译**：

```cpp
// Launch.cpp
#if PLATFORM_WINDOWS
int32 WINAPI WinMain(...)
#else
int main(int argc, char** argv)
#endif
{
    return GuardedMain();
}
```

真正的启动逻辑分散在平台特化文件中：
- `LaunchWindows.cpp`：实现 `GuardedMain()`，目前为 Triangle Demo 完整流程。
- `LaunchMac.cpp`：实现 `GuardedMain()`，包含离屏渲染 Pass（`FOffscreenPassResources`）与 Composite Pipeline，支持窗口缩放时动态重建交换链与离屏资源。

---

## 4. 窗口抽象设计

### 4.1 基类 `FPlatformWindow`

位于 `Public/Platform/Platform.h`，定义平台无关接口：

| 方法 | 用途 |
|------|------|
| `GetNativeHandle()` | 返回原生窗口句柄（`HWND` / `NSWindow*`）。 |
| `GetSurfaceDesc()` | 返回 `FRALSurfaceDesc`，供 RAL 创建交换链。 |
| `ProcessMessages()` | 消息泵/事件分发，返回 `false` 时退出主循环。 |
| `GetSize()` | 获取窗口客户区尺寸。 |
| `IsMinimized()` | 判断是否最小化，用于跳过渲染帧。 |

### 4.2 `FWindowsWindow`

- **原生句柄**：`HWND`
- **消息循环**：`PeekMessage` + `TranslateMessage` + `DispatchMessage`
- **WndProc**：静态回调映射 `g_WindowMap[HWND]`，处理 `WM_CLOSE` / `WM_DESTROY`
- **Surface 类型**：`ERALSurfaceType::Win32`

### 4.3 `FMacWindow`

- **原生句柄**：`void*` 桥接 `NSWindow*`、`NSView*`、`CAMetalLayer*`、`FMacWindowDelegate*`
- **事件分发**：`NSApp nextEventMatchingMask:untilDate:inMode:dequeue:` + `sendEvent:`
- **委托对象**：`FMacWindowDelegate`（Objective-C 对象），桥接 `windowWillClose` / `windowDidResize`
- **Metal 层**：`CAMetalLayer` 绑定到 `NSView`，`UpdateDrawableSize()` 同步 `backingScaleFactor`
- **Surface 类型**：`ERALSurfaceType::MetalLayer`

### 4.4 关键差异

| 特性 | Win64 | macOS |
|------|-------|-------|
| 缩放适配 | 固定尺寸，未动态重建 | `SyncSwapchainToWindowSize()` + 离屏资源重建 |
| 渲染路径 | 直接 BackBuffer | Triangle → Offscreen Texture → Composite → BackBuffer |
| DPI 处理 | 无 | `backingScaleFactor` × `drawableSize` |

---

## 5. 平台源码排除机制（`LaunchPlatformRules`）

`Build.ts` 通过 `LaunchBuild.Configure` 声明平台排除规则：

```csharp
public static void ApplyPlatformSourceExcludes(...)
{
    if (target.Platform != Platform.mac)
    {
        // 非 macOS：排除 Objective-C++ 窗口实现
        conf.SourceFilesBuildExclude.Add(".../Private/Mac/MacWindow.mm");
        return;
    }

    // macOS：排除全部 Win32 专属文件
    conf.SourceFilesBuildExclude.Add(".../Public/Windows/WindowsWindow.h");
    conf.SourceFilesBuildExclude.Add(".../Private/Windows/LaunchWindows.cpp");
    conf.SourceFilesBuildExclude.Add(".../Private/Windows/WindowsWindow.cpp");
}
```

此外，`LaunchProject` 构造函数将 `.m` / `.mm` 加入编译扩展名列表，确保 Objective-C 源码可被识别。

---

## 6. `ShaderRuntimeCompiler` 现状

`Private/ShaderRuntimeCompiler.h` 为**纯头文件实现**，提供运行时 HLSL → SPIRV 编译能力：

- **外部依赖**：调用系统 `glslangValidator` 可执行文件。
- **接口**：`Launch::ShaderRuntimeCompiler::CompileHlslToSpirv(SourceFilename, Stage, OutByteCode, EntryPoint)`
- **缓存策略**：基于哈希的临时文件（`%TEMP%/limitless_shader_runtime/shader_<hash>.spv`），编译成功后立即删除。
- **路径解析**：多层相对前缀 + 可执行文件目录向上回溯 10 层。

> **注意**：当前仅为占位实现，强依赖外部工具链；未来应迁移至静态链接 `glslang` 库或预编译管线缓存。

---

## 7. 依赖关系

`Build.ts` 显式声明的模块依赖：

```
Launch
 ├─ Core        (日志、基础类型、LE::Math::Vector3f 等)
 ├─ RAL         (FRALDevice、FRALSwapchain、FRALSurfaceDesc 等)
 ├─ RFG         (渲染框架图)
 ├─ Renderer    (FRenderer、FRendererFrameContext、渲染管线)
 ├─ World       (FWorld、FWorldMeshComponent、场景提取)
 └─ Spdlog      (日志后端)
```

---

## 8. 当前不足

| 不足项 | 现状说明 |
|--------|----------|
| 无输入系统 | 未处理键盘、鼠标、手柄事件；`WndProc` 仅处理关闭消息。 |
| 无事件循环 | `ProcessMessages()` 仅为消息泵，无高层事件分发（如 `OnResize`、`OnKeyDown`）。 |
| 无命令行解析 | `main` 参数 `argc` / `argv` 被 `(void)` 静默丢弃。 |
| 平台代码重复 | `LaunchWindows.cpp` 与 `LaunchMac.cpp` 大量重复（顶点结构、读文件、路径工具、管线创建逻辑）。 |
| 资源管理原始 | 全程裸 `new` / `delete`，无 RAII 或智能指针封装。 |
| ShaderRuntimeCompiler 脆弱 | 依赖系统 PATH 中的 `glslangValidator`，无版本校验与错误兜底。 |

---

## 9. 未来扩展

### 9.1 `FEngineLoop` 抽取

将 `GuardedMain()` 中的流程抽象为 `FEngineLoop`：

```cpp
class FEngineLoop
{
public:
    int32 Init();
    void Tick();
    void Exit();
};
```

`GuardedMain()` 缩减为：

```cpp
FEngineLoop EngineLoop;
EngineLoop.Init();
while (!GIsRequestingExit)
    EngineLoop.Tick();
EngineLoop.Exit();
```

### 9.2 Input 模块分离

- 将 `WndProc` / `NSEvent` 中的原始输入事件转换为平台无关的 `FInputEvent`。
- 新增 `Input` 模块，由 `FEngineLoop::Tick()` 统一轮询或事件驱动。
- 支持动作映射（Action Mapping）与轴映射（Axis Mapping）。

### 9.3 其他改进方向

- **命令行解析**：集成轻量级参数解析器，支持 `-windowed`、`-resX`、`-log` 等开关。
- **平台工具去重**：将 `ReadShaderFile`、`GetExecutableDirectory`、`IsValidSpirv` 等移至 `Core` 或新建 `PlatformUtility` 模块。
- **窗口事件委托**：`FPlatformWindow` 增加 `SetEventDelegate(IWindowEventHandler*)`，将关闭/缩放/输入事件外抛。

---

## 10. 源文件索引

| 文件 | 说明 |
|------|------|
| `Build.ts` | LimitlessBuilder 模块配置，定义依赖与平台排除规则。 |
| `Private/Launch.cpp` | 统一入口（WinMain / main）。 |
| `Private/Windows/LaunchWindows.cpp` | Win64 版 `GuardedMain()`（Triangle Demo）。 |
| `Private/Windows/WindowsWindow.cpp` | Win32 窗口实现。 |
| `Public/Windows/WindowsWindow.h` | `FWindowsWindow` 声明。 |
| `Private/Mac/LaunchMac.cpp` | macOS 版 `GuardedMain()`（含 Composite Pass）。 |
| `Private/Mac/MacWindow.mm` | AppKit/Metal 窗口实现（Objective-C++）。 |
| `Public/Mac/MacWindow.h` | `FMacWindow` 声明。 |
| `Public/Platform/Platform.h` | `FPlatformWindow` 抽象基类。 |
| `Private/ShaderRuntimeCompiler.h` | 运行时着色器编译工具（头文件实现）。 |
