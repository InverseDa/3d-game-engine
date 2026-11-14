# 同步资产加载

`AssetLoading` 将离线工具写出的 `.leasset` 文件装配成现有 Asset Registry 中的对象。导入的可编辑源文件和重建命令见 [AssetImport.md](AssetImport.md)，二进制布局见 [AssetFormat.md](AssetFormat.md)。

向 `FAssetCatalog::Rebuild` 提供 mount 名和绝对物理根目录。例如，将 `Game` 挂载到 `ContentCooked` 后，`Game/Meshes/Triangle` 对应 `ContentCooked/Meshes/Triangle.leasset`。目录扫描读取资源头，校验文件中记录的虚拟路径、类型和持久 ID。逻辑路径使用 v1 可移植 ASCII 命名规则，路径比较区分大小写。索引拒绝仅大小写不同的别名，物理根目录支持 UTF-8。最多支持 64 个 mount 和 16,384 个目录条目。索引构建失败保留上一次索引，成功替换会使先前的条目视图失效。

`FAssetLoadSession` 借用目录和 provider，每个会话拥有一个 registry。通过 ID 或虚拟路径同步加载，成功时返回类型化句柄。在同一会话中请求同一资产会复用句柄。首次读取时重新检查文件头、身份和数据，再构造对象并登记。缺失文件、类型不符、解析失败、构造失败或登记失败都会保留输出句柄。诊断包含结果、已知资源 ID、路径和底层失败原因。

CPU 纹理可直接加载。Mesh 和 Material 的创建与销毁由应用提供成对回调。创建回调成功时交付一个新对象，失败时负责释放自己的部分构造结果。Registry 在登记成功后接管对象，登记失败则加载器立即调用销毁回调。保持 provider 的代码和上下文存活直到会话销毁，保持目录在会话期间固定。若调用方把对象交给 GPU，销毁会话前必须等待相应帧完成。

按仓库的 Test target 构建并运行 `LimitlessTestsDebug.exe` 可验证目录和会话契约。测试使用纯 CPU 的 provider，覆盖路径与 ID 查找、索引失败后保留旧结果、像素解码、句柄复用、篡改、丢失文件及登记失败后的释放。
