# Tasks

## 1. Module boundaries and content

- [x] 1.1 Confine Vulkan headers and raw descriptors to RHI. Verify Win64 Game and Editor linkage.
- [x] 1.2 Move pass ownership into Renderer and use logical snapshot mesh/material references. Run existing scene, frame-slot and pixel regressions.
- [x] 1.3 Stage the textured solid sample and verify asset import, SceneTool and visible geometry. Document how to open it.

## 2. Directional shadow

- [x] 2.1 Keep light casters independent of the main camera. Produce and consume the slot-local shadow map through RFG. Verify low-level and snapshot tests.
- [x] 2.2 Test the solid sample on Win64 with an outside-view caster, caster edit, camera motion and window transitions. Document the pixel probes and results.

## 3. Delivery

- [x] 3.1 Build Win64 Game, Editor and tools. Run Builder and C++ tests and generate the review solution.
