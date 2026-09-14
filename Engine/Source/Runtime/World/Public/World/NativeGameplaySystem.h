#pragma once

#include "Containers/Span.h"
#include "Containers/StringView.h"
#include "Types/EngineTypes.h"

#include <limits>

namespace LE
{

class FWorld;
class FWorldCommandBuffer;

constexpr uint32 MaxNativeGameplayFixedSubsteps = 1024;

enum class ENativeGameplaySystemPhase : uint8
{
    Fixed = 0,
    Variable,
};

enum class ENativeGameplaySystemResult : uint8
{
    Success = 0,
    Failure,
};

struct FNativeGameplaySystemUpdateContext
{
    FWorld* World = nullptr;
    FWorldCommandBuffer* Commands = nullptr;
    uint64 FrameIndex = 0;
    double FrameDeltaSeconds = 0.0;
    double FrameElapsedSeconds = 0.0;
    double StepDeltaSeconds = 0.0;
    double StepElapsedSeconds = 0.0;
    uint64 FixedTickIndex = 0;
    uint32 FixedSubstepIndex = 0;
    ENativeGameplaySystemPhase Phase = ENativeGameplaySystemPhase::Variable;
};

using FNativeGameplaySystemUpdate = ENativeGameplaySystemResult (*)(
    void* ProviderContext,
    const FNativeGameplaySystemUpdateContext& Context) noexcept;

// Name and Dependencies are copied during registration. ProviderContext and
// Update remain borrowed; their provider must stay alive and loaded until the
// system is unregistered or its FWorld is destroyed.
struct FNativeGameplaySystemDescriptor
{
    StringView Name;
    Span<const StringView> Dependencies;
    void* ProviderContext = nullptr;
    FNativeGameplaySystemUpdate Update = nullptr;
    ENativeGameplaySystemPhase Phase = ENativeGameplaySystemPhase::Variable;
    bool bEnabled = true;
};

struct FNativeGameplayFixedStepConfig
{
    double FixedDeltaSeconds = 1.0 / 60.0;
    uint32 MaxSubsteps = 4;
    // Inclusive final unique index. Exhaustion never wraps or repeats it.
    uint64 LastFixedTickIndex = (std::numeric_limits<uint64>::max)();
};

enum class ENativeGameplaySystemRegisterResult : uint8
{
    Success = 0,
    InvalidWorld,
    InvalidDescriptor,
    DuplicateName,
    InvalidState,
    OutOfMemory,
};

enum class ENativeGameplaySystemUnregisterResult : uint8
{
    Success = 0,
    InvalidWorld,
    NotFound,
    InvalidState,
};

enum class ENativeGameplaySystemEnableResult : uint8
{
    Success = 0,
    InvalidWorld,
    NotFound,
    InvalidState,
};

enum class ENativeGameplayFixedStepConfigResult : uint8
{
    Success = 0,
    InvalidWorld,
    InvalidConfig,
    InvalidState,
};

enum class ENativeGameplayScheduleResult : uint8
{
    Success = 0,
    InvalidWorld,
    InvalidState,
    MissingDependency,
    CrossPhaseDependency,
    DependencyCycle,
    OutOfMemory,
};

enum class ENativeGameplayUpdateResult : uint8
{
    Success = 0,
    InvalidWorld,
    InvalidState,
    DirtySchedule,
    SystemFailed,
    TimelineExhausted,
};

struct FNativeGameplayUpdateReport
{
    uint32 FixedStepsAttempted = 0;
    uint32 FixedStepsCompleted = 0;
    uint32 FixedCallbacksAttempted = 0;
    uint32 VariableCallbacksAttempted = 0;
    // Only denotes an issuable next index when bFixedTimelineExhausted is false.
    uint64 NextFixedTickIndex = 0;
    double FixedAccumulatorSeconds = 0.0;
    double FixedSimulationSeconds = 0.0;
    double DroppedSeconds = 0.0;
    // Borrowed from the registry; invalidated by a successful registry mutation
    // or FWorld destruction.
    StringView FailedSystemName;
    uint32 FailedSubstepIndex = 0;
    ENativeGameplaySystemPhase FailedPhase = ENativeGameplaySystemPhase::Variable;
    bool bFixedTimelineExhausted = false;
};

} // namespace LE
