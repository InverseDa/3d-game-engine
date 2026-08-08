#pragma once

#include "Application/Application.h"
#include "Application/RuntimeModule.h"

namespace LE
{

using FEngineLoopTimeSource = double (*)();

struct FEngineLoopConfig
{
    // Null selects FPlatformTime::Seconds in the Application implementation.
    // The callback is borrowed and must remain valid through Shutdown.
    FEngineLoopTimeSource TimeSource = nullptr;
};

enum class EEngineLoopInitializeResult : uint8
{
    Success = 0,
    ModuleRegistrationFailed,
    ModuleDependencyValidationFailed,
    ModuleStartupFailed,
    ApplicationInitializeFailed,
    InvalidState,
};

enum class EEngineLoopTickResult : uint8
{
    Continue = 0,
    Exit,
};

enum class EEngineLoopState : uint8
{
    Uninitialized = 0,
    Running,
    ExitRequested,
    Shutdown,
};

class APPLICATION_API FEngineLoop final
{
public:
    FEngineLoop() noexcept = default;
    ~FEngineLoop() noexcept;

    FEngineLoop(const FEngineLoop&) = delete;
    FEngineLoop& operator=(const FEngineLoop&) = delete;
    FEngineLoop(FEngineLoop&&) = delete;
    FEngineLoop& operator=(FEngineLoop&&) = delete;

    EEngineLoopInitializeResult Initialize(
        IApplication& InApplication,
        const FEngineLoopConfig& Config = {});

    // One accepted Tick samples the clock exactly once and calls the application
    // in ProcessPlatformEvents -> Update -> Render order. All phases share one
    // immutable context. Exit short-circuits the remaining phases.
    EEngineLoopTickResult Tick();

    // Safe after every Initialize result and safe to repeat. The borrowed
    // application receives Shutdown at most once.
    void Shutdown() noexcept;

    EEngineLoopState GetState() const noexcept { return State; }
    uint64 GetNextFrameIndex() const noexcept { return NextFrameIndex; }
    const FRuntimeModuleRegistry& GetModuleRegistry() const noexcept { return ModuleRegistry; }

private:
    IApplication* Application = nullptr;
    FRuntimeModuleRegistry ModuleRegistry;
    FEngineLoopTimeSource TimeSource = nullptr;
    EEngineLoopState State = EEngineLoopState::Uninitialized;
    uint64 NextFrameIndex = 0;
    double LastTickSeconds = 0.0;
    double ElapsedSeconds = 0.0;
    bool bHasTickTimestamp = false;
    bool bApplicationInitializeAttempted = false;
    bool bApplicationShutdown = false;
};

} // namespace LE
