#pragma once
#include "RHIDescription.h"
#include "RHIResource.h"

namespace LE
{

class RHI_API FRHIBindGroupLayout : public FRHIResource
{
public:
    ~FRHIBindGroupLayout() override = default;
    virtual const FRHIBindGroupLayoutDesc& GetDesc() const = 0;
};

class RHI_API FRHIBindGroup : public FRHIResource
{
public:
    ~FRHIBindGroup() override = default;
    virtual const FRHIBindGroupDesc& GetDesc() const = 0;
};

} // namespace LE
