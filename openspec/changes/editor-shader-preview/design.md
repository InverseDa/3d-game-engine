# Design

## Context

`DemoApplication/Build.ts` 为 Win64 Game 和 Editor 配置离线 Shader，`DemoShaders` 在启动时加载打包字节码。`BasePassShadowPS.lsf` 通过 `BasePassPS.lsf` 引入着色实现，`BasePassConstants.lsi` 也供 BasePass VS 使用。`FSceneRenderer` 持有 BasePass PSO，每帧把当前 PSO 传给图录制。Editor 的 F5 和 F6 分别处理场景加载与资产代际切换。[离线配方](../../../Engine/Source/Runtime/DemoApplication/Build.ts) · [Shader 加载](../../../Engine/Source/Runtime/DemoApplication/Private/DemoShaders.cpp) · [Renderer](../../../Engine/Source/Runtime/Renderer/Private/Renderer/SceneRenderer.cpp) · [Editor 入口](../../../Engine/Source/Runtime/DemoApplication/Private/DemoApplication.cpp)

## Goals / Non-Goals

- 同一份编译配方服务构建和预览。AI 可编辑现有 `.lsf` 和 `.lsi` 文本，用请求日志和源码位置确认结果。
- Renderer 的更新入口接收平台无关的字节码，复用现有 RHI 资源和帧生命周期。Windows 进程、工具路径和 F7 状态归 Editor 工具链。
- 后续工作包括文件监听、VS 和其他 Pass、材质参数及绑定布局迁移。首版只编译 `BasePassShadowPS.lsf` 的 `MainPS`。共享 include 对 VS 的改动须通过离线构建生效。

## Decisions

### Editor 启动配置与工具链边界

仅在 `WITH_EDITOR && PLATFORM_WINDOWS` 编译预览入口。开发者通过 `LIMITLESS_EDITOR_SHADER_ROOT` 指定可信的源码 checkout 绝对路径。Editor 从中定位 `Engine/Content/Shaders` 和 Builder 预览入口。Node 优先使用现有 `LIMITLESS_BUILDER_NODE` 的绝对路径，其次从绝对 PATH 项解析 `node.exe`。Builder 从 `VULKAN_SDK` 和绝对 PATH 项解析 Shader 工具。工具依赖在 F7 请求时检查，Editor 正常启动仍只依赖离线产物。输入层提供 F7 按下边沿，预览控制器放在 Editor 用到的 DemoApplication 私有代码里。[Builder 启动配置](../../../Engine/Builder/LimitlessBuilder.bat)

Builder 抽取现有 Vulkan 配方、编译参数及递归 include 规则，供离线编译和预览 worker 复用。保留当前离线构建的依赖追踪和输出路径。发起一次完整 Builder 构建会耦合目标打包、依赖发现和预览时序，F7 只调用 Shader 编译入口。[离线编译入口](../../../Engine/Builder/Source/Content/CompileVulkanShader.ts) · [Vulkan 工具支持](../../../Engine/Builder/Source/Content/VulkanShaders.ts)

### 单请求编译和诊断

一个 Editor 会话只接受一个在途请求，首次出图待确认期间也视为忙碌。编译、验证和反射在子进程内进行。Windows 私有适配器使用绝对可执行文件和参数启动，不经过 shell。用 Job Object 管理 worker 和子工具，成功加入 Job 后才开始执行。设置有限超时，Editor 退出或超时后清理该请求的进程树。Editor 帧循环轮询完成状态。[Windows Job Object limits](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information)

使用独立的每会话、每请求工作目录。按离线 include 规则复制递归依赖及相对路径，保留源码路径映射，并计算内容散列。worker 对采集后的源文件集合及散列做复核，再于交付前比较一次当前源码。变化时返回 `source_changed`，由下一次 F7 重新采集。物理路径和相对 include 都须限制在 Shader 源目录内。工具输出和候选字节码限制大小，成功完成后才交付固定路径的候选。候选携带本次请求的身份，超时、取消或旧请求的结果不可应用。

Editor 日志保留请求 ID、阶段和稳定失败码。worker 的 JSON 诊断文件保留有上限的编译器输出，并把快照文件及行号映射到编辑过的源码。为同一运行会话中的开发者或 AI 保留最近请求的诊断，清理旧的临时字节码和源码快照。编译失败通过日志指向可读取的诊断文件。

### 用启动字节码校验 BasePass ABI

以 Editor 启动时实际载入的 `BasePassShadowPS.spv` 为基准。在预览会话首次运行时，把这份不可变字节码交给 worker 做反射。当前预览版本可以多次替换，基准始终是与已打包 VS 和运行时布局配套的启动版本。

先用 `spirv-val` 按离线配方的 Vulkan 目标环境验证模块，再从 `spirv-cross --reflect` 和 `spirv-dis` 提取首版 BasePass 所需接口。规范化后对比下列字段，忽略变量调试名称、SPIR-V ID 和声明顺序。超出首版检查范围的接口形态直接拒绝。`spirv-val` 检查模块本身，管线创建成功也不能作为引擎绑定与布局的兼容性判据。[SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) · [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross/blob/main/README.md)

| 检查项 | 当前打包 Shader 的接口 |
| --- | --- |
| 入口及模块声明 | `MainPS`，Fragment，`OriginUpperLeft`；同时检查能力、扩展、内存模型和相关执行模式 |
| 资源 | set 0 的颜色图和采样器，set 1 的光照常量，set 2 的阴影常量、阴影图和采样器。检查 set、binding、描述符类型和数量，以及图像类型 |
| 常量 | 32 字节光照常量、80 字节阴影常量、128 字节 push constant。检查成员类型、偏移、范围及相关布局修饰 |
| 阶段接口 | location 0–3 分别为 `vec3`、`vec2`、`vec3`、`vec4`，单个 `vec4` 颜色输出。检查位置、类型、相关修饰和内建输入输出 |

精确匹配允许首版沿用现有布局、绑定组和渲染目标。编译器若因改成常量颜色而裁掉正在使用的输入或资源，预览会拒绝候选。Lantern 正向用例保留原计算并对最终 RGB 加色调，确保测试的是像素逻辑更新。[像素着色实现](../../../Engine/Content/Shaders/BasePassPS.lsf)

### 在帧边界替换 PSO

Editor 在处理场景和资产请求后，且在本帧录制前轮询候选。`FSceneRenderer` 使用已存在的 VS、绑定布局、push constant 范围和 BasePass 格式创建候选像素 Shader 和图形 PSO。GPU 资源创建留在 Renderer，Editor 只接收结果。字节码、Shader 与 PSO 由同一个 Renderer 所有者持有，析构顺序为 PSO、Shader、字节码，满足当前 RHI 及 Vulkan 实现的借用关系。

候选创建失败时直接释放候选。成功后复用 Scheduler 的帧退休路径，确认旧 PSO 的在途帧已经完成，再整体替换；设备丢失走已有设备失败路径。首版允许切换时等待在途帧，后续可利用同一所有权边界延迟回收旧版本。图录制每帧绑定当前 PSO，因此兼容候选可沿用缓存的图计划。阴影深度 Pass、合成 Pass、资产 Provider、当前场景与相机都沿用原有对象。[Renderer 帧录制](../../../Engine/Source/Runtime/Renderer/Private/Renderer/SceneRenderer.cpp) · [Scheduler 帧退休](../../../Engine/Source/Runtime/Renderer/Private/Renderer/RenderFrameScheduler.cpp)

Editor 将请求 ID 和本次帧实际使用的 Shader 版本关联，只在该版本的帧 `PresentStatus == Success` 时记为 `presented`。在处理交换链重建分支前检查这项结果，避免把一次已成功的 Present 忽略。最小化或交换链重建可推迟这条回执。F5/F6 与 F7 各自跟踪回执，同一帧可确认多个请求。退出时先取消编译任务，再按 Renderer 原有顺序退休 GPU 资源和销毁设备。

## Risks / Trade-offs

- 工具输出格式随 SDK 版本变化。以已安装的 Win64 SDK 和当前离线产物做夹具，锁定首版读取的接口字段，解析失败时保留已启用版本并提供诊断。
- 创建 PSO 和等待在途帧会在切换时占用帧时间。用慢编译验收后台过程仍有连续帧，用实际记录和 Present 验证切换。
- ABI 相同的 Shader 仍可能有着色逻辑错误。保留源码差异、请求诊断和 Lantern 出图证据，修正源码后可再次 F7，重启则回到离线版本。
