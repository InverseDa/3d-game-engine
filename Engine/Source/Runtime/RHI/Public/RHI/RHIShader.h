#pragma once

#include "CoreMinimal.h"
#include "RHITypes.h"
#include "RHIResource.h"
#include "RHIDescription.h"

namespace LE
{

class RHI_API FRHIShader : public FRHIResource
{
public:
    ~FRHIShader() override = default;

public:
    virtual const FRHIShaderDesc& GetDesc() const = 0;

    EShaderStage GetStage() const { return this->GetDesc().Stage; }
    const LE::String& GetEntryPoint() const { return this->GetDesc().EntryPoint; }
    const void* GetByteCode() const { return this->GetDesc().ByteCode; }
    uint64 GetByteCodeSize() const { return this->GetDesc().ByteCodeSize; }
};

} // namespace LE
