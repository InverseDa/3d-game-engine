#pragma once

#include "CoreMinimal.h"
#include "RALTypes.h"
#include "RALResource.h"
#include "RALDescription.h"

class RAL_API FRALShader : public FRALResource
{
public:
    ~FRALShader() override = default;

public:
    virtual const FRALShaderDesc& GetDesc() const = 0;

    EShaderStage GetStage() const { return this->GetDesc().Stage; }
    const FString& GetEntryPoint() const { return this->GetDesc().EntryPoint; }
    const void* GetByteCode() const { return this->GetDesc().ByteCode; }
    uint64 GetByteCodeSize() const { return this->GetDesc().ByteCodeSize; }
};
