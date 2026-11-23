#pragma once

#include "Asset/AssetTypes.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "World/Transform.h"

namespace LE
{

inline constexpr uint32 SceneDocumentVersion = 2;
inline constexpr std::size_t MaxSceneDocumentBytes = 64 * 1024 * 1024;
inline constexpr std::size_t MaxSceneEntities = 65536;
inline constexpr std::size_t MaxSceneNameBytes = 1024;

struct FSceneEntityIdTag;
using FSceneEntityId = TStableId<FSceneEntityIdTag>;

struct FSceneMeshRecord
{
    explicit FSceneMeshRecord(IAllocator& Allocator = GetDefaultAllocator()) noexcept : DebugName(Allocator) {}

    FAssetId Mesh;
    FAssetId Material;
    String DebugName;
    uint64 SortKey = 0;
};

struct FSceneCameraRecord
{
    explicit FSceneCameraRecord(IAllocator& Allocator = GetDefaultAllocator()) noexcept : DebugName(Allocator) {}

    String DebugName;
    float VerticalFieldOfViewRadians = 1.04719755f;
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;
    int32 Priority = 0;
    bool bActive = true;
};

struct FSceneDirectionalLightRecord
{
    Math::Vector3f Direction{0.0f, 0.0f, -1.0f};
    Math::Vector3f Color{1.0f, 1.0f, 1.0f};
    float Intensity = 1.0f;
    bool bActive = true;
};

struct FSceneEntityRecord
{
    explicit FSceneEntityRecord(IAllocator& Allocator = GetDefaultAllocator()) noexcept : Mesh(Allocator), Camera(Allocator) {}

    FSceneEntityId Id;
    FSceneEntityId Parent;
    FTransform Transform;
    FSceneMeshRecord Mesh;
    FSceneCameraRecord Camera;
    FSceneDirectionalLightRecord DirectionalLight;
    bool bHasTransform = false;
    bool bHasMesh = false;
    bool bHasCamera = false;
    bool bHasDirectionalLight = false;
};

struct FSceneDocument
{
    explicit FSceneDocument(IAllocator& Allocator = GetDefaultAllocator()) noexcept : Entities(Allocator) {}

    Array<FSceneEntityRecord> Entities;
};

enum class ESceneDocumentResult : uint8
{
    Success,
    InvalidJson,
    UnsupportedVersion,
    DuplicateField,
    UnknownField,
    MissingField,
    InvalidValue,
    LimitExceeded,
    AllocationFailed,
    DuplicateEntityId,
    InvalidParent,
    ParentCycle,
    MultipleActiveDirectionalLights,
};

struct FSceneDocumentDiagnostic
{
    ESceneDocumentResult Result = ESceneDocumentResult::Success;
    std::size_t Offset = 0;
    std::size_t Line = 0;
    std::size_t Column = 0;
    FSceneEntityId Entity;
    StringView Field;
};

// Decode and encode preserve Out on failure. Decode sorts records by persistent
// entity ID. Diagnostics borrow only static field names; 0 line/column means
// the error came from a DTO instead of a source location.
SCENE_API ESceneDocumentResult DecodeSceneDocument(StringView Source, FSceneDocument& Out, FSceneDocumentDiagnostic* Diagnostic = nullptr) noexcept;
SCENE_API ESceneDocumentResult EncodeSceneDocument(const FSceneDocument& Document, String& Out, FSceneDocumentDiagnostic* Diagnostic = nullptr) noexcept;
SCENE_API ESceneDocumentResult ValidateSceneDocument(const FSceneDocument& Document, FSceneDocumentDiagnostic* Diagnostic = nullptr) noexcept;
SCENE_API const char* DescribeSceneDocumentResult(ESceneDocumentResult Result) noexcept;

} // namespace LE
