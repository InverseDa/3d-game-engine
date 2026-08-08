#include "Authoring/RFGParameterStore.h"

namespace LE
{

bool FRFGParameterStore::HasValue(const LE::String& Key) const
{
    return Values.Contains(Key.View());
}

const FRFGParameterValue* FRFGParameterStore::FindValue(const LE::String& Key) const
{
    return Values.Find(Key.View());
}

FRFGParameterValue* FRFGParameterStore::FindValue(const LE::String& Key)
{
    return Values.Find(Key.View());
}

void FRFGParameterStore::SetValue(const LE::String& Key, const FRFGParameterValue& Value)
{
    Values.InsertOrAssign(Key, Value);
}

void FRFGParameterStore::RemoveValue(const LE::String& Key)
{
    Values.Erase(Key.View());
}

void FRFGParameterStore::Clear()
{
    Values.Clear();
}

} // namespace LE
