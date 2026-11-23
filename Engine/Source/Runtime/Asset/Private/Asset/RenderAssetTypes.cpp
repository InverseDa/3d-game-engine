#include "Asset/RenderAssetTypes.h"

#include "Memory/Allocator.h"

namespace LE
{
namespace
{

FAssetTypeId ParseRequiredAssetTypeId(const StringView Text) noexcept
{
    FAssetTypeId Result;
    if (!FAssetTypeId::TryParse(Text, Result))
    {
        HandleContractViolation("invalid built-in asset type ID", __FILE__, __LINE__);
    }
    return Result;
}

} // namespace

FAssetTypeId GetMeshAssetTypeId() noexcept
{
    static const FAssetTypeId TypeId = ParseRequiredAssetTypeId("a2000000-0000-4000-8000-000000000001");
    return TypeId;
}

FAssetTypeId GetMaterialAssetTypeId() noexcept
{
    static const FAssetTypeId TypeId = ParseRequiredAssetTypeId("a2000000-0000-4000-8000-000000000002");
    return TypeId;
}

} // namespace LE
