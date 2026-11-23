# Proposal

## Why

Demo currently compiles five HLSL shaders at startup using repository source paths and a host compiler. A built Game should be portable with staged data, and the build should reconstruct shader artifacts when their source or shared includes change.

## What Changes

- Compile the built-in Vulkan shaders as per-shader Builder custom actions and place SPIR-V beside each Game or Editor output in `Shaders/Vulkan`.
- Track transitive local shader includes and publish each artifact after successful validation.
- Load and validate the staged bytecode from the executable directory at startup, retaining the bytes for the lifetime of their RHI shaders.
- Validate incremental compilation and exercise the 3D sample from a detached Win64 package.

## Capabilities

### New Capabilities

- `shader-artifacts` owns build-time shader generation, staging, and runtime consumption.

## Impact

Builder, shader source dependencies, DemoApplication startup, Win64 Game and Editor packaging, and verification commands change. HLSL remains the editable source, and Vulkan is the current artifact backend.
