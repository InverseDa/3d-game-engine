# Tasks

## 1. Build and staging

- [x] 1.1 Register one Builder action per staged Vulkan shader and track transitive include inputs.
- [x] 1.2 Validate shader bytes before atomic publication, with focused tests for dependency discovery and failed compilation.

## 2. Runtime

- [x] 2.1 Load the five executable-relative artifacts before RHI startup with bounded validation and shader-byte lifetime ownership. Verify missing and corrupt files produce a startup diagnostic.

## 3. Verification and delivery

- [x] 3.1 Build isolated Win64 Game and Editor outputs, run Builder and C++ coverage, and demonstrate affected-only recompilation with Ninja.
- [x] 3.2 Launch detached Win64 Game with staged assets and shaders, confirm the 3D shadow frame on the GPU, and document the EXE and code-review entry points.
