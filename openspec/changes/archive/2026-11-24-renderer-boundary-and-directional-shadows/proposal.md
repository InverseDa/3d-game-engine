# Proposal

## Why

Demo currently configures GPU resources for individual scene passes. Directional shadows need a second pass that selects its own geometry and pipeline. A volumetric sample makes depth, culling and lighting visible while exercising the same content import path.

## What Changes

- Put the scene pipeline, attachments, and per-frame lighting in Renderer. Extract mesh and material references without choosing pass-specific GPU bindings in WorldRenderer.
- Keep Vulkan implementation headers and raw native descriptor handles inside RHI.
- Add a staged, editable 3D sample using a textured box, a pillar and a ground receiver.
- Render one directional shadow map with light-space bounds and a caster list that includes geometry outside the main camera.

## Capabilities

### New Capabilities

- `renderer-scene-pipeline` defines scene assembly and the 3D sample.

### Modified Capabilities

- `renderer-directional-lighting` adds single-map shadows.

## Impact

Renderer, WorldRenderer, RHI, the Demo assembly, shaders, and content staging change. Existing scene JSON, AssetImport and cooked formats remain compatible.
