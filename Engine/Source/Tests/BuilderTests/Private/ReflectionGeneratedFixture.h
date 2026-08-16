#pragma once

#include "Reflection/ReflectionMinimal.h"

namespace LE
{

LE_STRUCT("61000000-0000-4000-8000-000000000001")
struct FGeneratedValue
{
    LE_GENERATED_BODY()

    LE_PROPERTY("61000000-0000-4000-8000-000000000007")
    int32 Payload = 17;
};

LE_STRUCT("61000000-0000-4000-8000-000000000002")
struct FGeneratedReadOnlyValue
{
    LE_GENERATED_BODY()

    FGeneratedReadOnlyValue() noexcept = default;
    FGeneratedReadOnlyValue& operator=(const FGeneratedReadOnlyValue&) = delete;
};

LE_ENUM("61000000-0000-4000-8000-000000000006")
enum class BUILDERTESTS_API EGeneratedMode : uint8
{
    First = 0,
    Maximum = 0xff,
};

LE_CLASS("61000000-0000-4000-8000-000000000003")
class BUILDERTESTS_API FGeneratedPrivateOwner
{
    LE_GENERATED_BODY()

private:
    FGeneratedPrivateOwner() noexcept = default;
    ~FGeneratedPrivateOwner() noexcept = default;

    LE_PROPERTY("61000000-0000-4000-8000-000000000004")
    FGeneratedValue Value;

    LE_PROPERTY("61000000-0000-4000-8000-000000000005")
    FGeneratedReadOnlyValue ReadOnly;

    LE_PROPERTY("61000000-0000-4000-8000-000000000008")
    EGeneratedMode Mode = EGeneratedMode::Maximum;

    LE_PROPERTY("61000000-0000-4000-8000-000000000009")
    String Title = "Generated owner";
};

} // namespace LE

namespace LE
{

BUILDERTESTS_API EReflectionRegisterResult RegisterBuilderTestsReflection(
    FReflectionRegistry& Registry,
    FReflectionModuleHandle& OutHandle) noexcept;
BUILDERTESTS_API EReflectionUnregisterResult UnregisterBuilderTestsReflection(
    FReflectionRegistry& Registry,
    FReflectionModuleHandle Handle) noexcept;

} // namespace LE
