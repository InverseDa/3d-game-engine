#pragma once

#include "Memory/UniquePtr.h"
#include "Types/EngineTypes.h"

namespace LE
{

class FRuntimeModuleRegistry;

enum class EApplicationFramePhase : uint8
{
    ProcessPlatformEvents = 0,
    Update,
    Render,
};

enum class EApplicationTickResult : uint8
{
    Continue = 0,
    Exit,
};

struct FApplicationTickContext
{
    EApplicationFramePhase Phase = EApplicationFramePhase::ProcessPlatformEvents;
    uint64 FrameIndex = 0;
    double DeltaSeconds = 0.0;
    double ElapsedSeconds = 0.0;
};

class APPLICATION_API IApplication
{
public:
    virtual ~IApplication() = default;

    // Registration must only describe modules; it must not start work or own
    // resources. The registry copies all names before this call returns.
    virtual void RegisterModules(FRuntimeModuleRegistry& Registry) = 0;
    virtual bool Initialize() = 0;
    virtual EApplicationTickResult Tick(const FApplicationTickContext& Context) = 0;
    virtual void Shutdown() noexcept = 0;
};

// FEngineLoop only borrows an IApplication. Concrete factories retain the
// creator-side destruction route in this pointer and the caller owns it.
using FApplicationPtr = LE::UniquePtr<IApplication>;

} // namespace LE
