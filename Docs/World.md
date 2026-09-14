# World module

## Current scope

World now owns the Phase 5 Task 15 identity/storage foundation, Task 16
Transform hierarchy, Task 17 tick/deferred-mutation boundary, and Task 18
single-threaded native gameplay schedule:

- `FWorld` creates and destroys `FEntity` values.
- `FEntity` combines a pool-issued index+generation slot with a nonzero runtime
  World domain. Generation rejects stale reuse; the domain rejects a handle from
  another World even when its slot pair is identical.
- `TComponentStorage<T>` keeps one contiguous dense `T` array, a parallel dense
  Entity array, and a sparse Entity-index-to-dense-index table.
- `FWorldMeshComponent` is the first real storage consumer. It retains only
  typed 16-byte Mesh/Material runtime asset handles plus world-authored debug
  name and sort key; no RAL or Renderer type crosses the World boundary.
- `FWorldCameraComponent` is a logical `FWorldPrimitive` with active/priority,
  vertical FOV, near and far planes. Its built-in typed storage follows the same
  Reset/Destroy lifetime as mesh and Transform storage and contains no Renderer
  or RAL types.
- `FTransform` is a standard-layout, trivially-copyable local TRS value. World
  owns its component storage privately and exposes only dirty-aware authored
  mutation and value-copy query APIs.
- Parent/child links use generation-checked Entity values, reject cycles, and
  update cached world matrices iteratively in parent-before-child order.
- `BeginTick` / `EndTick` publish deferred structural commands at deterministic
  boundaries while keeping the Updating snapshot structurally stable.
- Explicitly registered native systems execute in deterministic dependency
  order across fixed and variable update phases.

Scripts and jobs/Runnable execution are later roadmap tasks and intentionally
absent here. WorldRenderer is a separate downstream module; World itself still
does not know Renderer or RAL. Task 20 makes World depend only on the
Core-only Asset module: `FMeshAsset` and `FMaterialAsset` remain opaque here,
while Renderer uniquely completes their GPU-facing payload definitions.
Vertex count moved to the Mesh payload and prototype pass intent moved to the
Material payload. The former World-owned and Demo-private render-scene
extractors are both removed; the formal Task 21 bridge lives in WorldRenderer.

## Entity identity and lifetime

`FEntity` is a trivially copyable, non-owning value. Invalid values use world
domain zero or the invalid `TRuntimeHandle` sentinels. A World domain is issued
once per process and is never reused; exhaustion makes subsequently constructed
Worlds operationally invalid rather than wrapping.

The value keeps its slot fields visible for diagnostics and hashing, so native
code can physically forge bytes. Those bytes are never authority: every World
operation checks the domain and then asks its private handle pool whether the
index+generation pair is currently alive.

`FWorld::TryCreateEntity` preserves its output and entity count on allocation
failure. It reserves the dense live-Entity list before asking the handle pool to
issue a slot, so a failed create does not consume a generation. Destroying an
Entity removes all of its attached components while it is still alive, releases
the slot, and swap-removes it from the live list. Stale, double-destroyed,
forged-generation, and foreign-World values are rejected.

`FWorld` is non-copyable, non-movable, and not thread-safe. Its implementation
and allocator-backed ownership live behind a World-module PImpl. PImpl
allocation failure still leaves a valid C++ object, but `IsValid()` is false,
entity operations fail, counts are zero, and storage accessors return null.

## Typed contiguous storage

Each `TComponentStorage<T>` owns one allocator block with checked aligned
regions for:

1. sparse entries indexed by Entity slot index;
2. dense `FEntity` values;
3. dense `T` values with `sizeof(T)` stride.

Growth computes every offset and byte count before one aligned allocation.
Only after allocation succeeds are existing nothrow-movable components
relocated and the new block committed. Consequently an allocation-failed Add
does not consume its rvalue, move existing components, alter dense order, change
the exposed data address, or advance the mutation version. Over-aligned
components retain their required real address alignment.

Removal uses swap-remove. The last component is move-constructed into the hole,
the old objects are destroyed exactly once, the parallel dense Entity is moved,
and that Entity's sparse entry is repaired. `GetEntities()` and
`GetComponents()` have equal sizes and matching indices; iteration order is
dense but intentionally unstable after removal.

Sparse mutation helpers are private to `FWorld`. Callers cannot insert directly
into a storage and therefore cannot use a stale or forged generation to
overwrite the sparse mapping invariant.

The component API is explicit about both World and storage:

```cpp
LE::FWorld World;
LE::FEntity Entity;
auto* Meshes = World.GetMeshComponents();

if (World.IsValid() && Meshes != nullptr && World.TryCreateEntity(Entity))
{
    LE::FWorldMeshComponent Mesh;
    World.TryAddComponent(*Meshes, Entity, std::move(Mesh));
}
```

Generic external storage is attached without allocation:

```cpp
LE::TComponentStorage<FGameplayComponent> Components;
World.AttachComponentStorage(Components);
```

Registration is a non-owning intrusive node. World never deletes an external
storage. Storage destruction detaches itself in O(1); World destruction clears
and detaches every surviving external storage, after which its World-mediated
operations report `InvalidStorage`. In Editor DLL configurations, the module
which instantiated a component storage supplies its remove/clear thunks and
must remain loaded until the storage is detached. No allocation is freed by a
foreign module or raw cross-DLL `delete`.

## Operation semantics

`TryAddComponent` returns `Added`, `AlreadyPresent`, `InvalidEntity`,
`InvalidStorage`, or `AllocationFailed`. Duplicate Add does not consume its
argument or replace the existing value. `TrySetComponent` returns `Added` or
`Replaced` on success and supports move-only values. `GetComponent` returns null
and `RemoveComponent` reports an explicit non-success result for missing,
invalid, stale, foreign, or detached inputs.

Every successful Add, Replace, Remove, or Clear invalidates all pointers, spans,
and iteration positions borrowed from that storage and advances its mutation
version. Duplicate, missing, invalid, and allocation-failed operations do
neither. Structural operations and component construction/destruction are
non-reentrant; concurrent access requires synchronization by the caller.

## Transform and hierarchy

`FTransform` stores local translation, quaternion rotation, and scale. Authored
rotations are normalized; a zero-length quaternion canonicalizes to identity.
Scale may contain zero because Task 16 never performs an inverse. Core matrices
use row-major physical storage and column vectors, so a local matrix is
`Translation * Rotation * Scale` and a child world matrix is
`ParentLocalToWorld * ChildLocalMatrix`. Caching the full matrix preserves exact
affine composition when a rotated hierarchy also contains non-uniform scale.

Transform storage is an internal `TComponentStorage` owned by `FWorld::FImpl`.
It is deliberately not returned as a mutable pointer or span: callers could
otherwise change local data or hierarchy links without dirty propagation. The
controlled API provides Add/Remove, local edit, Set/Clear parent, value-copy
local/parent/world queries, explicit cache update, and a read-only authored
mutation version.

All query outputs remain unchanged on failure. A world-matrix query resolves
dirty caches first, but fails while another World structural mutation is in
progress so it cannot observe a half-committed hierarchy. Cache resolution is
derived work and does not advance the authored mutation version. Each
successful Add, local edit, reparent, Remove, or Entity destroy containing a
Transform advances it exactly once; unchanged, rejected, missing, foreign,
stale, allocation-failed, and cache-only operations do not. An operationally
invalid World reports version zero.

Parent, first-child, previous-sibling, and next-sibling are stored as `FEntity`
links rather than pointers, so component storage growth and swap-removal cannot
invalidate them. Reparenting prepends to the new parent's child list. Sibling
order is deterministic for a given mutation sequence but is not a stable sort
contract.

Task 16 supports only **KeepLocal** hierarchy edits. Reparent and clear-parent
leave local TRS unchanged and recompute world matrices. KeepWorld is not
offered: Core does not yet define affine inverse/decomposition, and decomposing
non-uniform scale plus rotation may be lossy due to shear. Removing a Transform
or destroying its Entity first unlinks it, makes each direct child a root with
its local value unchanged, dirties those child subtrees, then removes the
component. Entity slot release happens afterward, so generation reuse cannot
inherit stale hierarchy links.

Cycle detection walks the proposed parent's ancestor chain. Dirty propagation
and cache update both walk the intrusive hierarchy iteratively without a call
stack or scratch allocation; roots are discovered in dense Transform order and
each subtree is evaluated parent before child. Only Add may allocate, and its
OOM behavior is inherited from transactional typed storage. Reparent, local
edit, Remove, cycle detection, dirty propagation, and cache update allocate
nothing.

## Tick pipeline and deferred structural mutation

`FWorldTickContext` copies frame index, delta seconds, and elapsed seconds from
the Application layer without making World depend on the Application module.
The first accepted frame index may be any value. Later frame indices must be
strictly increasing, elapsed time may not move backward, and both time values
must be finite and nonnegative. Invalid context/state leaves the current
context, command queue, tick state, and optional flush-result output unchanged.

The P0 pipeline places the native schedule inside one Task 17 boundary:

```text
Idle/Render records -> BeginTick FIFO flush -> Transform resolve -> Updating
Updating             -> fixed substeps -> one variable pass
Updating records     -> EndTick FIFO flush -> Transform resolve -> Idle/Render
```

Commands recorded after `EndTick`, including during Render, therefore become
visible at the next `BeginTick`. Commands recorded during Updating become
visible at the same frame's `EndTick`, before Render. A Begin boundary that
reports command failures still completes Transform resolution and enters
Updating; its caller must close the accepted tick with `EndTick`.

Immediate structural APIs, `Reset`, and storage attachment reject Updating and
Flushing. Value queries read the last committed snapshot and world-matrix
queries may resolve its dirty cache. During a flush, only the individual
command execute thunk may temporarily enter structural mutation. Nested record,
resolve/discard, immediate mutation, and payload-lifecycle reentry are rejected;
payload destruction occurs after that execute permission has been removed.

The World-owned `FWorldCommandBuffer` records Entity create/destroy, generic
typed component Add/Set/Remove, and authored Transform/hierarchy commands.
`FDeferredEntityToken` is not an Entity. It addresses a generation-checked
result slot so later FIFO commands can depend on a deferred Create. After the
boundary, resolution consumes a successful/failed/dead result slot exactly
once; pending, creation-failed, no-longer-alive, invalid-token, and invalid-state
outcomes are distinct. Foreign and stale tokens both map to `InvalidToken`, and
every failed resolution preserves its Entity output.

Validation occurs both while recording and immediately before execution.
Conflicts are not merged: FIFO state is authority. Thus Destroy followed by Add
fails the Add, Add followed by Set replaces, Add followed by Remove removes,
and hierarchy cycle checks see every earlier command's committed result.
Create failure marks its token failed and dependent commands report dependency
failure without side effects.

Record allocates every fallible node/token resource before copying or moving a
typed payload; OOM does not append, consume an rvalue, issue a token generation,
or alter outputs. Flush is atomic per command, not per batch: it preserves a
successful prefix, reports the first failing FIFO index and reason, and keeps
executing independent suffix commands. This is required for move-only generic
components, which cannot be transactionally cloned for whole-batch rollback.
An rvalue that aliases the target storage's current dense array is rejected
before it is moved, because consuming it while recording would mutate the
committed Updating snapshot before the boundary. A copied alias is safe because
the command owns its independent snapshot.

Command nodes and payload bytes use the World's allocator. Typed execute and
destroy thunks are instantiated by the module that records the component type.
That provider module and every borrowed external component storage must remain
loaded/alive through flush or explicit World `Reset`; storage detach while the
World is non-idle or while any command is pending is a contract violation.
Reset and World destruction cancel queued payloads/tokens before clearing
storage and Entities. Reset also forgets the accepted tick context, so the next
Begin may establish a fresh timeline from any frame index.

## Native gameplay systems

Native gameplay scheduling is owned by `FWorld::FImpl` and exported through
`FWorld`; World still has no dependency on Application. Registration is
explicit per World. There is no static registry, discovery hook, scripting VM,
Runnable, or worker-thread execution.

`FNativeGameplaySystemDescriptor` contains a stable name, a Fixed or Variable
phase, explicit dependency names, an opaque provider context, and a
`noexcept` C-style update thunk. World copies names and dependency names with
its allocator. The context and executable thunk remain borrowed: an Editor DLL
provider must unregister before unload and must keep its code/context alive
through every callback. World never invokes provider callbacks while
unregistering, resetting, or destructing. Generic deferred payload thunks from
Task 17 independently require their provider DLL and external storage to remain
loaded until `EndTick`, `Reset`, or World destruction consumes/cancels them.

Registration validates descriptor shape and duplicate identity immediately,
but permits a forward dependency. After all registrations, callers explicitly
call `CompileNativeGameplaySystemSchedule`. Compilation reports missing,
cross-phase, and cyclic dependencies. Cross-phase edges are rejected: the
fixed-before-variable phase barrier is already implicit, and a reverse edge is
impossible. Within each phase, Kahn topological order chooses the
lexicographically smallest ready system, so registration order is irrelevant.
Registration and compilation are transactional on allocation failure. A graph
mutation marks the old plan dirty, and a dirty plan cannot execute even though
its previous arrays remain internally intact.

Register, unregister, enable/disable, configure, and compile are Idle-only.
`UpdateNativeGameplaySystems` is Updating-only and accepted at most once per
`BeginTick`. Callback reentry into scheduler mutation or another update is
rejected. Disabled callbacks are skipped; their dependents still run because a
dependency is an ordering constraint, not an enablement contract. Fixed steps
still consume time when their phase is empty or every fixed system is disabled.

The required call sequence is:

```cpp
World.BeginTick(FrameContext);
World.UpdateNativeGameplaySystems(); // all fixed steps, then one variable pass
World.EndTick();                     // publishes every recorded command
```

Callbacks receive only native World/command-buffer pointers and timing values.
They can read the committed snapshot and record structural commands. No flush
occurs between fixed substeps or before the variable pass. Immediate structural
mutation remains rejected during Updating; all recorded commands become
visible together at `EndTick` in Task 17 FIFO order.

Fixed configuration defaults to 1/60 second and four substeps. `MaxSubsteps`
must be in `[1, MaxNativeGameplayFixedSubsteps]`, where the public hard limit is
1024; `FixedDeltaSeconds * MaxSubsteps` must remain finite. Configuration resets
the accumulator, fixed simulation time, and index timeline. Each frame uses an
overflow-safe finite saturated sum of accumulator plus frame delta, caps it to
`FixedDeltaSeconds * MaxSubsteps`, and reports the finite representable excess
as dropped time. Due-step calculation uses a documented 64-epsilon ratio
tolerance to avoid losing an exact boundary to division rounding.

Each due step is consumed immediately before its callbacks: one FixedDelta is
removed, the unique fixed index is issued, and fixed simulation time advances.
The first context therefore has fixed index 0, substep index 0, and end-of-step
time FixedDelta. A callback failure stops all later callbacks, substeps, and the
Variable phase. The failed fixed step remains consumed so successful-prefix
effects are not replayed; due steps not yet attempted remain in the accumulator.
The caller must still call `EndTick`, which commits commands already recorded.

`LastFixedTickIndex` is an inclusive configuration bound and defaults to the
largest `uint64`. Its last unique value is issued once. If another due step
needs an index, the accepted update reports `TimelineExhausted`, consumes no
additional step, skips Variable, and still requires `EndTick`; it never wraps or
repeats an index. Reset/configuration starts a fresh index timeline.

Invalid World/state/dirty-schedule calls preserve the optional update report.
An accepted success, system failure, or timeline exhaustion commits a report.
Its failed-system name and the compile diagnostic name views borrow
registry-owned bytes; callers must not retain them across successful
register/unregister or World destruction.

The demo registers private `Demo.RotateTriangle` in Variable phase. After the
bootstrap token resolves, the system reads the mesh Entity's local Transform,
computes an absolute rotation from frame elapsed time, and records a Transform
update. The old value remains visible until `EndTick`; Render then consumes the
existing committed World snapshot. The callback has no renderer, RAL, asset, or
script dependency.

The demo is the first real consumer: its render runtime owns one persistent
World and `FAssetRegistry`, registers borrowed Renderer-owned Mesh/Material
payloads, records their handles with the Entity/Transform bootstrap commands,
and Render reads only the committed World. Tick, command, or asset-resolution
failure closes the accepted work and exits instead of dereferencing invalid GPU
state.

## Rendering bridge

WorldRenderer performs the formal one-way bridge:

```text
FWorld mesh handles + private Transform hierarchy + FAssetRegistry
        -> paired dense Entity/FWorldMeshComponent spans
        -> synchronous Mesh/Material payload resolution
        -> LocalToWorld matrix or identity fallback
        -> immutable frame-owned FWorldRenderSnapshot / FRenderScene proxies
        -> Renderer
```

It accepts only an Idle committed World, validates paired dense spans and
Transform caches, resolves both typed handles, and immediately copies owned
debug text, resolved GPU handles/scalars, LocalToWorld, stable primitive ID, and
the transformed world AABB, indexed draw data, opaque base color, and selected
camera matrices into an allocator-owned candidate. Only a complete
candidate is published; missing, stale, foreign, type-mismatched, invalid, or
OOM input leaves the output snapshot empty and unpublished. No registry object
pointer or borrowed metadata view survives the call.

The snapshot is a stack value in the frame callback and remains const while
Renderer synchronously records commands. GPU resources remain provider-owned;
on resize the Demo waits all actual frame fences, builds a replacement pipeline
in the inactive borrowed Material slot, calls `FAssetRegistry::Replace`, switches
the idle World component to the new handle, and only then destroys the old
pipeline. Task 22 now consumes this snapshot through the actual BasePass,
indexed draw, depth, camera, culling, and deterministic sorting contract.
