# Design

## Context

`FSceneRenderer` accepts borrowed bytecode when creating RHI shaders. Demo currently locates sources and spawns a host compiler while initializing. Builder custom actions already support per-variant outputs and explicit inputs.

## Decisions

- Define one action per built-in Vulkan stage. The output directory is `Shaders/Vulkan` under the Game or Editor binary root. Use the build host's `glslangValidator` and pass arguments without a shell.
- Discover the transitive quoted includes rooted at the shader source directory during graph generation. Each action depends on its source, those includes and the build adapter. An available absolute compiler path is also an input. Discovery allows project generation before the compiler is installed.
- Compile to a sibling temporary artifact, validate its SPIR-V header and bounded size, and replace the known output after success. Report the affected source and the underlying compiler failure.
- Demo reads the five staged files by executable-relative path. The loader uses bounded aligned storage, checks the header and expected stage and entry point, and retains bytes through Renderer shutdown. Shader source, SDK tools and compiler subprocesses are build-side concerns.
- Stage shader artifacts for Game and Editor targets supported by Builder. Preserve the existing Win64 sample staging and executable-relative lookup.

## Scope

This change packages the current Vulkan shader recipes, including directional shadows. The platform still supplies a compatible Vulkan runtime and driver. Validation first targets Win64, including a package outside the source tree.
