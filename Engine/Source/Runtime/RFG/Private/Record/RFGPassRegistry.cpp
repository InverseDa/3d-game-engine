#include "Record/RFGPassRegistry.h"

namespace
{
std::string MakeKey(const FString& Key)
{
    return std::string(Key.GetData());
}
}

bool FRFGPassRegistry::RegisterPassType(const FRFGRegisteredPassType& InPassType)
{
    if (InPassType.Schema.PassTypeName.IsEmpty())
    {
        return false;
    }

    return RegisteredPassTypes.emplace(MakeKey(InPassType.Schema.PassTypeName), InPassType).second;
}

bool FRFGPassRegistry::UnregisterPassType(const FString& PassTypeName)
{
    return RegisteredPassTypes.erase(MakeKey(PassTypeName)) > 0;
}

bool FRFGPassRegistry::HasPassType(const FString& PassTypeName) const
{
    return RegisteredPassTypes.find(MakeKey(PassTypeName)) != RegisteredPassTypes.end();
}

const FRFGRegisteredPassType* FRFGPassRegistry::FindPassType(const FString& PassTypeName) const
{
    const auto It = RegisteredPassTypes.find(MakeKey(PassTypeName));
    return It != RegisteredPassTypes.end() ? &It->second : nullptr;
}
