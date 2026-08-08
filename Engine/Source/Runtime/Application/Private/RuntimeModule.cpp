#include "Application/RuntimeModule.h"

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Memory/Allocator.h"

#include <cstddef>
#include <new>

namespace LE
{
namespace
{
struct FRegisteredRuntimeModule
{
    String Name;
    Array<String> Dependencies;
    void* Context = nullptr;
    FRuntimeModuleStartup Startup = nullptr;
    FRuntimeModuleShutdown Shutdown = nullptr;
};

bool IsAsciiLetter(const char Character) noexcept
{
    return (Character >= 'A' && Character <= 'Z') ||
        (Character >= 'a' && Character <= 'z');
}

bool IsAsciiDigit(const char Character) noexcept
{
    return Character >= '0' && Character <= '9';
}

bool IsValidModuleIdentifier(const StringView Name) noexcept
{
    if (Name.IsEmpty() || (!IsAsciiLetter(Name[0]) && !IsAsciiDigit(Name[0])))
    {
        return false;
    }

    for (std::size_t Index = 0; Index < Name.Size(); ++Index)
    {
        const char Character = Name[Index];
        if (!IsAsciiLetter(Character) && !IsAsciiDigit(Character) &&
            Character != '.' && Character != '_' && Character != '-')
        {
            return false;
        }
    }
    return true;
}

std::size_t FindModuleIndex(
    const Array<FRegisteredRuntimeModule>& Modules,
    const StringView Name) noexcept
{
    for (std::size_t Index = 0; Index < Modules.Size(); ++Index)
    {
        if (Modules[Index].Name == Name)
        {
            return Index;
        }
    }
    return Modules.Size();
}
} // namespace

struct FRuntimeModuleRegistry::FImpl
{
    Array<FRegisteredRuntimeModule> Modules;
    Array<std::size_t> StartupOrder;
    ERuntimeModuleRegistryState State = ERuntimeModuleRegistryState::Registering;
    ERuntimeModuleRegisterResult RegistrationFailure = ERuntimeModuleRegisterResult::Success;
    ERuntimeModuleRegisterResult LastRegisterResult = ERuntimeModuleRegisterResult::Success;
    ERuntimeModuleStartupResult LastStartupResult = ERuntimeModuleStartupResult::NotStarted;
    String DiagnosticModuleName;
    String DiagnosticDependencyName;

    void RecordRegistrationFailure(
        const ERuntimeModuleRegisterResult Result,
        const StringView ModuleName,
        const StringView DependencyName = {})
    {
        LastRegisterResult = Result;
        if (RegistrationFailure == ERuntimeModuleRegisterResult::Success)
        {
            RegistrationFailure = Result;
            DiagnosticModuleName = ModuleName;
            DiagnosticDependencyName = DependencyName;
        }
    }

    void SetStartupDiagnostic(
        const StringView ModuleName,
        const StringView DependencyName = {})
    {
        DiagnosticModuleName = ModuleName;
        DiagnosticDependencyName = DependencyName;
    }

    void RollbackActiveModules() noexcept
    {
        while (!StartupOrder.IsEmpty())
        {
            const std::size_t ModuleIndex = StartupOrder.Back();
            StartupOrder.PopBack();
            FRegisteredRuntimeModule& Module = Modules[ModuleIndex];
            Module.Shutdown(Module.Context);
        }
    }
};

FRuntimeModuleRegistry::FRuntimeModuleRegistry() noexcept
{
    IAllocator& Allocator = GetDefaultAllocator();
    Impl = AllocateArray<FImpl>(Allocator, 1);
    new (Impl) FImpl();
}

FRuntimeModuleRegistry::~FRuntimeModuleRegistry() noexcept
{
    ShutdownAll();
    if (Impl != nullptr)
    {
        Impl->~FImpl();
        DeallocateArray(GetDefaultAllocator(), Impl, 1);
        Impl = nullptr;
    }
}

ERuntimeModuleRegisterResult FRuntimeModuleRegistry::RegisterModule(
    const FRuntimeModuleDescriptor& Descriptor)
{
    if (Impl->State != ERuntimeModuleRegistryState::Registering)
    {
        Impl->LastRegisterResult = ERuntimeModuleRegisterResult::InvalidState;
        return Impl->LastRegisterResult;
    }

    if (!IsValidModuleIdentifier(Descriptor.Name) ||
        Descriptor.Startup == nullptr || Descriptor.Shutdown == nullptr)
    {
        Impl->RecordRegistrationFailure(
            ERuntimeModuleRegisterResult::InvalidDescriptor,
            Descriptor.Name);
        return ERuntimeModuleRegisterResult::InvalidDescriptor;
    }

    for (std::size_t DependencyIndex = 0;
         DependencyIndex < Descriptor.Dependencies.Size();
         ++DependencyIndex)
    {
        const StringView DependencyName = Descriptor.Dependencies[DependencyIndex];
        if (!IsValidModuleIdentifier(DependencyName))
        {
            Impl->RecordRegistrationFailure(
                ERuntimeModuleRegisterResult::InvalidDescriptor,
                Descriptor.Name,
                DependencyName);
            return ERuntimeModuleRegisterResult::InvalidDescriptor;
        }
        for (std::size_t PreviousIndex = 0; PreviousIndex < DependencyIndex; ++PreviousIndex)
        {
            if (Descriptor.Dependencies[PreviousIndex] == DependencyName)
            {
                Impl->RecordRegistrationFailure(
                    ERuntimeModuleRegisterResult::InvalidDescriptor,
                    Descriptor.Name,
                    DependencyName);
                return ERuntimeModuleRegisterResult::InvalidDescriptor;
            }
        }
    }

    if (FindModuleIndex(Impl->Modules, Descriptor.Name) != Impl->Modules.Size())
    {
        Impl->RecordRegistrationFailure(
            ERuntimeModuleRegisterResult::DuplicateName,
            Descriptor.Name);
        return ERuntimeModuleRegisterResult::DuplicateName;
    }

    FRegisteredRuntimeModule Module;
    Module.Name = Descriptor.Name;
    Module.Context = Descriptor.Context;
    Module.Startup = Descriptor.Startup;
    Module.Shutdown = Descriptor.Shutdown;
    Module.Dependencies.Reserve(Descriptor.Dependencies.Size());
    for (const StringView DependencyName : Descriptor.Dependencies)
    {
        Module.Dependencies.EmplaceBack(DependencyName);
    }
    Impl->Modules.PushBack(std::move(Module));
    Impl->LastRegisterResult = ERuntimeModuleRegisterResult::Success;
    return ERuntimeModuleRegisterResult::Success;
}

ERuntimeModuleStartupResult FRuntimeModuleRegistry::StartupAll()
{
    if (Impl->State != ERuntimeModuleRegistryState::Registering)
    {
        Impl->LastStartupResult = ERuntimeModuleStartupResult::InvalidState;
        return Impl->LastStartupResult;
    }

    if (Impl->RegistrationFailure != ERuntimeModuleRegisterResult::Success)
    {
        Impl->State = ERuntimeModuleRegistryState::Failed;
        Impl->LastStartupResult = ERuntimeModuleStartupResult::InvalidRegistration;
        return Impl->LastStartupResult;
    }

    Impl->DiagnosticModuleName.Clear();
    Impl->DiagnosticDependencyName.Clear();

    for (const FRegisteredRuntimeModule& Module : Impl->Modules)
    {
        for (const String& DependencyName : Module.Dependencies)
        {
            if (FindModuleIndex(Impl->Modules, DependencyName) == Impl->Modules.Size())
            {
                Impl->SetStartupDiagnostic(Module.Name, DependencyName);
                Impl->State = ERuntimeModuleRegistryState::Failed;
                Impl->LastStartupResult = ERuntimeModuleStartupResult::MissingDependency;
                return Impl->LastStartupResult;
            }
        }
    }

    Array<uint8> Planned;
    Planned.Resize(Impl->Modules.Size());
    Array<std::size_t> Plan;
    Plan.Reserve(Impl->Modules.Size());

    while (Plan.Size() < Impl->Modules.Size())
    {
        std::size_t SelectedIndex = Impl->Modules.Size();
        for (std::size_t ModuleIndex = 0; ModuleIndex < Impl->Modules.Size(); ++ModuleIndex)
        {
            if (Planned[ModuleIndex] != 0)
            {
                continue;
            }

            const FRegisteredRuntimeModule& Candidate = Impl->Modules[ModuleIndex];
            bool bReady = true;
            for (const String& DependencyName : Candidate.Dependencies)
            {
                const std::size_t DependencyIndex = FindModuleIndex(Impl->Modules, DependencyName);
                if (Planned[DependencyIndex] == 0)
                {
                    bReady = false;
                    break;
                }
            }
            if (bReady && (SelectedIndex == Impl->Modules.Size() ||
                Candidate.Name < Impl->Modules[SelectedIndex].Name))
            {
                SelectedIndex = ModuleIndex;
            }
        }

        if (SelectedIndex == Impl->Modules.Size())
        {
            // Pick a stable diagnostic name from the remaining cycle.
            for (std::size_t ModuleIndex = 0; ModuleIndex < Impl->Modules.Size(); ++ModuleIndex)
            {
                if (Planned[ModuleIndex] == 0 &&
                    (Impl->DiagnosticModuleName.IsEmpty() ||
                     Impl->Modules[ModuleIndex].Name < Impl->DiagnosticModuleName))
                {
                    Impl->SetStartupDiagnostic(Impl->Modules[ModuleIndex].Name);
                }
            }
            Impl->State = ERuntimeModuleRegistryState::Failed;
            Impl->LastStartupResult = ERuntimeModuleStartupResult::DependencyCycle;
            return Impl->LastStartupResult;
        }

        Planned[SelectedIndex] = 1;
        Plan.PushBack(SelectedIndex);
    }

    Impl->StartupOrder.Reserve(Plan.Size());
    for (const std::size_t ModuleIndex : Plan)
    {
        FRegisteredRuntimeModule& Module = Impl->Modules[ModuleIndex];
        if (!Module.Startup(Module.Context))
        {
            Impl->SetStartupDiagnostic(Module.Name);
            Module.Shutdown(Module.Context);
            Impl->RollbackActiveModules();
            Impl->State = ERuntimeModuleRegistryState::Failed;
            Impl->LastStartupResult = ERuntimeModuleStartupResult::ModuleStartupFailed;
            return Impl->LastStartupResult;
        }
        Impl->StartupOrder.PushBack(ModuleIndex);
    }

    Impl->State = ERuntimeModuleRegistryState::Running;
    Impl->LastStartupResult = ERuntimeModuleStartupResult::Success;
    return Impl->LastStartupResult;
}

void FRuntimeModuleRegistry::ShutdownAll() noexcept
{
    if (Impl == nullptr || Impl->State == ERuntimeModuleRegistryState::Shutdown)
    {
        return;
    }

    Impl->RollbackActiveModules();
    Impl->State = ERuntimeModuleRegistryState::Shutdown;
}

ERuntimeModuleRegistryState FRuntimeModuleRegistry::GetState() const noexcept
{
    return Impl->State;
}

ERuntimeModuleRegisterResult FRuntimeModuleRegistry::GetLastRegisterResult() const noexcept
{
    return Impl->LastRegisterResult;
}

ERuntimeModuleRegisterResult FRuntimeModuleRegistry::GetRegistrationFailure() const noexcept
{
    return Impl->RegistrationFailure;
}

ERuntimeModuleStartupResult FRuntimeModuleRegistry::GetLastStartupResult() const noexcept
{
    return Impl->LastStartupResult;
}

StringView FRuntimeModuleRegistry::GetDiagnosticModuleName() const noexcept
{
    return Impl->DiagnosticModuleName;
}

StringView FRuntimeModuleRegistry::GetDiagnosticDependencyName() const noexcept
{
    return Impl->DiagnosticDependencyName;
}

} // namespace LE
