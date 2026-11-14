# Synchronous Asset Loading

## Purpose

运行时通过显式挂载查找导入产物，按持久身份和预期类型同步获取可用对象。同一个加载会话复用已加载资源，并为调用方提供可诊断的失败结果。

## ADDED Requirements

### Requirement: Mounted catalog resolution

内容目录 SHALL 通过显式挂载建立索引，并支持按持久 ID 或规范化虚拟路径定位同一条目。索引 SHALL 拒绝重复 ID、重复路径、目录逃逸和会在目标文件系统上产生歧义的路径。

#### Scenario: Resolve independently of process working directory

- **WHEN** 两次运行从不同工作目录显式挂载同一个内容根，并分别通过 ID 和等价的规范化路径查询
- **THEN** 两次查询定位同一份导入产物

#### Scenario: Reject an invalid catalog

- **WHEN** 挂载内容含重复 ID、重复路径、与挂载路径不符的元数据，或符号链接或 junction 指向根外
- **THEN** 索引建立失败，诊断定位冲突条目，已发布索引仍可使用

### Requirement: Synchronous typed load and cache

加载 SHALL 在返回成功之前完成文件和类型校验及对象构造。会话内对同一资源的重复 ID 或路径请求 SHALL 返回同一个有效类型化句柄。

#### Scenario: Load an indexed mesh used by two entities

- **WHEN** 同一加载会话先按 ID、再按虚拟路径加载一个可渲染网格
- **THEN** 两次调用获得相同句柄，网格的顶点、索引和包围盒可用于绘制

#### Scenario: Load a CPU texture

- **WHEN** 通过其正确类型同步加载已导入的纹理
- **THEN** 返回有正确尺寸和完整 RGBA8 像素的 CPU 对象，构造不依赖 GPU

### Requirement: Failed load does not publish an asset

资源缺失、类型不符、版本不支持、数据无效、I/O 或构造失败时，加载 SHALL 提供带资源身份和原因的诊断，保留输出句柄的原值，不发布失败对象，并释放本次创建的临时资源。

#### Scenario: Reject corrupt or mismatched data

- **WHEN** 请求的类型不符，或者产物在索引后被截断或替换为身份不同的文件
- **THEN** 加载失败，失败资源不可由 registry 解析，先前成功加载的句柄仍有效

#### Scenario: Fail after creating the first GPU buffer

- **WHEN** 网格构造已创建顶点缓冲，但索引缓冲创建失败
- **THEN** 顶点缓冲被释放，registry 不出现该网格条目，修复故障后可以重试
