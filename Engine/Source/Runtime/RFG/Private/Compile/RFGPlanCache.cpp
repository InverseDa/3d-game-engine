#include "Compile/RFGPlanCache.h"

#include "Compile/RFGCompiledPlan.h"

namespace LE
{

LE::SharedPtr<const FRFGCompiledPlan> FRFGPlanCache::Find(const FRFGGraphSignature& Signature) const
{
    const LE::SharedPtr<const FRFGCompiledPlan>* const Plan = CachedPlans.Find(Signature.Value);
    if (Plan != nullptr)
    {
        if (!*Plan || (*Plan)->GetSignature().Value != Signature.Value)
        {
            ++Stats.MissCount;
            return nullptr;
        }

        ++Stats.HitCount;
        return *Plan;
    }

    ++Stats.MissCount;
    return nullptr;
}

void FRFGPlanCache::Store(const FRFGGraphSignature& Signature, const LE::SharedPtr<const FRFGCompiledPlan>& Plan)
{
    if (Signature.Value == 0 || !Plan || Plan->GetSignature().Value != Signature.Value)
    {
        return;
    }

    CachedPlans.InsertOrAssign(Signature.Value, Plan);
}

void FRFGPlanCache::Remove(const FRFGGraphSignature& Signature)
{
    if (CachedPlans.Erase(Signature.Value))
    {
        ++Stats.EvictionCount;
    }
}

void FRFGPlanCache::Clear()
{
    Stats.EvictionCount += CachedPlans.Size();
    CachedPlans.Clear();
}

const FRFGPlanCacheStats& FRFGPlanCache::GetStats() const
{
    return Stats;
}

} // namespace LE
