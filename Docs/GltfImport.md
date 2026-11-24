# 静态 glTF 导入与预览

首版读取 glTF 2.0 的 `.gltf`、同一内容根内的外置 BIN 以及 Base Color PNG。每次选择一个静态 scene，转换有索引的三角形、位置、法线、UV 和节点 TRS。多 primitive 节点的每次绘制各有一条普通 Scene 网格记录。当前不透明 BasePass 使用 Base Color、Lambert 光照和 nearest/clamp/mip 0，转换时会报告被忽略的材质通道。GLB、动画、矩阵节点、压缩、透明材质和扩展超出此版本的范围。

## Win64 样例

在仓库根目录构建 `Limitless` 的 Win64 Debug Game。构建会生成 `LanternContent/current.json` 以及当前版本的 `Assets` 和 `Scene.json`。保留 `Shaders` 在 EXE 旁边。以下命令选定已发布版本，把场景另存为可编辑副本并启动。

```powershell
$game = (Resolve-Path 'Engine/Binaries/Win64/Debug/Limitless/Game').Path
$output = Join-Path $game 'LanternContent'
$release = Get-Content -Raw -LiteralPath (Join-Path $output 'current.json') | ConvertFrom-Json
$content = Join-Path $output "generations/$($release.generation)"
$author = Join-Path (Get-Location).Path ('Engine/Intermediate/LanternScene-' + [guid]::NewGuid() + '.json')
Copy-Item -LiteralPath (Join-Path $content 'Scene.json') -Destination $author
$env:LIMITLESS_DEMO_CONTENT = $content
$env:LIMITLESS_DEMO_SCENE = $author
Start-Process -FilePath (Join-Path $game 'LimitlessGame.exe')
```

可按场景实体 UUID 修改副本中的 `transform`。每次编辑后用无窗口工具确认 JSON、场景层级和挂载的资产引用，再回到 Game 按 F5。

```powershell
$sceneTool = (Resolve-Path 'Engine/Binaries/Win64/Debug/SceneTool/Program/SceneTool.exe').Path
& $sceneTool check --json $author Game (Join-Path $content 'Assets')
```

合法修改会在同一进程呈现。非法父引用或变换保留当前画面和漫游位置。F5 重载当前 Scene，并沿用启动时选定的资产版本。要预览重导入的新纹理，重新启动时解析新的 `current.json`，原 `$author` 可继续使用。

## 在 Win64 Editor 刷新资产版本

构建 Win64 Debug Editor 后，把 `LIMITLESS_EDITOR_CONTENT` 指向包含 `current.json` 的发布根，把 `LIMITLESS_DEMO_SCENE` 指向独立的作者场景。以下命令从 Editor 暂存的 Lantern 版本创建一个可编辑副本并启动预览。

```powershell
$editor = (Resolve-Path 'Engine/Binaries/Win64/Debug/Limitless/Editor').Path
$published = Join-Path $editor 'LanternContent'
$release = Get-Content -Raw -LiteralPath (Join-Path $published 'current.json') | ConvertFrom-Json
$content = Join-Path $published "generations/$($release.generation)"
$author = Join-Path (Get-Location).Path ('Engine/Intermediate/LanternEditorScene-' + [guid]::NewGuid() + '.json')
Copy-Item -LiteralPath (Join-Path $content 'Scene.json') -Destination $author
$previousEditorContent = $env:LIMITLESS_EDITOR_CONTENT
$previousScene = $env:LIMITLESS_DEMO_SCENE
try {
    $env:LIMITLESS_EDITOR_CONTENT = $published
    $env:LIMITLESS_DEMO_SCENE = $author
    Start-Process -FilePath (Join-Path $editor 'LimitlessEditor.exe') -WorkingDirectory $editor
} finally {
    $env:LIMITLESS_EDITOR_CONTENT = $previousEditorContent
    $env:LIMITLESS_DEMO_SCENE = $previousScene
}
```

将重导入的 `--output` 指向同一个 `$published`，发布成功后回到 Editor 按 **F6**。切换仍使用同一个主相机时，继续沿用当前漫游机位。**F5** 在已挂载的资产版本上重载作者场景，并应用作者机位。场景另存的文件和已发布的资产目录都是预览的输入。

在 Editor 的 `Firefly.log` 中搜索 `Editor content refresh `。单行 JSON 给出 `status`、`code`、`active`、`candidate` 和 `navigation`。`mounted` 表示启动时已挂载资产，`presented` 表示新代已出图，`unchanged` 表示当前代仍是最新版，`rejected` 表示候选未能切换。`navigation` 记录机位是沿用会话姿态还是从作者场景重新初始化。

## 手动导入与重导入

先构建 `AssetImport` 和 `SceneTool` 的 Win64 Debug Program。`inspect` 和 `check` 都以一个 JSON 对象报告结果，`check` 在内存验证导入计划和编码。传入 `--output` 时，`check` 还会与当前已发布的身份快照比较。

```powershell
$importer = (Resolve-Path 'Engine/Binaries/Win64/Debug/AssetImport/Program/AssetImport.exe').Path
$command = 'Engine/Builder/Source/Content/ImportGltf.ts'
$manifest = 'Engine/Content/Examples/Lantern/Import.json'
$work = 'Engine/Intermediate/LanternImports'
node --experimental-strip-types $command inspect --asset-import $importer --manifest $manifest --json
node --experimental-strip-types $command check --asset-import $importer --manifest $manifest --output $work --json
node --experimental-strip-types $command import --asset-import $importer --scene-tool $sceneTool --manifest $manifest --output $work --json
```

重导入已有作者场景时可在 `import` 命令加上 `--author-scene $author`。工具在发布前用候选资产重新校验此文件的引用。成功报告给出 `contentRoot` 和 `generation`。

## 清单与发布

在版本控制中保存 `Import.json`。`id` 标识导入集，`scene` 选择 glTF scene，`sourceDocumentSha256` 确认选择器适用的源文档。`assets`、`nodes`、`draws` 分别保存资产、变换节点和绘制子实体的稳定逻辑键及 UUID。资产另有 `kind` 和虚拟 `path`。资产选择器对应 glTF 的 `(mesh, primitive)`、`material` 或 `image`，绘制选择器引用节点和网格逻辑键。`material: null` 表示缺省白色材质，使用显式的资产绑定。

重新导出 `.gltf` 后先运行 `inspect`。例如 `node-chain` 仍代表原链条，但新文件的索引从 `1` 变成了 `3`，更新该记录的 `selector.node` 和新 `sourceDocumentSha256`，保留 `key`、`id` 及原资产路径。调整外置 PNG 或 BIN 可以沿用原映射。重导入现有批次时，工具拒绝移除稳定键或改变它的 UUID、资产类型或虚拟路径。

候选内容写在 `generations/<UUID>`，并用 SceneTool 检查后才通过 `current.json` 发布。失败时旧入口及其资源保留，重试前先修复诊断指出的源文件或清单。遇到 `stale_generation`，重新启动一次导入以读取最新基线。旧代仍可选取，恢复预览时把 `LIMITLESS_DEMO_CONTENT` 指向旧的具体代目录即可。作者场景应保存在各代目录外。

Lantern 样例的模型来源和许可记录随源文件保存。[样例目录](../Engine/Content/Examples/Lantern)
