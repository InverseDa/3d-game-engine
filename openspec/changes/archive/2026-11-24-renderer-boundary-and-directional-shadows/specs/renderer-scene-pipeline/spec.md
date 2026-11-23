# Renderer Scene Pipeline Delta

## Purpose

Provide a reusable rendering path from a scene snapshot to the application output, with backend-specific handles confined to the graphics backend and a staged solid-geometry scene for verification.

## ADDED Requirements

### Requirement: Own built-in scene pass resources in Renderer

The Renderer SHALL support recording the built-in scene pipeline from the frame's execution inputs and a scene snapshot containing pass-independent mesh and material references. The graphics backend SHALL retain ownership of native Vulkan objects and descriptors.

#### Scenario: Render a logical scene and resize its output
- **WHEN** the application records frames and resizes its presentation target
- **THEN** Renderer selects its pass pipelines and rebuilds the relevant attachments without requiring per-pass configuration by the application

### Requirement: Stage an editable three-dimensional sample

The Win64 Game and Editor build SHALL stage the textured box, pillar and ground scene through the existing asset pipeline.

#### Scenario: Inspect the same object from multiple angles
- **WHEN** the developer opens the solid sample and navigates around the box
- **THEN** the visible faces, occlusion and lighting change according to the 3D geometry, and SceneTool can validate the sample references
