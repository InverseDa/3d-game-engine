# WorldRenderer module

## Responsibility and dependency direction

WorldRenderer is the downstream bridge between committed gameplay state and
Renderer data. Its public dependency set is exactly Core, Asset, Renderer, and
World. World remains unaware of Renderer/RHI, Renderer remains unaware of
World, and Asset remains Core-only.

`FWorldRenderer::BuildSnapshot` accepts an Idle `FWorld`, the synchronous
`FAssetRegistry`, an explicit frame/viewport build context, and an output
`FWorldRenderSnapshot`. It updates
the World's private Transform cache, validates the paired dense Entity/Mesh
component spans, resolves typed Mesh/Material handles, and builds a complete
candidate before publication.

## Snapshot contract

`FWorldRenderSnapshot` is non-copyable and noexcept movable. Consumers can only
read its frame index, publication state, and const `FRenderScene`. Its arrays
and owned debug strings use the allocator supplied when the snapshot is
constructed. A failed build always leaves it empty and unpublished, including
when replacing a previously successful result.

Each mesh proxy copies:

- a `FRenderPrimitiveId` derived losslessly from World domain plus Entity
  generation/index;
- its owned debug name and sort key;
- `LocalToWorld` and a validated world-space AABB;
- the persistent Material asset ID used by deterministic ordering;
- resolved vertex/index buffers, vertex/index counts, R16/R32 index format,
  graphics pipeline, minimum opaque base color, and pass mask.

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
explicit projection Y flip. Meshes are sorted by `(SortKey, MaterialAssetId,
PrimitiveId.Scope, PrimitiveId.Value)`, independent of dense storage order.

## Lifetime

The Demo constructs one snapshot on the `FRenderFrameScheduler::ExecuteFrame`
callback stack and Renderer reads it synchronously while recording. No World
pointer, registry pointer, resolved asset-object pointer, or borrowed
`StringView` is stored in the published snapshot. A later asset Replace is seen
only by the next build; an already published CPU snapshot is not rewritten.

GPU resource pointers are provider-borrowed, not snapshot-owned. Providers must
keep them alive until submitted frames finish. The Demo waits all frame fences
only for resize/shutdown, builds every replacement attachment/binding/pipeline
as a candidate, Replace-publishes the inactive Material payload, switches the
Idle World handle, swaps candidates, and only then destroys old resources.
Shutdown clears World, unregisters assets, stops frame slots, and only then
destroys GPU resources.

## Deliberate exclusions

Phase 6 remains synchronous: it does not add asynchronous asset loading, a
render thread, scripts, UI, editor behavior, occlusion culling, LOD, or a general
multi-pass/material framework. The snapshot is immutable and synchronously
consumed by Renderer during the frame callback.
