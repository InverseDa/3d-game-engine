#pragma once

#include "CoreMinimal.h"

#include <string>
#include <unordered_map>
#include <vector>

struct FRFGParameterValue
{
    std::vector<uint8> Bytes;
    uint32 Revision = 0;
};

class RFG_API FRFGParameterStore
{
public:
    FRFGParameterStore() = default;
    ~FRFGParameterStore() = default;

public:
    bool HasValue(const FString& Key) const;
    const FRFGParameterValue* FindValue(const FString& Key) const;
    FRFGParameterValue* FindValue(const FString& Key);

public:
    void SetValue(const FString& Key, const FRFGParameterValue& Value);
    void RemoveValue(const FString& Key);
    void Clear();

private:
    std::unordered_map<std::string, FRFGParameterValue> Values;
};
