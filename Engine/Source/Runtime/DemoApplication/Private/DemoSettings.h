#pragma once

#include "Reflection/ReflectionMinimal.h"

namespace LE
{

// Private demo configuration used to exercise the real reflection/property
// serialization path without creating a public settings or asset API.
LE_STRUCT("72000000-0000-4000-8000-000000000001")
struct FDemoSettings
{
    LE_GENERATED_BODY()

    LE_PROPERTY("72000000-0000-4000-8000-000000000002")
    uint32 WindowWidth = 1280;

    LE_PROPERTY("72000000-0000-4000-8000-000000000003")
    uint32 WindowHeight = 720;

    LE_PROPERTY("72000000-0000-4000-8000-000000000004")
    String WindowTitle = "Limitless Engine - RFG Composite Triangle Demo";
};

EReflectionRegisterResult RegisterDemoApplicationReflection(
    FReflectionRegistry& Registry,
    FReflectionModuleHandle& OutHandle) noexcept;
EReflectionUnregisterResult UnregisterDemoApplicationReflection(
    FReflectionRegistry& Registry,
    FReflectionModuleHandle Handle) noexcept;

} // namespace LE
