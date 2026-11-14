# BasePass 纹理内容验收

在 [Imports.json](../Engine/Content/Examples/AssetImport/Imports.json) 中查看可编辑源及其持久 ID。默认场景从左到右绘制四块内容：

| 画面 | 源和预期 |
|---|---|
| 无乘色的 2×2 | `TextureQuad.obj`、`TextureCorners.material`、`TextureCorners.ppm` |
| M1 纯色三角形 | v1 `Triangle.obj`、`WarmOrange.material` |
| M1 纹理 | `TextureBlue.material` 引用 M1 `Palette.ppm` |
| 乘色后的 2×2 | `TextureTint.material` 对同一张 `TextureCorners.ppm` 施加 0.25 线性 RGB 乘数 |

编辑源文件，再用 [内容工作流](M1ContentWorkflow.md) 的命令重建。单个资产可用 `AssetImport --check` 验证语法和可编码性，`SceneTool check` 用暂存目录中的 cooked 文件进一步检查实体引用和 UV。材质可添加单行 `base-color-texture <UUID>`，场景继续保存 Mesh 和 Material UUID。纹理的 sRGB 值在着色器采样时解码为线性值，BasePass 输出再编码为 sRGB。贴图仅有 mip 0，使用 nearest、clamp-to-edge 和不透明输出。

可以在有效场景启动后测试一次失败的替换。先将样例 `Scene.json` 复制到工作目录，把首个贴图实体的 mesh 改成无 UV 的 `62fdc991-27a8-4a81-a077-647b47fd1f7b`，再把副本绝对路径传给 `LIMITLESS_DEMO_NEXT_SCENE`。首帧呈现后 Demo 尝试替换，日志应定位到实体和 Mesh/Material ID，随后输出 `Presented the preserved scene with 2 texture uploads.`。退出后可移除该环境变量。

## Win64 Debug 像素基准

在客户区截图的色块内部采样，避开边缘。以下目标为 8 位 RGB，每通道允许 ±3。使用默认相机和 z=0 网格时，可用 `s = 客户区高度 / (6 × tan(π/6))` 将世界坐标换算到像素。x 取该物体中心 ±0.25，y 取 ±0.25，屏幕原点在左上。纯色三角形在物体中心下方 0.1 处采样。

| 区域 | 左上 | 右上 | 左下 | 右下 |
|---|---|---|---|---|
| 无乘色的 2×2 | 255,0,0 | 0,128,0 | 0,0,255 | 255,255,0 |
| 0.25 线性乘色 | 137,0,0 | 0,66,0 | 0,0,137 | 137,137,0 |

M1 纯色预期为 255,160,97，M1 纹理左右色块为 32,35,10 和 128,64,192。RTX 4070 Ti 上的 992×562 和恢复后的 908×498 客户区截图中，上述 11 个像素都与目标相同。两次独立进程启动各上传 2 张带 Asset ID 的纹理，窗口改变尺寸时上传计数保持为 2。两次运行均启用了 Khronos 验证层并正常退出。测试进程通过 `DISABLE_VULKAN_OW_OBS_CAPTURE=1` 禁用了本机的第三方 Overwolf 捕获层。
