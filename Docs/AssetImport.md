# AssetImport

`AssetImport` converts source content into `.leasset` files for the runtime. Keep the sources and import arguments in version control. Edit the source text, run `--check` to validate it, and reimport using the same asset ID, type and virtual path. The generated binary is reproducible from those inputs.

## Build and use

From the repository root on Windows:

```powershell
Engine/Builder/LimitlessBuilder.bat build --target AssetImport --platform Win64 --config Debug --type Program
$import = 'Engine/Binaries/Win64/Debug/AssetImport/Program/AssetImport.exe'
$source = 'Engine/Content/Examples/AssetImport'
$root = 'Engine/Intermediate/AssetImportExample'
New-Item -ItemType Directory -Force -Path "$root/Meshes","$root/Materials","$root/Textures" | Out-Null
& $import mesh --source "$source/Triangle.obj" --id 62fdc991-27a8-4a81-a077-647b47fd1f7b --path Game/Meshes/Triangle --output "$root/Meshes/Triangle.leasset"
& $import material --source "$source/WarmOrange.material" --id 62fdc991-27a8-4a81-a077-647b47fd1f7c --path Game/Materials/WarmOrange --output "$root/Materials/WarmOrange.leasset"
& $import texture --source "$source/Palette.ppm" --id 62fdc991-27a8-4a81-a077-647b47fd1f7d --path Game/Textures/Palette --output "$root/Textures/Palette.leasset"
```

Mount `$root` as `Game` to load these files. The virtual path is the mount name plus the path under the mounted root, with the final `.leasset` suffix removed. Use the same canonical spelling in every import command. The destination directory must already exist. Run imports for the same destination sequentially.

To validate a source without writing an asset file, replace `--output FILE.leasset` with `--check`. Both modes parse the same source and run the same format checks. Successful validation or publication exits with status 0. Failed imports report the source or destination and the cause, plus an OBJ line number when available. A destination with a different ID, path or type is rejected. Files that cannot be identified are also rejected. The previous file is kept if publication fails.

## Source syntax

| Kind | Source syntax |
| --- | --- |
| Mesh | OBJ `v x y z`, optional `vt u v`, and triangular faces with either `f v v v` or `f v/vt v/vt v/vt`. A mesh uses one face mode throughout. Coordinates are finite decimal floats, and indices are positive and one-based. Textured meshes expand combinations of position and UV indices in first-use order, preserving face order, winding and UV seams. Import converts OBJ's bottom-left UV origin to the top-left row order used by PPM, using `v = 1 - v`. Whitespace and `#` comments are accepted. Normals, quads and other records are rejected. |
| Texture | PPM `P3` (decimal RGB text) or `P6` (binary RGB), with a maximum value of 255. Whitespace and `#` comments are allowed between header tokens. P6 consumes one whitespace byte after 255 and then reads exactly the raster length, so use a single LF before binary pixels. P3 accepts comments and whitespace between samples. The result is RGBA8 with alpha 255 and the original row order. |
| Material | One `base-color r g b` directive sets the finite, non-negative linear RGB tint. An optional `base-color-texture <UUID>` directive refers to a non-nil texture Asset ID in the mounted catalog. Each directive appears at most once. Separate tokens with whitespace and use `#` for comments. |

`AssetImport --check` checks the source syntax and encoded payload. Use `SceneTool check` with the cooked asset root to validate cross-file texture dependencies and the selected mesh's UVs.

See [AssetFormat](AssetFormat.md) for binary layout and limits.
