#include "Asset/TextureAssetTypes.h"

#include "Memory/Allocator.h"

namespace LE
{

FAssetTypeId GetCpuTextureAssetTypeId() noexcept
{
    static const FAssetTypeId TypeId = []() noexcept {
        FAssetTypeId Result;
        if (!FAssetTypeId::TryParse("a2000000-0000-4000-8000-000000000003", Result))
        {
            HandleContractViolation("invalid CPU texture asset type ID", __FILE__, __LINE__);
        }
        return Result;
    }();
    return TypeId;
}

} // namespace LE
