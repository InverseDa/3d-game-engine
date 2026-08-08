# Core allocation and containers (C1-C3)

This document records the implemented C1-C3 allocation, container, and string
contracts under ADR-0001. `Array`, `HashMap`, `HashSet`, and `String` own their
storage and engine ABI; none aliases or wraps a Standard Library owning
container.

## Allocation and ownership route

- `IAllocator::Allocate(size, alignment)` returns `nullptr` for failure and for
  zero bytes. `Deallocate` must be called on the same allocator instance.
- `AllocatorRef` is the default `Array` allocator policy. It stores the exact
  `IAllocator*` that created the storage; it does not look up the process default
  again during destruction. A custom allocator object must outlive every
  allocation and container that refers to it.
- `GetDefaultAllocator()` returns a process-lifetime Core allocator. It uses
  `_aligned_malloc/_aligned_free` on MSVC and `posix_memalign/free` elsewhere.
  Requested alignment is a non-zero power of two; the backend raises smaller
  valid alignments to the platform minimum without weakening the caller's
  element-alignment guarantee.
- Across a DLL boundary, an owning object must either retain this allocator
  route or be destroyed through an exported create/destroy pair in its creating
  module. A raw `new` on one side and `delete` on the other remains forbidden.
- `TryMultiplySize`, `TryAllocateBytes`, and `TryAllocateArray` centralize
  overflow, zero-allocation, and alignment behavior. Invalid alignment is a
  contract violation. Size/capacity overflow and allocator exhaustion return
  failure from `Try*` APIs before mutating the container.
- Ordinary mutators (`Reserve`, `Shrink`, `PushBack`, `EmplaceBack`, `Insert`)
  call the single non-returning `HandleOutOfMemory` path. Tests can replace the
  handler with `SetOutOfMemoryHandler`; production code must not install a
  returning handler. Contract failures use the corresponding centralized
  contract handler.

## `Array<T, Allocator>`

The Release object owns an element pointer, size, capacity, allocator policy,
and a storage generation. Debug builds additionally own a parallel iterator
token allocation used only to diagnose operation-point invalidation and stale
end iterators. This diagnostic storage is deliberately absent in Release; the
project does not promise ABI compatibility between Debug and Release.

Growth is geometric (approximately 1.5x), monotonic, and capped before any
capacity arithmetic can overflow. Its exact sequence is not API. `Reserve(N)`
guarantees no further allocation while size remains at most `N`. Empty arrays
have `Data() == nullptr`, make no allocation, and do not use an allocation's
address as identity. `Shrink` moves storage only when capacity changes.

Relocation uses `IsBitwiseRelocatable<T>`. The default is true only for
trivially-copyable types; an explicit specialization is an ownership promise
that raw relocation ends the source lifetime without a destructor call.
Non-bitwise types are move-constructed and the source elements are destroyed.
Erase always destroys the logically removed elements, including non-trivial
types that explicitly opt into bitwise relocation.

Invalidation rules are:

- reallocation, growing `Reserve`, and moving `Shrink` invalidate all iterators,
  pointers, references, and spans;
- append without reallocation preserves earlier element references and
  iterators, but invalidates the old end iterator;
- insert and erase invalidate the operation point and everything after it;
- `Clear` invalidates every element view.

Debug iterators detect those rules with per-position tokens, including multiple
successive mutations (an invalid iterator cannot become valid again). Release
iterators retain normal bidirectional traversal but omit this diagnostic cost.
Bounds, empty dereference, iterator misuse, invalid alignment, and invalid
subspans are diagnosed through the contract handler in Debug. Overflow checks
needed to prevent memory corruption remain active in Release.

The named `TryReserve`, `TryShrink`, `TryResize`, `TryPushBack`,
`TryEmplaceBack`, and `TryInsert` paths leave array storage, size, capacity, and
existing values valid when allocation fails. `Resize` value-initializes growth;
its fill overload stages the fill value before reallocation so a value borrowed
from the same array remains valid. Ordinary variants terminate through the OOM policy.
Element construction/move/copy is expected not to throw, as specified by
ADR-0001; C1 does not provide an exception guarantee for throwing element types.

## `StaticArray` and `Span`

`StaticArray<T, N>` is an inline fixed-capacity value. `StaticArray<T, 0>` has
no dummy element and returns null data. Its addresses remain stable only for its
own lifetime; moving or relocating the owner invalidates borrowed views.

`Span<T>` stores only a pointer and element count. It never owns or extends
lifetime, and its `AsSpan` creation helpers are lvalue-qualified so a temporary
owner cannot silently produce a dangling view. Mutable spans convert to const
spans, not the reverse. Empty spans are `{nullptr, 0}`. `Subspan` checks its
range, and `TrySizeBytes` reports multiplication overflow without wrapping.

## Verification surface

The formal `LimitlessTests` target includes Core and exercises constructor and
destructor balance, move-only types, copy/move, self-aliasing, growth,
reserve/shrink, insert/erase/clear, iterator invalidation history, over-aligned
types, overflow, failing allocators, spans, and zero-length static arrays.
Separate process probes verify that bounds/iterator contract failures and the
ordinary-mutator OOM path terminate with a non-zero status.

## Hash containers (C2)

`LE::HashMap<K, V, Hash, Equal, Allocator>` and
`LE::HashSet<K, Hash, Equal, Allocator>` own engine-defined storage and ABI.
They do not alias or wrap `std::unordered_map`/`std::unordered_set`.
`HashSet` shares the engine `HashMap` bucket implementation with an empty mapped
value so both containers have the same probing, failure, and invalidation rules.

### Buckets, probing, and capacity

- Storage is an allocator-owned, power-of-two bucket array. Each bucket contains
  the required `Empty`/`Occupied`/`Tombstone` state and aligned in-place entry
  storage. Release has no per-bucket debug generation or token allocation.
- Lookup and insertion use linear open-addressing. Every probe is bounded by the
  bucket count, including constant-hash input and a table containing only
  tombstones, so malformed/adversarial distributions cannot create an infinite
  loop. Deleted slots are reused by later insertions.
- The maximum live load factor is `0.7`. The precise capacity sequence remains
  an implementation detail. `Reserve(elementCount)` reserves for live entries;
  `Rehash(bucketCount)` requests a bucket count and rounds safely to the next
  supported power of two. All multiplication, load, rounding, and growth paths
  reject overflow before allocation or probing.
- `Insert` rejects duplicate keys without replacing the value.
  `InsertOrAssign` reports whether it inserted or updated. The corresponding
  `Try*` insertion calls return `EHashInsertResult`, distinguishing
  `Inserted`, `Updated`, `AlreadyExists`, and `AllocationFailed`.
- `Erase` destroys the entry and leaves a tombstone. `Clear` destroys all live
  entries and resets every bucket to empty. Iteration visits each live entry
  exactly once, but its order is deliberately unstable and must never be used
  for serialization, networking, golden output, or deterministic hashing.

### Hash and equality policies

`DefaultHash` is intentionally defined only for approved integral, enum, and
pointer keys in C2. String content hashing arrives with C3. Business types must
provide explicit `Hash` and `Equal` policies, and those policies may carry state
through the hash-container constructor. Hash, equality, and allocator policies
must be non-throwing movable/swappable; this is enforced at instantiation rather
than hidden behind unconditional `noexcept`. Their required invariant is:
`Equal(A, B) == true` implies `Hash(A) == Hash(B)`.

C++17 heterogeneous `Find`, `Contains`, and `Erase` participate only when both
policies declare `is_transparent` and are callable for the query/key pair. This
allows a view-like query to probe stored business keys without constructing a
temporary key. C2 tests use a custom key/view pair and do not pre-implement C3
strings.

### Ownership, failure, and invalidation

The allocator policy defaults to `AllocatorRef` and retains the exact
`IAllocator` identity used for bucket allocation. Copy construction copies the
allocator route; move transfers it. Assignment replaces the destination with a
fully constructed source-owned temporary, so destruction always returns through
the matching allocator.

Named `TryReserve`, `TryRehash`, and allocation-requiring `TryInsert` paths leave
the existing map/set valid and unchanged when bucket allocation fails. Ordinary
mutators route allocation failure to the centralized non-returning OOM handler.
Insertion allocates replacement buckets before staging a new key/value, so an
allocation failure cannot move-from even an internally aliased rvalue. It then
stages after duplicate detection and before rehash, so a mapped value borrowed
from the same map remains valid through forced growth; a rejected duplicate does
not consume its rvalue arguments. As established by
ADR-0001, element/policy construction, assignment, hash, equality, and move are
expected not to throw. C2 does not promise a strong exception guarantee for
throwing user types; such types are outside exception-free container paths.

Any insertion or rehash may invalidate every iterator, pointer, and reference;
erase invalidates at least the removed entry, and `Clear` invalidates all.
Debug iterators retain a storage generation and diagnose stale dereference or
past-end misuse through the Core contract handler. Release omits this diagnostic
generation from both containers and buckets while retaining bounded probes and
overflow checks needed for memory safety and termination.

The `LimitlessTests` target covers basic update/find/erase/clear, copy/move and
move-only entries, reserve/rehash/load, constant-hash collisions, tombstone
reuse, duplicate keys, all-tombstone termination, heterogeneous queries,
unordered iteration completeness, stateful policies, allocator identity,
failing/overflow allocation, ordinary OOM, stale iterators, and HashSet parity.

## String and StringView (C3)

`LE::String` owns allocator-backed byte storage and always reserves one extra
byte for a trailing NUL. `Data()` therefore returns a non-null NUL-terminated
pointer even for an empty string. Size/Length counts content bytes and excludes
that terminator. Embedded NUL bytes are preserved and participate in equality
and hashing. The P0 text contract is UTF-8, but Core does not validate, normalize,
segment, or count Unicode scalar values; byte-level construction is available
for I/O and interoperability.

`LE::StringView` is the corresponding borrowed `{pointer, byte count}` view. It
does not own, extend lifetime, promise NUL termination, or validate UTF-8. Both
`{nullptr, 0}` and `{non-null, 0}` are valid empty views and compare equal;
non-zero size requires non-null data. Owner destruction, assignment, growth,
or another mutation that changes referenced bytes invalidates a view. A String
append without reallocation preserves views into the previous prefix only when
the referenced bytes themselves are not overwritten.

`TryReserve`, `TryAssign`, and `TryAppend` leave the original string unchanged
when allocation or capacity arithmetic fails. Ordinary forms route failure to
the non-returning Core OOM handler. Assign and append accept overlapping/self-
aliased views; reallocation copies before releasing old storage, and in-place
operations use overlap-safe moves. `SIZE_MAX` is rejected before terminator
addition, allocation, or source access.

`StringHash` and `StringEqual` are transparent content policies shared by the
`DefaultHash`/`DefaultEqual` specializations for String and StringView. Equal
String/StringView byte sequences therefore produce identical hashes, including
embedded NUL, and `HashMap<String, V>` can query with StringView or a
NUL-terminated `const char*` without allocating a temporary String.

`StringView::Compare` and the String/StringView ordering operators compare the
full byte sequence with `memcmp`, then order a shorter equal prefix first. The
ordering is therefore deterministic and embedded-NUL safe; it is byte ordering,
not locale-aware Unicode collation.

C6 removed the old global `FString` compatibility name. Engine code uses
`LE::String` directly; no second implementation or STL facade is exposed.

Ownership and callable utilities justified by current Runtime consumers are
specified separately in [Core ownership and callable utilities](CoreOwnership.md).
