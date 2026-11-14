# 单方向光样例

在仓库根目录构建 Win64 Debug Game。Builder 将两个内容样例分别暂存到可执行程序旁的 `DemoContent` 和 `DirectionalLightContent`。运行下列命令查看光照样例。

```powershell
Engine/Builder/LimitlessBuilder.bat build --target Limitless --platform Win64 --config Debug --type Game
Engine/Builder/LimitlessBuilder.bat build --target SceneTool --platform Win64 --config Debug --type Program
$gameRoot = (Resolve-Path 'Engine/Binaries/Win64/Debug/Limitless/Game').Path
$lightContent = Join-Path $gameRoot 'DirectionalLightContent'
$tool = (Resolve-Path 'Engine/Binaries/Win64/Debug/SceneTool/Program/SceneTool.exe').Path
& $tool check "$lightContent/Scene.json" Game "$lightContent/Assets"
$env:LIMITLESS_DEMO_CONTENT = $lightContent
try {
    Start-Process -FilePath "$gameRoot/LimitlessGame.exe" -WorkingDirectory (Get-Location).Path -Wait
} finally {
    Remove-Item Env:LIMITLESS_DEMO_CONTENT
}
```

源文件在 [DirectionalLight](../Engine/Content/Examples/DirectionalLight) 中。按 UUID `8a0a5d74-17ef-48e0-a46b-f7105518e707` 找到方向光，编辑 `directionalLight` 的世界传播方向、线性颜色、强度和启用状态。修改源文件后重新构建。也可以复制暂存的 Scene.json，在副本中修改、运行同一个 `SceneTool check`，再用 `LIMITLESS_DEMO_SCENE` 指向副本绝对路径。用 `LIMITLESS_DEMO_SAVE_SCENE` 保存到另一个路径并重启，可检查作者光源数据和画面。

## 取样位置

初始相机在 `(0,0,4)`，垂直视场角为 60°。令 `s = 客户区高度 / (8 × tan(π/6))`。各块中心的像素坐标是 `(宽度/2 + x × s, 高度/2 - y × s)`。参考颜色为 RGB，每通道容差 ±3。

| (x,y) | 内容 | 初始光照 |
| --- | --- | --- |
| (−1.4, 0.7) | 白材质、+Z 法线 | (188,137,99) |
| (0, 0.7) | 白材质、+X 法线 | (0,0,0) |
| (1.4, 0.7) | 白材质、−Z 法线 | (0,0,0) |
| (−1.4, −0.7) | 128 灰贴图、+Z 法线 | (92,66,46) |
| (0, −0.7) | (1,0,1) 作者法线、(2,1,1) 缩放 | (178,130,94) |
| (1.4, −0.7) | 无法线旧网格、白材质 | (255,255,255) |

反转 `direction` 可交换上排左右面的亮暗。在相同方向把灯光颜色改为白色时，倾斜法线区域约为 `(178,178,178)`。将强度设为 0，受光网格为黑。停用灯后，白材质和灰贴图恢复原值。默认的 [纹理样例](TextureSampling.md) 可用于回归无灯输出和 11 个历史取样点。

## Win64 Debug 验收记录

- RTX 4070 Ti 和 Vulkan 1.4.341 上，样例的 6 个初始取样点及默认纹理场景的 11 个取样点均精确匹配。反转、零强度、停用、非均匀缩放和镜像通过。WASD 移动后，同一块表面的参考颜色仍为 `(178,178,178)`。
- 按 UUID 将传播方向作者值改为 `[0,0,2]`，强度改为 `0.25`。SceneTool 校验前后文件哈希一致。显式保存保留方向长度与强度，重启画面与保存前一致。重复启用灯、退化法线矩阵和损坏的 v3 网格都会拒绝候选场景，当前画面仍可用。
- 专用测试构建让两个帧槽交替使用正向和反向照明。resize 前、resize 后和最小化恢复后各采集 110 对像素，各观察到 27 次亮暗切换，无混合或上一帧残留。验收进程启用了 Khronos 验证层，错误为 0，窗口变化前后均只累计上传 1 张纹理。
