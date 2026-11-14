# Renderer Texture Sampling Delta

## MODIFIED Requirements

### Requirement: Textured and solid BasePass draws

BasePass SHALL 使用材质指向的颜色纹理与网格 UV 采样，按 sRGB 解码后在线性空间乘以材质 RGB 和顶点 RGB。采样用 nearest、clamp-to-edge 和 mip 0，输出保持不透明。应用方向光时 SHALL 以上述线性颜色为光照输入。未应用方向光的纹理和纯色绘制 SHALL 保持原有视觉输出。

#### Scenario: Sample an asymmetric pattern

- **WHEN** 绘制带 UV 的网格，其材质引用四角可区分且含中间亮度值的纹理
- **THEN** 可在画面内部区域验证四角对应关系及线性空间乘色结果

#### Scenario: Bind each draw independently

- **WHEN** 一个 BasePass 顺序绘制纹理 A、纯色材质、纹理 B 和另一个引用 A 的材质
- **THEN** 每个 draw 读取自己的材质输入，纯色材质输出不受相邻 draw 影响

#### Scenario: Multiply a sampled midtone by a colored light

- **WHEN** 启用方向光照亮使用中间亮度 sRGB 纹理的带法线网格，旁边还绘制原有无法线网格
- **THEN** 带法线网格的纹理先解码再乘光照，无法线网格保持原纹理和材质颜色
