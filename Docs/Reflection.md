# Reflection and property serialization P0

Reflection P0 is a self-owned runtime metadata boundary. It describes native
types, properties, and enums without exposing a third-party or compiler
reflection ABI. Methods, RPC, scripting, schema migration, and native-object
restore remain outside this phase. `FPropertyValue`, `FPropertyBag`, and the
structured binary codec build on the stable metadata boundary.

## Stable declaration macros

Engine code marks declarations with the engine-owned forms
`LE_STRUCT("uuid")`, `LE_CLASS("uuid")`, `LE_ENUM("uuid")`,
`LE_PROPERTY("uuid")`, and the zero-argument `LE_GENERATED_BODY()`. Each ID is
a lowercase canonical, non-nil UUID string literal. The type macros immediately
precede their declaration, a property macro immediately precedes one non-static
data member, and one generated body appears within each marked struct or class.
No flags or serialization/editor/script policy are part of this P0 grammar.

The ID macros expand only to C++17 constant validation.
`LE_GENERATED_BODY()` grants friendship to the engine-private
`TReflectionGeneratedAccess<T>` hook. These declarations add no storage, static
initialization, metadata, or registration. The registry therefore remains
empty until a provider explicitly registers a module.

The Builder reflection generator parses those stable macro tokens, specializes
the generic access hook, and emits descriptors plus explicit module entry
points against this same Reflection ABI. A future C++26 backend must target the
same hook and ABI as well. Neither backend name nor raw compiler/third-party
reflection syntax is exposed to ordinary engine code.

## Builder generator contract

Each provider declares one typed `CustomAction` in its `Build.ts`. Reflection
headers are explicit action inputs rather than a hidden repository scan. The
generator implementation is an implicit input, the generated `.cpp` is placed
under `[module.Generated]/Reflection`, and `RunBeforeCompile` connects the
producer to every module compile. Inputs are sorted and declarations retain
stable source order; generated text contains no timestamp or absolute path, and
the output file is replaced only when its content changes. A failed parse or emit leaves an existing output
unchanged and leaves no partial C++ file behind.

Task 13 intentionally supports a small, diagnosed C++ subset: named non-template,
non-nested structs/classes without bases; one non-static, non-array, non-bitfield
value member per `LE_PROPERTY`; and scoped enums with an explicit engine integer
underlying type and explicit integer-literal values. Every reflected struct or
class has exactly one `LE_GENERATED_BODY`. Reflection macros in conditional
preprocessor regions are rejected, including `#if 0`, so the generator cannot
create metadata for declarations absent from the compiler translation unit.
Comments, ordinary strings, character literals, and raw strings never create
false macro matches. Diagnostics use `path:line:column` locations.

A property value type must either use one documented canonical builtin spelling
or itself be reflected in the same generator batch. The current builtin C++
spellings are `bool`, `int8`, `uint8`, `int32`, `uint32`, `int64`, `uint64`,
`float32`, `float64`, and `String`, including their `LE::`/`::LE::` qualified
forms where applicable. There is no ordinary 16-bit property scalar because
Core currently owns no `int16`/`uint16` alias; a reflected enum may still have
a 16-bit underlying representation through the enum raw-bits contract.
`Bytes` exists only for manually populated bags and is not a generator mapping
for `Array<uint8>` or another template type.

The TypeScript generator maps spellings to `EPropertyBuiltinType` enumerators
and emits calls to `GetBuiltinPropertyTypeId`; it does not contain UUID copies.
Generated construction and destruction happen inside the
friend access specialization and retain the nothrow default-construction and
destruction compile contract. Setter selection is made by a C++ type trait in
the generated translation unit; a non-nothrow or non-assignable member receives
a null read-only setter.

## Module and ABI boundary

`Runtime/Reflection` depends only on Core. `FReflectionRegistry` is exported as
a non-copyable and non-movable PImpl class. The PImpl, module metadata, strings,
arrays, runtime-handle storage, and their destruction all remain inside the
Reflection binary and use the allocator supplied at registry construction (the
default constructor uses `GetDefaultAllocator()`). A custom allocator must
outlive the registry that borrows it. Public `FTypeInfo`,
`FPropertyInfo`, `FEnumInfo`, and `FEnumValueInfo` expose only values,
`StringView`, `Span`, and plain function pointers; they never expose an owning
container across the DLL boundary. `FPropertyValue` and `FPropertyBag` are
likewise exported PImpl owners. Their destructors remain in Reflection and each
instance retains the exact allocator supplied at construction.

Registration descriptors are borrowed for the duration of `RegisterModule`.
A successful registration deep-copies every descriptor string and span. The
construction, destruction, getter, and setter thunk addresses are different:
they remain borrowed code pointers into the provider module. A provider must
explicitly unregister its module before its DLL can unload. Registry destruction
only discards thunk values and never calls provider code.

Every successful registration or unregistration invalidates all pointers and
views returned by lookup. Failed operations do not invalidate them. Registry
access, including lookup, is not thread-safe; the owner must externally
synchronize every call. Reflection P0 adds no locks, threads, Runnable, or job
system policy.

## Stable identity and module instances

`FTypeId` and `FPropertyId` are separate `TStableId` domains backed by non-nil
UUIDs. Struct/class and enum types share one global ID and qualified-name
namespace. A property ID and name are unique within its owning type; the lookup
context is the pair `(FTypeId, FPropertyId)`. Property serialization preserves
that pair; it does not define former IDs or migration.

`FReflectionModuleHandle` is an index+generation runtime handle. It identifies
one loaded registration instance and must not be persisted. Unregistration uses
the exact handle, so a stale handle cannot remove a later reload with the same
module name.

## Canonical builtin type IDs

These UUIDs are persistent schema identities. The C++ accessor implementation
is the executable source of truth; this table documents that contract:

| Builtin | Stable `FTypeId` |
|---|---|
| Bool | `62c925ef-59ef-4b81-ac9a-956d443b91fe` |
| Int8 | `8f174d4e-59ca-4c58-9749-3ec98fa8c56c` |
| UInt8 | `b7436e66-6482-4e1f-94d9-67b0323c426d` |
| Int32 | `6ff6b20a-9345-460c-9bfd-9a4c55ee503a` |
| UInt32 | `93497730-7ea5-412b-aee9-e10ce909a080` |
| Int64 | `146ed02f-ea65-4278-a698-61906f630132` |
| UInt64 | `cfe809b0-014d-4485-8eb6-2328ddce9c27` |
| Float32 | `7378e461-09b0-4da9-be06-386cb67d70d4` |
| Float64 | `351e30dd-600b-4201-b1c0-0dd42263ef23` |
| String | `71219161-14ba-4a9f-a4ed-9fd33a96a1d5` |
| Bytes | `68d345ab-1bad-41ab-acc3-add49d1280e9` |

These IDs are reserved from reflected struct/class and enum registration;
attempting to reuse one is a duplicate type identity.

## Property values, bags, and ownership

`FPropertyValue` is a move-only tagged value for the exact builtin widths,
UTF-8 strings, manual bytes, enum raw bits, nested objects, and opaque future
wire records. `FPropertyBag` is a move-only `(schema FTypeId, nonzero uint32
schema version)` document with fields keyed by stable `FPropertyId`. It stores
an `Array`, not a `HashMap`, and maintains ascending UUID-byte order after every
mutation. Hash iteration can therefore never influence persistent output.

`TryInsert` rejects duplicates while `TrySet` explicitly replaces. Only success
consumes an rvalue. Duplicate, invalid, and allocation-failure results preserve
both the input value and the bag's logical contents. A bag recursively owns all
entry storage through its own allocator: values arriving from another allocator
are cloned first, committed only after all allocation succeeds, and consumed
only after commit. The custom allocator must outlive the bag. Moving a complete
value or bag transfers that allocator dependency; the moved-from object remains
empty, destructible, reusable, and still bound to its original allocator.
Pointers returned by bag lookup and views/pointers returned by a property value
are borrowed. Any successful mutation, initialization/reset, move, or
destruction of the respective owner conservatively invalidates them.

All `TrySet` operations replace a value only after constructing a complete
candidate. `String` accepts only well-formed UTF-8 (embedded NUL is valid UTF-8).
Float encodings preserve the raw IEEE bit pattern, including NaN sign and
payload, rather than normalizing numeric values.

## Structured binary format

Task 14 deliberately provides one binary format rather than a parallel text
format. A text/JSON implementation would add escaping, number-conversion, and
unknown-token rules while still needing the binary framing contract. Binary v1
is deterministic, length-delimited, and can retain future field encodings.
Every integer is little-endian.

The 32-byte document header is magic `LPBG`, format version `uint16`, document
flags `uint16`, schema TypeId (16 UUID bytes), schema version `uint32`, and field
count `uint32`. A field is property ID (16), value type ID (16), wire tag
`uint8`, wire flags `uint8`, reserved `uint16`, payload size `uint32`, then that
many payload bytes. Field output order is ascending property UUID bytes. Nested
Object payloads contain a complete document. Capture applies the caller's one
nonzero schema version recursively to the whole object graph; a manually built
nested bag may carry its own version and the wire preserves it.

Wire tags 1-13 are Bool, Int8, UInt8, Int32, UInt32, Int64, UInt64, Float32,
Float64, String, Bytes, Enum, and Object. Tag zero is invalid. A known tag with
zero flags is decoded strictly: exact payload length and builtin type ID must
match. A known tag with nonzero future flags, or any tag 14-255, is stored as
Opaque with its original tag, flags, type ID, and payload. Nonzero reserved bits
are malformed. Unknown property IDs remain ordinary bag entries, preserving
their value semantics. Opaque values preserve their tag, flags, type ID, and raw
payload bytes exactly. Known kinds are re-emitted in canonical form, including
canonical field ordering inside nested Object values. Re-encoding therefore does
not promise the input's original record order, known-kind payload bytes, or
byte-for-byte document identity.

Decode, encode, and native capture build complete candidates and replace their
output only on success. They reject bad magic, unsupported format versions or
document flags, nil schema/type IDs, nil property IDs (`InvalidPropertyId`),
duplicate field IDs, malformed lengths, invalid UTF-8,
truncation, trailing data, arithmetic overflow, and configured total-document,
per-value, field-count, and recursion-depth limits. No decode error can leave a
partially updated output.
One shared, allocation-free, overflow-safe wire-size measurement applies the
same value, document, field, and depth limits before encode allocation,
validation success, or capture commit.

`ValidatePropertyBag` checks known fields against registry `ValueTypeId`s while
accepting unknown property IDs. `TryCapturePropertyBag` consumes generated
getter metadata for builtins, enums, and recursively reflected value objects;
read-only properties remain capturable. A hand-written descriptor that assigns
a builtin `ValueTypeId` to a different native C++ storage type violates the
getter/capture thunk contract and cannot be diagnosed from `const void*` at
runtime. Generated providers guarantee that the C++ spelling and emitted
builtin ID agree.

There is intentionally no apply-to-existing-native-object API. Existing
metadata has no clone/swap/move-construction thunk, so sequential setters could
partially mutate an object and would not meet the transactional contract. This
task does not invent migrations or former-ID behavior to hide that gap.

`DemoApplication` is the first non-test consumer. Its private `FDemoSettings`
provider is explicitly registered by the existing `Demo.Window` runtime module;
startup captures defaults, encodes and decodes them, validates the decoded bag,
and creates the actual platform window from decoded width, height, and title.
Shutdown unregisters the provider before discarding the bag, and every startup
failure performs the same exact cleanup. No public settings API or extra runtime
module was added.

## Transaction and validation contract

One fully owned module is the registry commit unit. Registration first validates
the complete batch, then deep-copies it into local owned storage, reserves the
single outer module container, issues a handle, and performs a non-allocating
commit. Invalid descriptors, duplicates, and allocation failures leave all
observable registry contents and the output handle unchanged; they do not
consume an observable handle generation.

Registration rejects:

- empty or embedded-NUL names and nil stable IDs;
- duplicate module names, global type IDs, or global qualified type names;
- reflected type/enum IDs reserved by canonical builtin property types;
- duplicate property IDs or names within one owner;
- zero native sizes, non-power-of-two/mismatched alignments, invalid type kinds,
  and missing construction, destruction, or getter thunks;
- invalid enum underlying kinds, duplicate enum value names, and raw values
  wider than the declared underlying type.

Property value type IDs must be valid but may be unresolved or self-referential.
This allows ordinary reflected modules to register in an explicit order
without adding hidden global initialization. A null setter is a
valid, explicit read-only property.

## Thunks

`TDefaultReflectionThunks<T>` placement-constructs and destroys `T` in
caller-provided storage. It requires nothrow default construction/destruction
and rejects null or misaligned storage. `TMemberReflectionThunks<Member>`
provides a const borrowed-address getter and a copying setter. Its setter
requires nothrow assignment and rejects null or misaligned object/value
pointers. Provider-specific hand-written thunks may implement private access,
validation, or notifications while retaining the same function-pointer ABI.

The World module is the first real DLL provider and generated macro consumer.
`FWorld` uses `LE_CLASS`, contains `LE_GENERATED_BODY`, and exports explicit
`RegisterWorldReflection` and `UnregisterWorldReflection` declarations from its
stable public header. Its Builder action generates the only descriptor
definition, still describing only `LE::FWorld` at type level. It intentionally
does not assign persistent IDs to the current prototype mesh/GPU fields. The
generated translation unit is compiled into the provider, so Editor thunk
addresses remain in `World.dll` and must be unregistered before unload; Game
uses the same explicit functions from the static module. There is no global
static registration or automatic Application integration.

Visual Studio does not duplicate the CustomAction or show the generated `.cpp`
before it exists, but its NMake build command returns to LimitlessBuilder/Ninja
and executes the same action graph. Xcode remains the native compiler owner. A
pre-Sources phase calls `LimitlessBuilder.sh build-action` for the reflection
action, and a stable Debug/Release bridge includes the corresponding generated
translation unit by repository-relative path. The bridge contains no descriptor
copy, so the generated file remains the single metadata truth. The custom-only
Ninja graph lives below the variant `CustomActions` directory and owns incremental
execution; it never invokes a Mac C++ toolchain. Xcode script sandboxing is
explicitly disabled because this phase launches LB/Ninja and writes variant
generated output. The project structure and custom DAG are covered on Windows;
a real macOS `xcodebuild` remains required for platform acceptance.

## Enum numeric contract

`EEnumUnderlyingType` explicitly selects signed or unsigned 8/16/32/64-bit
storage. `FEnumValueInfo::ValueBits` is the canonical raw underlying bit pattern:
bits above widths smaller than 64 must be zero, while a negative signed value
uses that width's two's-complement bits. Numeric aliases are allowed and
`FindValueByBits` returns the first value in declaration order. The signed and
unsigned interpretation helpers reject the wrong signedness, non-canonical raw
bits, and unknown underlying kinds.
