#pragma once

#include "CoreMinimal.h"
#include "RHIResource.h"
#include "RHIDescription.h"

namespace LE
{

class RHI_API FRHITexture : public FRHIResource
{
public:
    virtual ~FRHITexture() = default;

public:
    virtual const FRHITextureDesc& GetDesc() const = 0;
};

class RHI_API FRHITextureView : public FRHIResource
{
public:
    virtual ~FRHITextureView() = default;

public:
    virtual FRHITexture* GetTexture() const = 0;
    virtual const FRHITextureViewDesc& GetDesc() const = 0;

};

} // namespace LE
