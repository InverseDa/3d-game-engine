#include "Compile/RFGCompiledPlan.h"

const FRFGGraphSignature& FRFGCompiledPlan::GetSignature() const
{
    return Signature;
}

const std::vector<FRFGCompiledPass>& FRFGCompiledPlan::GetPasses() const
{
    return CompiledPasses;
}

const std::vector<FRFGCompiledResourceLife>& FRFGCompiledPlan::GetResourceLifetimes() const
{
    return ResourceLifetimes;
}

FRFGGraphSignature& FRFGCompiledPlan::GetMutableSignature()
{
    return Signature;
}

std::vector<FRFGCompiledPass>& FRFGCompiledPlan::GetMutablePasses()
{
    return CompiledPasses;
}

std::vector<FRFGCompiledResourceLife>& FRFGCompiledPlan::GetMutableResourceLifetimes()
{
    return ResourceLifetimes;
}

void FRFGCompiledPlan::Clear()
{
    Signature = {};
    CompiledPasses.clear();
    ResourceLifetimes.clear();
}
