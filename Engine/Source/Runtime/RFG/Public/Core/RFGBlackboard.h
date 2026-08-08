#pragma once

#include "CoreMinimal.h"
#include "Core/RFGHandles.h"


namespace LE
{

class RFG_API FRFGBlackboard
{
public:
    FRFGBlackboard() = default;
    ~FRFGBlackboard() = default;

public:
    bool HasPassHandle(const LE::String& Key) const;
    bool HasResourceHandle(const LE::String& Key) const;
    bool HasUInt(const LE::String& Key) const;

public:
    void SetPassHandle(const LE::String& Key, FRFGPassHandle Value);
    void SetResourceHandle(const LE::String& Key, FRFGResourceHandle Value);
    void SetUInt(const LE::String& Key, uint64 Value);

public:
    FRFGPassHandle GetPassHandle(const LE::String& Key) const;
    FRFGResourceHandle GetResourceHandle(const LE::String& Key) const;
    uint64 GetUInt(const LE::String& Key) const;

public:
    void Clear();

private:
    LE::HashMap<LE::String, FRFGPassHandle> PassEntries;
    LE::HashMap<LE::String, FRFGResourceHandle> ResourceEntries;
    LE::HashMap<LE::String, uint64> UIntEntries;
};

} // namespace LE
