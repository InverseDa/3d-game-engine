# Core Math Conventions

Phase 1.5 C4 replaces the old global `FMath` implementation and Core's GLM
dependency with engine-owned, header-only value types in `LE::Math`. These
conventions are part of the P0 contract and must change only with an ADR and
updated numerical tests.

## Coordinates and angles

- World and view space are right-handed.
- The basis is `+X` right, `+Y` up, and `+Z` backward; forward is `-Z`.
- `Cross(+X, +Y) == +Z`. Positive rotations follow the right-hand rule.
- Every public angle accepted by Core math is in radians.
- Current Vulkan shaders already receive clip-space positions directly. A future
  projection builder must explicitly perform Vulkan depth/Y conversion at the
  render boundary; it must not silently change the world-space convention.

## Matrices and composition

`LE::Math::Matrix<T, R, C>` uses contiguous row-major physical storage:
`Data()[Row * C + Column]`. Mathematical vectors are column vectors on the
right, so transforms are written `MatrixValue * VectorValue`.

For compatible matrices, `A * B` applies `B` first and `A` second. This is the
same composition order used by Hamilton quaternion multiplication. Rectangular
matrices are first-class; identity is available only for square matrices.

## Quaternions

`LE::Math::Quaternion<T>` stores and indexes components in `x, y, z, w` order,
with the vector part first and scalar part last. Multiplication is the Hamilton
product. `FromAxisAngle` normalizes its axis, uses radians, and returns identity
for a zero axis. `Inverse` divides the conjugate by norm squared rather than
assuming a unit quaternion. Normalizing a zero quaternion returns identity.

## Layout and compatibility

Vector, matrix, and quaternion types:

- support `float` and `double` scalars;
- are standard-layout and trivially copyable;
- use natural scalar alignment (`alignof(T)`);
- contain no implicit SIMD padding (`Vector<float, 3>` is 12 bytes);
- own no dynamic storage and expose no GLM or Standard Library container ABI.

The stable names are `LE::Math::Vector<T, N>`, `Matrix<T, R, C>`, and
`Quaternion<T>`, with `Vector2f` through `Vector4d`, `Matrix2f` through
`Matrix4d`, and `Quaternionf`/`Quaterniond` aliases inside `LE::Math`. C6
removed the old global `FMath::T*`, `FVector*`, `FMatrix*`, and `FQuaternion*`
compatibility names.

Bounds failures use the Core contract-violation handler. Normalization of a
zero-length value is a defined numerical fallback rather than a contract error:
vectors become zero and quaternions become identity.
