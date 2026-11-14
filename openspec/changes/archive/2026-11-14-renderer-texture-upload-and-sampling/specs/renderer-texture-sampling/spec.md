# Renderer Texture Sampling

## Purpose

让现有颜色纹理经过受验证的同步上传和明确的 GPU 生命周期管理进入不透明 BasePass，使文本资产中的像素、UV 与材质配置可以得到可重现的渲染结果。

## ADDED Requirements

### Requirement: Sample ready color textures

Renderer SHALL 将有效的 RGBA8 颜色纹理上传为可供像素着色器采样的单 mip 2D sRGB 资源，并在 GPU 确认完成后发布。同一设备和内容会话内，同一纹理 Asset ID SHALL 复用同一 GPU 纹理。

#### Scenario: Share an uploaded image

- **WHEN** 两个不同材质引用同一个纹理 UUID
- **THEN** 两者可使用同一份已完成的 GPU 上传，同一 BasePass 对该图像只声明一次采样读取

#### Scenario: Failed upload

- **WHEN** 资源分配、映射、录制、提交或完成确认失败
- **THEN** 候选纹理及依赖材质不发布，诊断标明资产及失败阶段；可能仍在 GPU 使用的对象保持存活直到安全回收

### Requirement: Textured and solid BasePass draws

BasePass SHALL 使用材质指向的颜色纹理与网格 UV 采样，按 sRGB 解码后在线性空间乘以材质 RGB 和顶点 RGB。采样用 nearest、clamp-to-edge 和 mip 0，输出保持不透明。纯色材质 SHALL 保持原有视觉输出。

#### Scenario: Sample an asymmetric pattern

- **WHEN** 绘制带 UV 的网格，其材质引用四角可区分且含中间亮度值的纹理
- **THEN** 可在画面内部区域验证四角对应关系及线性空间乘色结果

#### Scenario: Bind each draw independently

- **WHEN** 一个 BasePass 顺序绘制纹理 A、纯色材质、纹理 B 和另一个引用 A 的材质
- **THEN** 每个 draw 读取自己的材质输入，纯色材质输出不受相邻 draw 影响

### Requirement: Preserve GPU resources through frame completion

纹理、view、sampler 和材质绑定 SHALL 在使用它们的提交帧结束前保持有效。窗口大小改变 SHALL 保留材质纹理及会话缓存，卸载或退出 SHALL 在帧完成后释放。

#### Scenario: Render through lifecycle transitions

- **WHEN** Win64 Vulkan Demo 加载从磁盘生成的混合材质场景，完成多帧，resize、最小化、恢复，再正常退出并重新加载
- **THEN** 采样结果保持正确，resize 不触发纹理重传，启用 Khronos 验证层且错误为零
