#pragma once

#include "CoreMinimal.h"
#include "RALResource.h"
#include "RALDescription.h"

namespace LE
{

class RAL_API FRALTexture : public FRALResource
{
public:
    virtual ~FRALTexture() = default;

public:
    virtual const FRALTextureDesc& GetDesc() const = 0;
};

class RAL_API FRALTextureView : public FRALResource
{
public:
    virtual ~FRALTextureView() = default;

public:
    virtual FRALTexture* GetTexture() const = 0;
    virtual const FRALTextureViewDesc& GetDesc() const = 0;

};

} // namespace LE
