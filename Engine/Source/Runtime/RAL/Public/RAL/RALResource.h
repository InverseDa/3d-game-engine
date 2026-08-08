#pragma once

#include "CoreMinimal.h"

namespace LE
{

class RAL_API FRALResource : public FNonCopyable
{
public:
    FRALResource() = default;
    virtual ~FRALResource() = default;

public:
    virtual uint32 GetBindlessIndex() const { return 0xFFFFFFFF; }
};

} // namespace LE
