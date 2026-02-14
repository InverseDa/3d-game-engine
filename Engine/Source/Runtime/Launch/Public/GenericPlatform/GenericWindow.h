#pragma once

#include "CoreMinimal.h"

class IGenericWindow
{
public:
    virtual ~IGenericWindow() = default;

    virtual void* GetNativeHandle() const = 0;
    virtual bool ProcessMessages() = 0;
    virtual void GetSize(uint32& OutWidth, uint32& OutHeight) const = 0;
    virtual bool IsMinimized() const = 0;
};
