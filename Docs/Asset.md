# Asset module

## Current scope

The Asset module is the Phase 6 Task 19 synchronous identity and loaded-object
registry foundation. It depends only on Core and provides:

- persistent `FAssetId` and logical `FAssetTypeId` values;
- compact typed runtime handles;
- canonical virtual soft paths;
- a synchronous, allocator-backed `FAssetRegistry`;
- explicit borrowed or provider-thunk-owned object lifetime.

It does **not** perform file I/O, asynchronous loading, importing, dependency
loading, filesystem watching, serialization, or hot reload. It has no World,
Reflection, RHI, Renderer, or GPU dependency.

## Persistent identity and runtime handles

`FAssetId` is `TStableId<FAssetIdTag>` and is the persistent identity of one
asset. `FAssetTypeId` uses a different `TStableId` domain and identifies the
logical asset kind. Neither is a runtime slot.

Providers specialize `TAssetTypeTraits<T>::GetTypeId() noexcept`. The function
must return a stable, valid `FAssetTypeId` value derived from fixed UUID bytes;
it must never depend on a DLL-local address or initialization order. Missing,
wrong-return-type, and potentially-throwing specializations produce a compile
diagnostic. A specialization that returns an invalid value is rejected by the
registry.

`TAssetHandle<T>` contains only:

```text
FAssetSlotHandle (uint32 index + uint32 generation)
uint64 registry domain
```

The value is 16 bytes, trivially copyable, copyable, equality comparable, and
hashable within the same `T`. Handles of different asset types are not
comparable. Generation rejects stale slot reuse and the non-reused runtime
registry domain rejects handles from another registry. The handle contains no
persistent ID, type ID, soft path, pointer, or ownership state.

Runtime handles are never serialized. Persistent data stores an `FAssetId` or
`FSoftAssetPath`, then uses `TryFindById` or `TryFindByPath` to acquire the
current runtime handle. `TryGetAssetId` and `TryGetInfo` recover persistent
metadata from a validated typed handle.

## Virtual soft paths

`FSoftAssetPath` is an allocator-owned, canonical UTF-8 byte string. It is a
logical path, not an operating-system path. A valid path has at least a mount
and an asset segment, for example both of these are valid:

```text
Game/Meshes/Cube
Game/Meshes/Cube.mesh
```

Canonicalization applies the same rules on every platform:

- `\` becomes `/` and repeated separators collapse;
- `.` segments disappear;
- `..` removes one ordinary segment but cannot escape the mount root;
- leading separators, UNC paths, drive/scheme colons, trailing directory
  separators, embedded NUL/control/space bytes, and non-portable ASCII
  punctuation are rejected;
- non-ASCII input must be a valid UTF-8 scalar sequence, rejecting overlong
  sequences, surrogate encodings, and values above U+10FFFF;
- extensions are optional;
- identity is bytewise and case-sensitive.

The module deliberately performs no Unicode NFC normalization or case folding.
Consequently `Game/A` and `game/A` are different identities, as are distinct
valid UTF-8 normalization forms. Calls are transactional: invalid input or OOM
preserves the previous `FSoftAssetPath` value. Lookup canonicalizes its input
through the same implementation used by registration.

## Registry operations

`FAssetRegistry` is non-copyable, non-movable, synchronous, non-reentrant, and
not thread-safe. Its owner must externally serialize all access. A failed PImpl
allocation leaves an inert C++ object whose operations return
`InvalidRegistry` and whose count is zero.

Registration requires a valid persistent ID, a valid provider type trait, a
canonicalizable path, a non-null object, and a non-empty provider name. The
registry deep-copies soft path, display name, and provider name. Persistent ID,
canonical path, and object address are each globally unique within a registry;
duplicates are rejected rather than merged.

`Resolve` exposes only `const T*`: Task 19 assets are immutable to consumers.
`FAssetInfo` contains value IDs, borrowed string views, and ownership metadata;
it does not expose mutable object ownership. Every successful Register,
Replace, or Unregister conservatively invalidates all previously returned
object pointers and info views. A failed operation invalidates neither.

Replace is an explicit synchronous object replacement, not hot reload. It must
retain the existing `FAssetId` and `FAssetTypeId`, may change path and metadata,
requires a new object address different from the old object and every other
entry, advances generation, and returns a new handle. The previous handle is
stale. Descriptor ID/type validity is diagnosed before handle validity;
otherwise runtime validation reports invalid, foreign, stale, then type
mismatch. Failed replacement preserves the old handle, object, metadata,
generation, indices, output, and both ownership obligations.

Unregister removes all indices, invalidates the slot, then performs provider
cleanup. Slot reuse advances generation. A generation at its maximum is
retired rather than wrapped.

## Ownership and provider unload

Registration has two explicit ownership modes:

- `Borrowed`: the destroy thunk must be null. The provider owns the object and
  keeps it alive until successful Unregister or registry destruction.
- `OwnedByProviderThunk`: the provider supplies
  `void (*)(void* ProviderContext, void* Object) noexcept`. Ownership transfers
  only after successful Register or Replace.

The Asset module never directly applies `delete` to a provider object and never
casts between incompatible function pointer signatures. Successful Replace,
Unregister, and registry destruction invoke the creator-side thunk exactly once
for each owned object. Remaining owned objects are destroyed in reverse
registration/replacement order. Providers must not infer an asset dependency
graph from this cleanup order.

Provider context, object addresses, and executable thunk addresses remain
borrowed across the DLL boundary. Every associated asset must be unregistered,
or the entire registry must be destroyed, before that provider DLL unloads.
Cleanup runs under the registry mutation guard; reentrant registry calls return
`InvalidState`.

## Allocation transaction

Registry-owned entries, strings, slot/free/order arrays, and ID/path/object
indices use the registry allocator. Register and Replace first canonicalize and
deep-copy all metadata, prepare owning path-index keys, and reserve every
commit container. Only then are no-allocation index insertions performed and a
generation published. Reliable rollback removes any earlier prepared index if
an unexpected later insertion fails.

At every allocation failure point:

- the object is not consumed and no destroy thunk runs;
- outputs, asset count, lookup results, slot liveness, and generation are
  unchanged;
- successful earlier registry entries and their borrowed views remain valid.

An Unregister that cannot track a reusable free slot still completes safely and
retires that slot.

## Render asset identities

Task 20 adds `RenderAssetTypes.h`, which declares opaque `FMeshAsset` and
`FMaterialAsset` logical types, their 16-byte typed handle aliases, and two
fixed UUID-backed type traits. The exported type-ID accessors ensure every DLL
observes the same value identity. Asset remains Core-only: it never completes
either payload and contains no RHI or Renderer fields.

Renderer uniquely completes the GPU-facing payload definitions. World depends
only on Asset and stores the typed handles; Demo owns the synchronous registry
and borrowed payload objects. WorldRenderer resolves each handle afresh into an
immutable frame snapshot and never retains registry object pointers across
Replace. In Task 22 the Mesh payload provides a borrowed vertex buffer/count,
R16/R32 index buffer/count/format, and local bounds. The minimum Material payload
provides a borrowed graphics pipeline, SceneColor pass mask, and finite
non-negative RGB base color. SortKey remains World-authored on the mesh component
and is copied into the render proxy. These remain Renderer-owned definitions; Asset does
not gain RHI knowledge. Task 22 owns the first real BasePass, not a general asset
loader or material system.
