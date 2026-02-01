#pragma once
#include "RALDescription.h"
#include "RALResource.h"

class RAL_API FRALBindGroupLayout : public FRALResource
{
public:
    ~FRALBindGroupLayout() override = default;
    virtual const FRALBindGroupLayoutDesc& GetDesc() const = 0;
};

class RAL_API FRALBindGroup : public FRALResource
{
public:
    ~FRALBindGroup() override = default;
    virtual const FRALBindGroupDesc& GetDesc() const = 0;
};

