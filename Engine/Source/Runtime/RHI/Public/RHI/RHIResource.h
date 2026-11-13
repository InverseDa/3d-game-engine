#pragma once

#include "CoreMinimal.h"

namespace LE
{

class RHI_API FRHIResource : public FNonCopyable
{
public:
    FRHIResource() = default;
    virtual ~FRHIResource() = default;

public:
    virtual uint32 GetBindlessIndex() const { return 0xFFFFFFFF; }
};

} // namespace LE
