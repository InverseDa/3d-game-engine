#pragma once

#include "CoreMinimal.h"

class RAL_API FRALResource
{
public:
    FRALResource() = default;
    virtual ~FRALResource() = default;

    // Disallow copying
    FRALResource(const FRALResource&) = delete;
    FRALResource& operator=(const FRALResource&) = delete;

    // allow moving
    FRALResource(FRALResource&&) = default;
    FRALResource& operator=(FRALResource&&) = default;
};