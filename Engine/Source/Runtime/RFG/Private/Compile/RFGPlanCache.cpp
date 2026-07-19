#include "Compile/RFGPlanCache.h"

#include "Compile/RFGCompiledPlan.h"

std::shared_ptr<const FRFGCompiledPlan> FRFGPlanCache::Find(const FRFGGraphSignature& Signature) const
{
    const auto It = CachedPlans.find(Signature.Value);
    if (It != CachedPlans.end())
    {
        if (It->second == nullptr || It->second->GetSignature().Value != Signature.Value)
        {
            ++Stats.MissCount;
            return nullptr;
        }

        ++Stats.HitCount;
        return It->second;
    }

    ++Stats.MissCount;
    return nullptr;
}

void FRFGPlanCache::Store(const FRFGGraphSignature& Signature, const std::shared_ptr<const FRFGCompiledPlan>& Plan)
{
    if (Signature.Value == 0 || Plan == nullptr || Plan->GetSignature().Value != Signature.Value)
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
