#include "Compile/RFGPlanCache.h"

std::shared_ptr<const FRFGCompiledPlan> FRFGPlanCache::Find(const FRFGGraphSignature& Signature) const
{
    const auto It = CachedPlans.find(Signature.Value);
    if (It != CachedPlans.end())
    {
        ++Stats.HitCount;
        return It->second;
    }

    ++Stats.MissCount;
    return nullptr;
}

void FRFGPlanCache::Store(const FRFGGraphSignature& Signature, const std::shared_ptr<const FRFGCompiledPlan>& Plan)
{
    if (Plan == nullptr)
    {
        return;
    }

    CachedPlans[Signature.Value] = Plan;
}

void FRFGPlanCache::Remove(const FRFGGraphSignature& Signature)
{
    const auto It = CachedPlans.find(Signature.Value);
    if (It != CachedPlans.end())
    {
        CachedPlans.erase(It);
        ++Stats.EvictionCount;
    }
}

void FRFGPlanCache::Clear()
{
    Stats.EvictionCount += CachedPlans.size();
    CachedPlans.clear();
}

const FRFGPlanCacheStats& FRFGPlanCache::GetStats() const
{
    return Stats;
}
