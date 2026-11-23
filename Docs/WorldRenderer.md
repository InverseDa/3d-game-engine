# WorldRenderer module

## Responsibility and dependency direction

WorldRenderer is the downstream bridge between committed gameplay state and
Renderer data. Its public dependency set is exactly Core, Asset, Renderer, and
World. World remains unaware of Renderer/RHI, Renderer remains unaware of
World, and Asset remains Core-only.

`FWorldRenderer::BuildSnapshot` accepts an Idle `FWorld`, the synchronous
`FAssetRegistry`, an explicit frame/viewport build context, and an output
`FWorldRenderSnapshot`. It updates the World's private Transform cache,
validates the paired dense entity and component spans, resolves typed Mesh and
Material handles, and builds a complete candidate before publication.

## Snapshot contract

`FWorldRenderSnapshot` is non-copyable and noexcept movable. Consumers can read
its frame index, publication state, const `FRenderScene`, and authored primary
camera pose. Its arrays and owned debug strings use the allocator supplied when
the snapshot is constructed. A failed build leaves it empty and unpublished, including
when replacing a previously successful result.

Each visible mesh proxy owns its debug name, sort key, world transform,
world-space AABB and `NormalToWorld` when lit. A `FRenderPrimitiveId` records
the World domain and packed Entity generation/index. The proxy also carries the
persistent material ID and borrows the resolved Mesh and Material payloads.
Renderer passes select their own pipeline and access GPU resources through
those payloads.

A material with an authored color texture requires authored mesh UVs.
Snapshot extraction rejects incompatible pairs before culling and reports
the mesh index and primitive ID.

Mesh assets provide validated local-space AABBs. WorldRenderer transforms them
with the absolute linear matrix, so rotation and non-uniform scale remain
conservative. Default bounds are invalid sentinels; non-finite transforms or
unordered/non-finite bounds fail publication. The selected camera supplies View,
Projection, and ViewProjection matrices. WorldRenderer conservatively tests all
eight AABB corners in homogeneous clip space. It retains plane contact, omits
fully outside meshes from the visible list and rejects non-finite clip
coordinates.

Exactly one primary camera is published. Active candidates are ordered by
descending priority and then ascending persistent primitive ID. The winner must
have a valid Transform and a finite rigid camera basis. The convention is
right-handed, -Z forward, +Y up, column vectors, Vulkan depth [0,1], and an
explicit projection Y flip.

On a published snapshot, `GetAuthoredPrimaryCameraPose()` supplies the selected
camera's primitive ID and validated camera-to-world matrix for initializing a
session pose. This matrix includes its authored parent transforms. A
`CameraOverride` in the build context applies only when its primitive ID matches
the selected camera. An absent or stale identity uses the authored pose. A
matching override must pass the same finite rigid-transform checks as the
authored pose. The session pose supplies View, the camera component supplies
Projection, and the resulting ViewProjection drives both rendering and culling.
WorldRenderer publishes the authored seed independently of the effective View
and leaves the authored transform and hierarchy intact.

The snapshot includes at most one directional light. WorldRenderer validates
the authored fields of all directional lights and publishes only an enabled
light. Its proxy stores the primitive ID, unit direction toward the light
(opposite the authored ray direction), and linear RGB radiance (color times
intensity). It derives the proxy from world-space authored values, independent
of both the light entity's hierarchy and the selected camera pose. On failure,
`InvalidDirectionalLight` and `MultipleDirectionalLights` report the primitive
ID of the offending light.

With an enabled light, `ShadowCasters` contains the mesh reference, world
transform and validated bounds for each mesh before main-camera culling removes
it from the visible list. This includes geometry without authored normals.
Renderer uses the caster bounds to fit the directional shadow map. See the
[solid geometry sample](SolidGeometry.md) for an off-camera caster with an
on-screen shadow.

When a light is enabled, a mesh with authored normals uses the inverse
transpose of the linear part of its world transform. The Core math helper
computes the cofactors in double precision and preserves the determinant sign for
mirrored transforms. It scales all coefficients by a common positive factor
for float storage. BasePass normalizes the transformed interpolated normal.
A non-finite transform or a determinant with magnitude at or below `1e-8`
times the product of the linear column lengths fails before frustum culling.
`InvalidNormalTransform` reports the failing mesh component index and primitive
ID. Meshes without authored normals and frames without an enabled light keep
the existing transform checks and use the unlit material formula.

Meshes are sorted by `(SortKey, MaterialAssetId, PrimitiveId.Scope,
PrimitiveId.Value)`, independent of dense storage order.

## Lifetime

The Demo constructs one snapshot on the `FRenderFrameScheduler::ExecuteFrame`
callback stack and passes it to `FSceneRenderer::RecordFrame`. The published
snapshot owns its scene data and debug strings and borrows the resolved asset
payloads. Renderer reads it synchronously during recording. Each subsequent
build resolves the currently registered assets.

Keep the provider and each referenced payload alive until its submitted frames
finish. The Demo drains frames before resizing the swapchain. `FSceneRenderer`
builds candidate attachments and Composite resources, then waits for submitted
frames before publishing and retiring its old targets. The shared BasePass
pipeline uses dynamic viewport and scissor, keeping material handles valid
across viewport sizes. Shutdown drains frame slots, releases the scene and
asset session, then destroys the renderer and device.

## Deliberate exclusions

Phase 6 remains synchronous: it does not add asynchronous asset loading, a
render thread, scripts, UI, editor behavior, occlusion culling, LOD, or a general
multi-pass/material framework. The snapshot is immutable and synchronously
consumed by Renderer during the frame callback.
