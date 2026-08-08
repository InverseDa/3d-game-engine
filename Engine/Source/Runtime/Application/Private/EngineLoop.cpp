#include "Application/EngineLoop.h"

#include "Platform/PlatformTime.h"

namespace LE
{
namespace
{
double DefaultTimeSource()
{
    return FPlatformTime::Seconds();
}
} // namespace

FEngineLoop::~FEngineLoop() noexcept
{
    Shutdown();
}

EEngineLoopInitializeResult FEngineLoop::Initialize(
    IApplication& InApplication,
    const FEngineLoopConfig& Config)
{
    if (State != EEngineLoopState::Uninitialized)
    {
        return EEngineLoopInitializeResult::InvalidState;
    }

    Application = &InApplication;
    TimeSource = Config.TimeSource != nullptr ? Config.TimeSource : &DefaultTimeSource;

    Application->RegisterModules(ModuleRegistry);
    const ERuntimeModuleStartupResult ModuleStartupResult = ModuleRegistry.StartupAll();
    if (ModuleStartupResult != ERuntimeModuleStartupResult::Success)
    {
        State = EEngineLoopState::ExitRequested;
        switch (ModuleStartupResult)
        {
        case ERuntimeModuleStartupResult::InvalidRegistration:
        case ERuntimeModuleStartupResult::InvalidState:
        case ERuntimeModuleStartupResult::NotStarted:
            return EEngineLoopInitializeResult::ModuleRegistrationFailed;
        case ERuntimeModuleStartupResult::MissingDependency:
        case ERuntimeModuleStartupResult::DependencyCycle:
            return EEngineLoopInitializeResult::ModuleDependencyValidationFailed;
        case ERuntimeModuleStartupResult::ModuleStartupFailed:
            return EEngineLoopInitializeResult::ModuleStartupFailed;
        case ERuntimeModuleStartupResult::Success:
            break;
        }
    }

    bApplicationInitializeAttempted = true;
    if (!Application->Initialize())
    {
        Application->Shutdown();
        bApplicationShutdown = true;
        ModuleRegistry.ShutdownAll();
        State = EEngineLoopState::ExitRequested;
        return EEngineLoopInitializeResult::ApplicationInitializeFailed;
    }

    State = EEngineLoopState::Running;
    return EEngineLoopInitializeResult::Success;
}

EEngineLoopTickResult FEngineLoop::Tick()
{
    if (State != EEngineLoopState::Running)
    {
        return EEngineLoopTickResult::Exit;
    }

    const double CurrentSeconds = TimeSource();
    double DeltaSeconds = 0.0;
    if (bHasTickTimestamp && CurrentSeconds > LastTickSeconds)
    {
        DeltaSeconds = CurrentSeconds - LastTickSeconds;
        ElapsedSeconds += DeltaSeconds;
    }
    if (!bHasTickTimestamp || CurrentSeconds > LastTickSeconds)
    {
        LastTickSeconds = CurrentSeconds;
    }
    bHasTickTimestamp = true;

    FApplicationTickContext Context;
    Context.FrameIndex = NextFrameIndex;
    Context.DeltaSeconds = DeltaSeconds;
    Context.ElapsedSeconds = ElapsedSeconds;

    constexpr EApplicationFramePhase Phases[] = {
        EApplicationFramePhase::ProcessPlatformEvents,
        EApplicationFramePhase::Update,
        EApplicationFramePhase::Render,
    };

    for (const EApplicationFramePhase Phase : Phases)
    {
        Context.Phase = Phase;
        if (Application->Tick(Context) == EApplicationTickResult::Exit)
        {
            ++NextFrameIndex;
            State = EEngineLoopState::ExitRequested;
            return EEngineLoopTickResult::Exit;
        }
    }

    ++NextFrameIndex;
    return EEngineLoopTickResult::Continue;
}

void FEngineLoop::Shutdown() noexcept
{
    if (State == EEngineLoopState::Shutdown)
    {
        return;
    }

    if (Application != nullptr && bApplicationInitializeAttempted && !bApplicationShutdown)
    {
        Application->Shutdown();
        bApplicationShutdown = true;
    }

    ModuleRegistry.ShutdownAll();

    Application = nullptr;
    TimeSource = nullptr;
    State = EEngineLoopState::Shutdown;
}

} // namespace LE
