#include "Record/RFGPassRegistry.h"

namespace LE
{

bool FRFGPassRegistry::RegisterPassType(const FRFGRegisteredPassType& InPassType)
{
    if (InPassType.Schema.PassTypeName.IsEmpty())
    {
        return false;
    }

    return RegisteredPassTypes.Insert(InPassType.Schema.PassTypeName, InPassType);
}

bool FRFGPassRegistry::UnregisterPassType(const LE::String& PassTypeName)
{
    return RegisteredPassTypes.Erase(PassTypeName.View());
}

bool FRFGPassRegistry::HasPassType(const LE::String& PassTypeName) const
{
    return RegisteredPassTypes.Contains(PassTypeName.View());
}

const FRFGRegisteredPassType* FRFGPassRegistry::FindPassType(const LE::String& PassTypeName) const
{
    return RegisteredPassTypes.Find(PassTypeName.View());
}

} // namespace LE
