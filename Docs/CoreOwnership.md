# Core ownership and callable utilities (C3)

This document fixes the ownership boundary before Runtime modules migrate away
from Standard Library owning types. It implements only utilities justified by
current public consumers: Renderer PImpls require unique ownership, RFG
compiled plans require shared lifetime, and RFG/Renderer callbacks require
copyable type erasure. No current Runtime API requires optional value semantics,
so C3 deliberately does not add `LE::Optional`.

## Cross-module destruction rule

Owning memory may cross a module or DLL boundary only when the destruction code
and allocation route travel with it. An object created by module A must never be
destroyed by a raw `delete` selected in module B. The supported routes are:

- an exported creator/destroyer pair owned by module A;
- `LE::UniquePtr`, which stores a creator-side type-erased destruction thunk,
  context, and object pointer;
- `LE::SharedPtr`, whose engine-owned control block stores creator-side object
  and control-block destruction thunks plus the exact `IAllocator` identity;
- another explicit RAII owner whose contract provides the same guarantee.

Every referenced custom allocator must outlive its object and control block.
The default allocator has process lifetime. Stored destruction thunks are part
of the current same-source/same-toolchain engine ABI, not a promise of permanent
plugin ABI stability.

The creator module/code image that owns a stored destroy, copy, or invoke thunk
must remain loaded until every corresponding `UniquePtr`, `SharedPtr`,
`WeakPtr`, `Function`, object, and control block is gone. These utilities do not
pin a DLL. A future module-unload or hot-reload system must drain/revoke such
owners and callbacks before unloading creator code.

## `UniquePtr`

`LE::UniquePtr<T>` is move-only and never wraps or aliases `std::unique_ptr`.
`MakeUnique` uses the default allocator; `MakeUniqueWithAllocator` retains the
explicit allocator in its destroy context. Their `Try*` variants return an empty
owner when allocation fails, while ordinary factories use the centralized OOM
handler. Construction must be non-throwing and is checked at compile time.

`UniquePtr::Adopt` is the narrow external-resource hook. It requires an explicit
`noexcept` destroy function and context whenever the pointer is non-null. C3
does not expose `release()`: detaching the raw pointer would also discard the
only mechanically tracked cross-DLL destruction route. Use `Reset` for explicit
destruction and move for ownership transfer.

## `SharedPtr` and `WeakPtr`

`LE::SharedPtr<T>` and `LE::WeakPtr<T>` use an engine-owned control block, not a
Standard Library control block. `MakeShared` co-allocates object and control
block through the selected allocator. The block stores strong/weak atomic
counts, the original object pointer, and creator-side destroy thunks. Converted
base/const shared pointers keep their correctly adjusted access pointer while
the creator thunk continues to destroy the original dynamic object.

`AdoptShared` supports a stateful custom deleter. Allocation failure in
`TryAdoptShared` leaves the raw pointer owned by the caller; success transfers
its destruction to the control block. Deleter construction, invocation, and
destruction must be non-throwing. The object is destroyed exactly when the last
strong reference leaves. The control block remains until the implicit weak
reference and all `WeakPtr` instances leave. `WeakPtr::Lock` atomically succeeds
only while a strong owner remains.

Reference counts are thread-safe. The pointed-to object, pointer assignment,
and user operations are not made thread-safe by shared ownership. Shared
ownership is reserved for lifetimes that cannot be expressed by one owner plus
borrowed handles; it must not be used to avoid deciding ownership or to create
cycles.

## `Function`

`LE::Function<R(Args...)>` provides the copyable type erasure required by
existing render-graph and renderer callback APIs. Callable storage is allocated
through `IAllocator`; invoke, copy, and destroy thunks are created with the
callable type, so destruction remains on the creator route across a DLL call.
Callable construction, copying, and destruction must be non-throwing. Empty
invocation is a contract violation.

Invocation itself is not declared `noexcept`, because current render callbacks
are not yet annotated that way. `Function` does not catch or translate callable
exceptions: Runtime callables are still required by ADR-0001 not to let an
exception escape a module/DLL/third-party callback boundary. A later module
migration may strengthen individual callback signatures to `noexcept` only
after their implementations and failure channels support it.

`TryAssign` allocates a replacement before changing the current callable and
leaves the existing function intact on allocation failure. Ordinary construction,
copy, and assignment use the centralized OOM handler. C3 intentionally does
not add synchronization wrappers or claim that invocation is thread-safe.

## Migration boundary

C3 provides the stable vocabulary and tests but does not perform C5's broad
module migration. Existing `std::unique_ptr`, `std::shared_ptr`, and
`std::function` consumers are migrated module-by-module in C5, with builds after
each batch. Third-party callback and ownership signatures remain narrow
interoperability boundaries and are not mechanically rewritten.
