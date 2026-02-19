#pragma once

#include "CoreMinimal.h"
#include "Core/RFGTypes.h"

#include <memory>
#include <unordered_map>

class FRFGCompiledPlan;

struct FRFGPlanCacheStats
{
    uint64 HitCount = 0;
    uint64 MissCount = 0;
    uint64 EvictionCount = 0;
};

class RFG_API FRFGPlanCache
{
public:
    FRFGPlanCache() = default;
    ~FRFGPlanCache() = default;

public:
    std::shared_ptr<const FRFGCompiledPlan> Find(const FRFGGraphSignature& Signature) const;
    void Store(const FRFGGraphSignature& Signature, const std::shared_ptr<const FRFGCompiledPlan>& Plan);
    void Remove(const FRFGGraphSignature& Signature);
    void Clear();

public:
    const FRFGPlanCacheStats& GetStats() const;

private:
    mutable FRFGPlanCacheStats Stats;
    std::unordered_map<uint64, std::shared_ptr<const FRFGCompiledPlan>> CachedPlans;
};
