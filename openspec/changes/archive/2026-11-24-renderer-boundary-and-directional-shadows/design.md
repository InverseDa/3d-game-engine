# Design

## Context

The current scene snapshot is tied to a BasePass pipeline and Demo assembles that pass's GPU bindings. The current sample fixtures are planar and suitable for pixel regression. RHI already supports depth-only graphics and sampled D32 textures.

## Decisions

- Renderer owns the built-in scene pipeline, shader modules supplied as bytecode, target attachments and frame-slot resources. Demo provides the device, swapchain, shaders and frame scope. WorldRenderer emits references to Renderer-owned mesh/material resources. An individual render pass selects its pipeline and binding layout.
- Vulkan implementation and SDK headers stay private to RHI. Static Win64 targets continue to link the Vulkan import library. The unused public raw descriptor accessor and allocation placeholder can be removed.
- The solid sample is a third content package that reuses the existing staged importer. Its box, pillar and ground are authored as triangulated OBJ with normals. The authored camera looks at the ground and the textured box. The light puts the pillar's shadow on screen while the pillar is outside the main view.
- WorldRenderer gathers geometry and world AABBs before main-camera culling when a light is active. Fit one world-space orthographic light projection to those AABBs. Render one 1024-square D32 shadow map per in-flight slot and reuse the map only after that slot's fence. RFG owns the depth-write to fragment-sample dependency.
- The BasePass shadow variant reads a frame-slot constant buffer and sampled depth. Reconstruct the light clip position from camera clip coordinates. Preserve the 128-byte object constants and the original light/material rules. A small receiver depth bias handles self-shadowing. The unshadowed recipe stays available when shadow shaders are absent.

## Scope and trade-offs

The current scene, material and mesh formats cover this sample. One map and point depth comparison yield hard shadow edges. Light bounds derive from scene geometry so camera motion alone leaves projection stable. The 3D case tests one directional light, opaque meshes and the existing Win64 Vulkan path.
