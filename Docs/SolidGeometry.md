# 立体几何样例

这个场景包含带贴图的箱体、立柱和地面。箱体和立柱共用一个封闭三角网格，每面有独立的法线和 UV。箱体的三轴缩放与旋转用于检查遮挡、纹理接缝和受光。样例的 OBJ、PPM、材质和场景 JSON 都在 [SolidGeometry](../Engine/Content/Examples/SolidGeometry)。

## 启动和编辑

Win64 Game 和 Editor 会把样例暂存到各自的 `SolidGeometryContent`。在仓库根目录构建 Game 和 SceneTool，再运行下面的命令。

```powershell
$gameRoot = (Resolve-Path 'Engine/Binaries/Win64/Debug/Limitless/Game').Path
$solidContent = Join-Path $gameRoot 'SolidGeometryContent'
$tool = (Resolve-Path 'Engine/Binaries/Win64/Debug/SceneTool/Program/SceneTool.exe').Path
& $tool check --json "$solidContent/Scene.json" Game "$solidContent/Assets"
$previousContent = $env:LIMITLESS_DEMO_CONTENT
try {
    $env:LIMITLESS_DEMO_CONTENT = $solidContent
    Start-Process -FilePath "$gameRoot/LimitlessGame.exe" -WorkingDirectory (Get-Location).Path -Wait
} finally {
    $env:LIMITLESS_DEMO_CONTENT = $previousContent
}
```

用 WASD 和右键转向绕看箱体。初始相机同时看到箱体的顶部和侧面，移动后可检查箱体的轮廓、地面遮挡和各面的贴图。要编辑并在运行中按 F5 重载，可用 `LIMITLESS_DEMO_SCENE` 指向场景副本，操作与 [场景预检及重载](M1ContentWorkflow.md#预检并重载正在运行的场景) 相同。

## 方向光阴影取样

初始相机在 `(0, 3.2, 6.3)`，向下俯视 23°，垂直视场角为 50°。立柱在 `(8, 1.8, 0.2)`，高 3.6。光线沿 `(-1.8, -1, -0.4)` 传播。立柱完全位于初始 16:9 画面的右侧，地面上的投影落在画面内。表格坐标以客户区左上角为原点，适用于初始 1280×720 视口。

| 地面测点 | 世界坐标 | 客户区像素 | 初始预期 |
| --- | --- | --- | --- |
| 立柱投影内部 | `(2.1, 0, -1.11)` | `(841, 365)` | 遮光 |
| 同一材质的受光处 | `(2.1, 0, 0.10)` | `(873, 418)` | 受光 |

立柱实体的 UUID 是 `96e540d4-209d-47be-8be0-08d14fb5ef02`。在场景副本中将它的平移 `z` 从 `0.2` 改为 `1.6` 并重载，两个位置的预期亮暗会互换，立柱仍在视野外。

## Win64 验收

初始场景有两个主相机可见网格。两个地面取样点分别测得 RGB `(0,0,0)` 和 `(159,167,170)`。移动屏幕外立柱后，两点亮暗互换。随后把相机移到 `(2.4,3.6,6)` 并向原点附近转向，同一世界坐标的取样值保持一致。最大化和最小化恢复后再次取样，结果相同。

RFG 依次执行 `ShadowDepth → BasePass → CompositeToBackBufferPass`。每个在途帧槽有独立的 1024×1024 深度图，使用一次点采样和固定接收深度偏移。图形验收进程正常退出，Vulkan 验证错误为零。
