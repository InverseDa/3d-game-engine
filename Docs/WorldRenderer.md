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

Each mesh proxy copies:

- a `FRenderPrimitiveId` derived losslessly from World domain plus Entity
  generation/index;
- its owned debug name and sort key;
- `LocalToWorld` and a validated world-space AABB;
- whether the mesh has authored normals, plus `NormalToWorld` when lit;
- the persistent Material asset ID used by deterministic ordering;
- resolved vertex/index buffers, vertex/index counts, R16/R32 index format,
  graphics pipeline, opaque base color, sampled image, material binding, and pass mask.

A material with an authored color texture requires authored mesh UVs.
Snapshot extraction rejects incompatible pairs before culling and reports
the mesh index and primitive ID. GPU pointers remain borrowed through the
scene's renderer provider.

Mesh assets provide validated local-space AABBs. WorldRenderer transforms them
with the absolute linear matrix, so rotation and non-uniform scale remain
conservative. Default bounds are invalid sentinels; non-finite transforms or
unordered/non-finite bounds fail publication. The selected camera supplies View,
Projection, and ViewProjection matrices. WorldRenderer conservatively tests all
eight AABB corners in homogeneous clip space; plane contact is retained, fully
outside meshes are omitted, and non-finite clip coordinates fail publication.

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

When a light is enabled, a mesh with authored normals uses the inverse
transpose of the linear part of its world transform. WorldRenderer computes
the cofactors in double precision and preserves the determinant sign for
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
callback stack and Renderer reads it synchronously while recording. No World
pointer, registry pointer, resolved asset-object pointer, or borrowed
`StringView` is stored in the published snapshot. A later asset Replace is seen
only by the next build; an already published CPU snapshot is not rewritten.

GPU resource pointers are provider-borrowed, not snapshot-owned. Providers must
keep them alive until submitted frames finish. During resize, the Demo waits
for submitted frames, builds replacement attachments and Composite resources,
and publishes the complete replacement before destroying the old resources.
The shared BasePass pipeline uses dynamic viewport and scissor, so the same
material handle remains usable for every entity across viewport sizes.
Shutdown stops frame slots, releases the active scene and asset session,
then destroys the shared pipeline and device.

## Deliberate exclusions

Phase 6 remains synchronous: it does not add asynchronous asset loading, a
render thread, scripts, UI, editor behavior, occlusion culling, LOD, or a general
multi-pass/material framework. The snapshot is immutable and synchronously
consumed by Renderer during the frame callback.
