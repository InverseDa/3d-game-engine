#pragma once

#include "Containers/Span.h"
#include "Containers/StringView.h"
#include "Types/EngineTypes.h"

namespace LE
{

using FRuntimeModuleStartup = bool (*)(void* Context) noexcept;
using FRuntimeModuleShutdown = void (*)(void* Context) noexcept;

// Descriptors are borrowed only for the duration of RegisterModule. The
// registry owns copies of Name and Dependencies after a successful call.
// Names start with an ASCII letter/digit and otherwise use letters, digits,
// '.', '_', or '-'. Context may be null for stateless thunks; non-null context
// and both thunks must outlive registry shutdown. Runtime mutation, unload,
// restart, and DLL hot reload are intentionally unsupported.
// Shutdown must be safe after a false Startup result so it can release partial
// state; the registry invokes it once before rolling back earlier modules.
struct FRuntimeModuleDescriptor
{
    StringView Name;
    Span<const StringView> Dependencies;
    void* Context = nullptr;
    FRuntimeModuleStartup Startup = nullptr;
    FRuntimeModuleShutdown Shutdown = nullptr;
};

enum class ERuntimeModuleRegisterResult : uint8
{
    Success = 0,
    InvalidDescriptor,
    DuplicateName,
    InvalidState,
};

enum class ERuntimeModuleStartupResult : uint8
{
    NotStarted = 0,
    Success,
    InvalidRegistration,
    MissingDependency,
    DependencyCycle,
    ModuleStartupFailed,
    InvalidState,
};

enum class ERuntimeModuleRegistryState : uint8
{
    Registering = 0,
    Running,
    Failed,
    Shutdown,
};

class APPLICATION_API FRuntimeModuleRegistry final
{
public:
    FRuntimeModuleRegistry() noexcept;
    ~FRuntimeModuleRegistry() noexcept;

    FRuntimeModuleRegistry(const FRuntimeModuleRegistry&) = delete;
    FRuntimeModuleRegistry& operator=(const FRuntimeModuleRegistry&) = delete;
    FRuntimeModuleRegistry(FRuntimeModuleRegistry&&) = delete;
    FRuntimeModuleRegistry& operator=(FRuntimeModuleRegistry&&) = delete;

    ERuntimeModuleRegisterResult RegisterModule(const FRuntimeModuleDescriptor& Descriptor);
    ERuntimeModuleStartupResult StartupAll();

    // Stops active modules in strict reverse startup order. Safe to call after
    // every StartupAll result and safe to repeat.
    void ShutdownAll() noexcept;

    ERuntimeModuleRegistryState GetState() const noexcept;
    ERuntimeModuleRegisterResult GetLastRegisterResult() const noexcept;
    ERuntimeModuleRegisterResult GetRegistrationFailure() const noexcept;
    ERuntimeModuleStartupResult GetLastStartupResult() const noexcept;

    // Returned views refer to registry-owned storage and remain valid until
    // this registry is destroyed. They are empty when no matching diagnostic
    // exists.
    StringView GetDiagnosticModuleName() const noexcept;
    StringView GetDiagnosticDependencyName() const noexcept;

private:
    struct FImpl;
    FImpl* Impl = nullptr;
};

} // namespace LE
