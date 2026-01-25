#pragma once

#include "CoreMinimal.h"

class RAL_API FRALResource : public FNonCopyable
{
public:
    FRALResource() = default;
    virtual ~FRALResource() = default;

public:
    virtual uint32 GetBindlessIndex() const = 0;
};