#pragma once

#include "CoreMinimal.h"


namespace LE
{

struct FRFGParameterValue
{
    LE::Array<uint8> Bytes;
    uint32 Revision = 0;
};

class RFG_API FRFGParameterStore
{
public:
    FRFGParameterStore() = default;
    ~FRFGParameterStore() = default;

public:
    bool HasValue(const LE::String& Key) const;
    const FRFGParameterValue* FindValue(const LE::String& Key) const;
    FRFGParameterValue* FindValue(const LE::String& Key);

public:
    void SetValue(const LE::String& Key, const FRFGParameterValue& Value);
    void RemoveValue(const LE::String& Key);
    void Clear();

private:
    LE::HashMap<LE::String, FRFGParameterValue> Values;
};

} // namespace LE
