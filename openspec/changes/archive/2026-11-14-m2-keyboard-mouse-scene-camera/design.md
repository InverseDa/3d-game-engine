# Design

## Context

`FEngineLoop` 依次调用 `ProcessPlatformEvents → Update → Render`。Demo 窗口模块在第一个阶段泵出窗口生命周期事件，`FDemoRenderRuntimeModule` 在 Update 完成 World tick，随后让 WorldRenderer 从已提交的 World 构建快照。场景发布已包含候选 World、资产会话和渲染快照预检。

当前主相机由 WorldRenderer 按 active、priority、`FRenderPrimitiveId` 选出。作者机位来自实体和父层级的世界变换，Scene 的保存路径读取已提交的本地变换。场景和资产格式见 [Scene v1](../../../Docs/SceneFormat.md) 与 [M1 工作流](../../../Docs/M1ContentWorkflow.md)。

## Goals / Non-Goals

把 Win32 消息限制在 Platform 内部，把键位和相机控制策略放在 DemoApplication，并让视图与剔除共同消费会话机位。公共输入使用引擎值类型。实现范围为 Win64 的单 Demo 窗口、WASD 和右键，完整键位映射、手柄、编辑器视口控制、macOS 键鼠和方向光各有独立范围。

## Decisions

### Platform 负责窗口输入与捕获

在现有窗口消息泵旁提供窗口作用域的输入值，包含 `WindowId`、引擎键码、右键捕获状态、相对位移和输入取消。保持窗口生命周期事件、消息顺序和其他窗口的路由。Win32 类型、鼠标设备注册和捕获所有权由 Windows 后端管理。Application 继续提供已有帧阶段和时间，macOS 后端继续提供已有的窗口生命周期。

Win64 为单 Demo 窗口注册前台鼠标 Raw Input，保留普通右键消息，按 Win32 约定将前台 `WM_INPUT` 交给默认窗口过程完成清理，并随窗口会话解除注册。相对位移使用有符号值，忽略绝对位置包，并在捕获期间累计。这样视角可以在指针到达屏幕边缘后继续转动。直接用客户区坐标差会受指针边界限制。输入队列用合并相邻位移或线性遍历处理鼠标消息突发，键盘、捕获和焦点的事件边界须保留。 [Microsoft Raw Input](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input) · [WM_INPUT](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-input) · [RAWMOUSE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawmouse)

右键在可交互时申请捕获，使用当前捕获所有者确认成功。注册相对输入或申请捕获失败时，保留键盘控制并记录可定位的失败。普通松开右键结束捕获，当前帧在释放前已接收的位移仍可消费一次。失焦、最小化、零尺寸、系统取消、意外丢失捕获或退出使输入状态归零，清除当帧累计位移并结束捕获。释放前先提交内部状态，避免主动释放触发的 `WM_CAPTURECHANGED` 被当作意外丢失。恢复焦点时等待真正的新按下，过滤恢复前按住的键产生的自动重复。系统快捷键继续通过窗口默认处理。 [SetCapture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture) · [WM_CAPTURECHANGED](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged) · [WM_KEYDOWN](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-keydown)

### DemoApplication 按帧更新会话机位

| 阶段 | 职责 |
| --- | --- |
| `ProcessPlatformEvents` | Demo 窗口模块归并本窗口输入，保留跨帧按住状态，产生只供本帧读取的状态快照。 |
| `Update` | Demo 消费一次快照并更新会话机位，与既有 World tick 同处 Update 阶段。World 和 Gameplay System 调度继续处理场景逻辑。 |
| `Render` | Demo 将当前会话机位交给 WorldRenderer。WorldRenderer 构建快照，BasePass 消费其中的视图与可见网格。 |

使用引擎的右手坐标系，局部 `-Z` 为前、`+X` 为右、`+Y` 为世界上方。WASD 沿当前相机的前、右方向移动，合成方向归一化后乘以速度与帧时间。初始调试值为每秒 3 单位，单帧移动时间上限 50 ms。鼠标位移直接换算为旋转角，初始灵敏度为每单位 0.0025 弧度，横向控制世界 Y 偏航，纵向控制俯仰并限制极点翻转。作者姿态原样用于无输入的首帧。移动速度、灵敏度与俯仰保护在 Demo 内集中定义。

### WorldRenderer 从同一个主相机生成视图和剔除

现有场景预检复用同一处主相机选择和刚性世界变换验证，向 Demo 提供所选相机身份与作者世界机位作为种子。种子在场景成功发布时成为会话状态，随场景会话销毁。候选失败时保留当前会话。成功切换时清空旧场景的按键状态和累计位移。若右键仍由窗口捕获，它继续接收切换后的新移动，直到右键松开或捕获取消。

给 `FWorldRenderSnapshotBuildContext` 增加可选的会话机位，绑定到选中主相机的运行时身份。WorldRenderer 用有效的刚性会话姿态计算 View 和 ViewProjection，并用同一 ViewProjection 剔除。投影仍取所选相机的参数。会话机位缺失或身份与主相机不匹配时，以当前主相机的作者姿态构建快照。为带父级的相机，以预检解析出的世界机位作为种子，避免在移动时逆解父变换或改动场景层级。

### Scene 与 SceneTool 负责作者数据

会话机位单独存在 Demo 中。若把漫游直接写入作者相机的 Transform，现有 `CaptureScene` 会把它写入显式存档。在场景 World 里加临时实体也会影响 World 实体与持久 UUID 的对应关系。独立会话状态让保存路径继续直接读取作者数据。

沿用 `SceneTool check SCENE.json MOUNT ABSOLUTE_ASSET_ROOT` 对文档字段、层级和资源引用做 CPU 校验。相机的合法姿态和 FOV 由现有文档解码检查，实际机位能否用于渲染由 Game 的场景预检确认，包括解析父层级后的刚性约束。用同一份实例分别跑这两个入口，覆盖 AI 编写文件到引擎消费的完整路径。

## Risks / Trade-offs

- 同一次消息泵可能同时收到右键松开和系统取消。分别处理普通结束和强制取消，并用合成事件验证释放前位移只应用一次、取消清除未消费位移。
- 场景发布和导航都可能改变画面。回归同时检查相机矩阵与可见网格集合，并比较失败替换前后的机位。
- Win32 窗口边缘、捕获和系统切换依赖真实窗口行为。用无窗口测试覆盖状态归并，再用 Win64 真机复现窗口外交互、切焦和恢复。

## Migration Plan

Scene v1、实体 UUID、资产挂载和现有构建入口继续使用。输入和机位参数都在运行期产生，随进程销毁。
