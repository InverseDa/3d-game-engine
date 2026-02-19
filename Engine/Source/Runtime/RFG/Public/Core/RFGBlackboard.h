#pragma once

#include "CoreMinimal.h"
#include "Core/RFGHandles.h"

#include <string>
#include <unordered_map>

class RFG_API FRFGBlackboard
{
public:
    FRFGBlackboard() = default;
    ~FRFGBlackboard() = default;

public:
    bool HasPassHandle(const FString& Key) const;
    bool HasResourceHandle(const FString& Key) const;
    bool HasUInt(const FString& Key) const;

public:
    void SetPassHandle(const FString& Key, FRFGPassHandle Value);
    void SetResourceHandle(const FString& Key, FRFGResourceHandle Value);
    void SetUInt(const FString& Key, uint64 Value);

public:
    FRFGPassHandle GetPassHandle(const FString& Key) const;
    FRFGResourceHandle GetResourceHandle(const FString& Key) const;
    uint64 GetUInt(const FString& Key) const;

public:
    void Clear();

private:
    std::unordered_map<std::string, FRFGPassHandle> PassEntries;
    std::unordered_map<std::string, FRFGResourceHandle> ResourceEntries;
    std::unordered_map<std::string, uint64> UIntEntries;
};
