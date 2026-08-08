#include "Core/RFGBlackboard.h"

namespace LE
{

bool FRFGBlackboard::HasPassHandle(const LE::String& Key) const
{
    return PassEntries.Contains(Key.View());
}

bool FRFGBlackboard::HasResourceHandle(const LE::String& Key) const
{
    return ResourceEntries.Contains(Key.View());
}

bool FRFGBlackboard::HasUInt(const LE::String& Key) const
{
    return UIntEntries.Contains(Key.View());
}

void FRFGBlackboard::SetPassHandle(const LE::String& Key, FRFGPassHandle Value)
{
    PassEntries.InsertOrAssign(Key, Value);
}

void FRFGBlackboard::SetResourceHandle(const LE::String& Key, FRFGResourceHandle Value)
{
    ResourceEntries.InsertOrAssign(Key, Value);
}

void FRFGBlackboard::SetUInt(const LE::String& Key, uint64 Value)
{
    UIntEntries.InsertOrAssign(Key, Value);
}

FRFGPassHandle FRFGBlackboard::GetPassHandle(const LE::String& Key) const
{
    const FRFGPassHandle* const Found = PassEntries.Find(Key.View());
    return Found != nullptr ? *Found : FRFGPassHandle{};
}

FRFGResourceHandle FRFGBlackboard::GetResourceHandle(const LE::String& Key) const
{
    const FRFGResourceHandle* const Found = ResourceEntries.Find(Key.View());
    return Found != nullptr ? *Found : FRFGResourceHandle{};
}

uint64 FRFGBlackboard::GetUInt(const LE::String& Key) const
{
    const uint64* const Found = UIntEntries.Find(Key.View());
    return Found != nullptr ? *Found : 0;
}

void FRFGBlackboard::Clear()
{
    PassEntries.Clear();
    ResourceEntries.Clear();
    UIntEntries.Clear();
}

} // namespace LE
