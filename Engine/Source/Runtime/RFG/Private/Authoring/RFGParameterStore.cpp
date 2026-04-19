#include "Authoring/RFGParameterStore.h"

namespace
{
std::string MakeKey(const FString& Key)
{
    return std::string(Key.GetData());
}
}

bool FRFGParameterStore::HasValue(const FString& Key) const
{
    return Values.find(MakeKey(Key)) != Values.end();
}

const FRFGParameterValue* FRFGParameterStore::FindValue(const FString& Key) const
{
    const auto It = Values.find(MakeKey(Key));
    return It != Values.end() ? &It->second : nullptr;
}

FRFGParameterValue* FRFGParameterStore::FindValue(const FString& Key)
{
    const auto It = Values.find(MakeKey(Key));
    return It != Values.end() ? &It->second : nullptr;
}

void FRFGParameterStore::SetValue(const FString& Key, const FRFGParameterValue& Value)
{
    Values[MakeKey(Key)] = Value;
}

void FRFGParameterStore::RemoveValue(const FString& Key)
{
    Values.erase(MakeKey(Key));
}

void FRFGParameterStore::Clear()
{
    Values.clear();
}
