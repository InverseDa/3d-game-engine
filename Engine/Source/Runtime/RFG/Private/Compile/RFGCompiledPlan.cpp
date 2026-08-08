#include "Compile/RFGCompiledPlan.h"

namespace LE
{

const FRFGGraphSignature& FRFGCompiledPlan::GetSignature() const
{
    return Signature;
}

const LE::Array<FRFGCompiledPass>& FRFGCompiledPlan::GetPasses() const
{
    return CompiledPasses;
}

const LE::Array<FRFGCompiledResourceLife>& FRFGCompiledPlan::GetResourceLifetimes() const
{
    return ResourceLifetimes;
}

FRFGGraphSignature& FRFGCompiledPlan::GetMutableSignature()
{
    return Signature;
}

LE::Array<FRFGCompiledPass>& FRFGCompiledPlan::GetMutablePasses()
{
    return CompiledPasses;
}

LE::Array<FRFGCompiledResourceLife>& FRFGCompiledPlan::GetMutableResourceLifetimes()
{
    return ResourceLifetimes;
}

void FRFGCompiledPlan::Clear()
{
    Signature = {};
    CompiledPasses.Clear();
    ResourceLifetimes.Clear();
}

} // namespace LE
