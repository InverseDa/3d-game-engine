#pragma once

#include "CoreMinimal.h"
#include "RAL/RALDescription.h"

class FPlatformWindow
{
public:
    virtual ~FPlatformWindow() = default;

    virtual void* GetNativeHandle() const = 0;
    virtual FRALSurfaceDesc GetSurfaceDesc() const = 0;
    virtual bool ProcessMessages() = 0;
    virtual void GetSize(uint32& OutWidth, uint32& OutHeight) const = 0;
    virtual bool IsMinimized() const = 0;
};
