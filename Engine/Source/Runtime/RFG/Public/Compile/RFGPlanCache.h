#pragma once

#include "CoreMinimal.h"
#include "Core/RFGTypes.h"


namespace LE
{

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
    LE::SharedPtr<const FRFGCompiledPlan> Find(const FRFGGraphSignature& Signature) const;
    void Store(const FRFGGraphSignature& Signature, const LE::SharedPtr<const FRFGCompiledPlan>& Plan);
    void Remove(const FRFGGraphSignature& Signature);
    void Clear();

public:
    const FRFGPlanCacheStats& GetStats() const;

private:
    mutable FRFGPlanCacheStats Stats;
    LE::HashMap<uint64, LE::SharedPtr<const FRFGCompiledPlan>> CachedPlans;
};

} // namespace LE
