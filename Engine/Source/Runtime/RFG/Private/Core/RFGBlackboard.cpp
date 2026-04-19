#include "Core/RFGBlackboard.h"

namespace
{
std::string MakeKey(const FString& Key)
{
    return std::string(Key.GetData());
}
}

bool FRFGBlackboard::HasPassHandle(const FString& Key) const
{
    return PassEntries.find(MakeKey(Key)) != PassEntries.end();
}

bool FRFGBlackboard::HasResourceHandle(const FString& Key) const
{
    return ResourceEntries.find(MakeKey(Key)) != ResourceEntries.end();
}

bool FRFGBlackboard::HasUInt(const FString& Key) const
{
    return UIntEntries.find(MakeKey(Key)) != UIntEntries.end();
}

void FRFGBlackboard::SetPassHandle(const FString& Key, FRFGPassHandle Value)
{
    PassEntries[MakeKey(Key)] = Value;
}

void FRFGBlackboard::SetResourceHandle(const FString& Key, FRFGResourceHandle Value)
{
    ResourceEntries[MakeKey(Key)] = Value;
}

void FRFGBlackboard::SetUInt(const FString& Key, uint64 Value)
{
    UIntEntries[MakeKey(Key)] = Value;
}

FRFGPassHandle FRFGBlackboard::GetPassHandle(const FString& Key) const
{
    const auto It = PassEntries.find(MakeKey(Key));
    return It != PassEntries.end() ? It->second : FRFGPassHandle{};
}

FRFGResourceHandle FRFGBlackboard::GetResourceHandle(const FString& Key) const
{
    const auto It = ResourceEntries.find(MakeKey(Key));
    return It != ResourceEntries.end() ? It->second : FRFGResourceHandle{};
}

uint64 FRFGBlackboard::GetUInt(const FString& Key) const
{
    const auto It = UIntEntries.find(MakeKey(Key));
    return It != UIntEntries.end() ? It->second : 0;
}

void FRFGBlackboard::Clear()
{
    PassEntries.clear();
    ResourceEntries.clear();
    UIntEntries.clear();
}
