#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"

class RAL_API FRALSemaphore : public FRALResource
{
public:
    virtual ~FRALSemaphore() override = default;
};

class RAL_API FRALFence : public FRALResource
{
public:
    virtual ~FRALFence() override = default;
    virtual void Reset() = 0;
    virtual void Wait(uint64 Timeout = UINT64_MAX) = 0;
    virtual bool IsSignaled() = 0;
};
