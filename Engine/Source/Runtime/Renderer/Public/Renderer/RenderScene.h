#pragma once

#include "Asset/AssetTypes.h"
#include "CoreMinimal.h"
#include "RHI/RHITypes.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace LE
{

class FRHIBuffer;
class FRHIPipeline_Graphics;

enum class ERenderMeshPassMask : uint8
{
    None = 0,
    BackBuffer = 1 << 0,
    SceneColor = 1 << 1,
    All = BackBuffer | SceneColor,
};

inline constexpr ERenderMeshPassMask operator|(ERenderMeshPassMask Lhs, ERenderMeshPassMask Rhs)
{
    return static_cast<ERenderMeshPassMask>(static_cast<uint8>(Lhs) | static_cast<uint8>(Rhs));
}

inline constexpr bool HasRenderMeshPass(ERenderMeshPassMask Value, ERenderMeshPassMask Flag)
{
    return (static_cast<uint8>(Value) & static_cast<uint8>(Flag)) != 0;
}

// Renderer-neutral identity. WorldRenderer currently maps Scope to the source
// World domain and Value to the packed Entity generation/index pair.
struct FRenderPrimitiveId
{
    uint64 Scope = 0;
    uint64 Value = 0;

    bool IsValid() const noexcept { return Scope != 0 && Value != 0; }

    friend bool operator==(
        const FRenderPrimitiveId& Left,
        const FRenderPrimitiveId& Right) noexcept
    {
        return Left.Scope == Right.Scope && Left.Value == Right.Value;
    }

    friend bool operator!=(
        const FRenderPrimitiveId& Left,
        const FRenderPrimitiveId& Right) noexcept
    {
        return !(Left == Right);
    }
};

template <>
struct DefaultHash<FRenderPrimitiveId>
{
    std::size_t operator()(const FRenderPrimitiveId& Id) const noexcept
    {
        const std::size_t ScopeHash = DefaultHash<uint64>{}(Id.Scope);
        const std::size_t ValueHash = DefaultHash<uint64>{}(Id.Value);
        return ScopeHash ^ (ValueHash + static_cast<std::size_t>(0x9e3779b9U) +
            (ScopeHash << 6) + (ScopeHash >> 2));
    }
};

static_assert(std::is_trivially_copyable<FRenderPrimitiveId>::value,
    "render primitive IDs must remain trivially copyable runtime values");

// Axis-aligned bounds. The default sentinel is intentionally invalid so an
// asset provider must publish real local bounds before WorldRenderer accepts it.
struct FRenderBounds
{
    FRenderBounds() noexcept
        : Min((std::numeric_limits<float>::max)())
        , Max((std::numeric_limits<float>::lowest)())
    {
    }

    FRenderBounds(
        const Math::Vector3f& InMin,
        const Math::Vector3f& InMax) noexcept
        : Min(InMin)
        , Max(InMax)
    {
    }

    bool IsValid() const noexcept
    {
        for (std::size_t Axis = 0; Axis < 3; ++Axis)
        {
            if (!std::isfinite(Min.Values[Axis]) ||
                !std::isfinite(Max.Values[Axis]) ||
                Min.Values[Axis] > Max.Values[Axis])
            {
                return false;
            }
        }
        return true;
    }

    Math::Vector3f Min;
    Math::Vector3f Max;
};

struct FRenderPrimitive
{
    FRenderPrimitive() noexcept = default;
    explicit FRenderPrimitive(IAllocator& Allocator) noexcept
        : DebugName(Allocator)
    {
    }

    FRenderPrimitiveId PrimitiveId;
    LE::String DebugName;
    FRenderBounds WorldBounds;
};

struct FRenderMeshProxy : public FRenderPrimitive
{
    FRenderMeshProxy() noexcept = default;
    explicit FRenderMeshProxy(IAllocator& Allocator) noexcept
        : FRenderPrimitive(Allocator)
    {
    }

    Math::Matrix4f LocalToWorld = Math::Matrix4f::Identity();
    LE::FRHIPipeline_Graphics* GraphicsPipeline = nullptr;
    LE::FRHIBuffer* VertexBuffer = nullptr;
    uint32 VertexCount = 0;
    LE::FRHIBuffer* IndexBuffer = nullptr;
    uint32 IndexCount = 0;
    EPixelFormat IndexFormat = EPixelFormat::Unknown;
    ERenderMeshPassMask PassMask = ERenderMeshPassMask::All;
    uint64 SortKey = 0;
    FAssetId MaterialAssetId;
    Math::Vector4f BaseColor{1.0f};
};

struct FRenderLightProxy : public FRenderPrimitive
{
    using FRenderPrimitive::FRenderPrimitive;
};

struct FRenderCameraProxy : public FRenderPrimitive
{
    FRenderCameraProxy() noexcept = default;
    explicit FRenderCameraProxy(IAllocator& Allocator) noexcept
        : FRenderPrimitive(Allocator)
    {
    }

    Math::Matrix4f View = Math::Matrix4f::Identity();
    Math::Matrix4f Projection = Math::Matrix4f::Identity();
    Math::Matrix4f ViewProjection = Math::Matrix4f::Identity();
    uint32 ViewportWidth = 0;
    uint32 ViewportHeight = 0;
};

class RENDERER_API FRenderScene
{
public:
    FRenderScene() noexcept = default;

    explicit FRenderScene(IAllocator& Allocator) noexcept
        : Meshes(Allocator)
        , Lights(Allocator)
        , Cameras(Allocator)
    {
    }

    void Reset()
    {
        Meshes.Clear();
        Lights.Clear();
        Cameras.Clear();
    }

    IAllocator& GetAllocator() const noexcept { return Meshes.GetAllocator(); }

public:
    LE::Array<FRenderMeshProxy> Meshes;
    LE::Array<FRenderLightProxy> Lights;
    LE::Array<FRenderCameraProxy> Cameras;
};

} // namespace LE
