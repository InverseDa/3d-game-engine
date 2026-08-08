#pragma once

#include "CoreMinimal.h"

namespace LE
{

class FRALResource;

/**
 * Borrowed, non-owning destination for RFG-owned transient resources.
 *
 * A true return value transfers ownership of Resource to the sink immediately.
 * RFG never stores or deletes the sink itself.
 */
class RFG_API IRFGDeferredReleaseSink
{
public:
    virtual bool DeferRelease(FRALResource* Resource) noexcept = 0;

protected:
    ~IRFGDeferredReleaseSink() = default;
};

} // namespace LE
