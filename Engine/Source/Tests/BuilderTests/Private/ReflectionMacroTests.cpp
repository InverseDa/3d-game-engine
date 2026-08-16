#include "Reflection/ReflectionMacros.h"
#include "Reflection/ReflectionRegistry.h"

#include <cstddef>
#include <cstdio>
#include <type_traits>

namespace
{

static_assert(LE::Detail::IsValidReflectionStableIdLiteral(
    "60000000-0000-4000-8000-000000000001"),
    "canonical non-nil reflection IDs must be accepted");
static_assert(!LE::Detail::IsValidReflectionStableIdLiteral(
    "00000000-0000-0000-0000-000000000000"),
    "the nil UUID is not a stable reflection ID");
static_assert(!LE::Detail::IsValidReflectionStableIdLiteral(
    "60000000-0000-4000-8000-00000000000"),
    "short UUID spellings must be rejected");
static_assert(!LE::Detail::IsValidReflectionStableIdLiteral(
    "60000000_0000-4000-8000-000000000001"),
    "misplaced UUID separators must be rejected");
static_assert(!LE::Detail::IsValidReflectionStableIdLiteral(
    "60000000-0000-4000-8000-00000000000g"),
    "non-hex UUID digits must be rejected");
static_assert(!LE::Detail::IsValidReflectionStableIdLiteral(
    "60000000-0000-4000-8000-00000000000A"),
    "uppercase UUID digits must be rejected");

struct FReflectionMacroLayoutControl
{
    int Value = 0;
    unsigned char State = 0;
};

LE_STRUCT("60000000-0000-4000-8000-000000000002")
struct FReflectionMacroStruct
{
    LE_GENERATED_BODY()

    LE_PROPERTY("60000000-0000-4000-8000-000000000003")
    int Value = 0;
    unsigned char State = 0;
};

LE_CLASS("60000000-0000-4000-8000-000000000004")
class FReflectionMacroClass
{
    LE_GENERATED_BODY()

public:
    explicit FReflectionMacroClass(const int InValue) noexcept
        : Value(InValue)
    {
    }

private:
    LE_PROPERTY("60000000-0000-4000-8000-000000000005")
    int Value = 0;
};

LE_ENUM("60000000-0000-4000-8000-000000000006")
enum class EReflectionMacroEnum : unsigned char
{
    First = 1,
};

static_assert(sizeof(FReflectionMacroStruct) == sizeof(FReflectionMacroLayoutControl) &&
        alignof(FReflectionMacroStruct) == alignof(FReflectionMacroLayoutControl),
    "reflection declaration macros must not change native object layout");
static_assert(std::is_standard_layout<FReflectionMacroStruct>::value,
    "reflection declaration macros must preserve standard-layout types");

int ExpectReflectionMacro(const bool bCondition, const char* const Name)
{
    if (bCondition)
    {
        return 0;
    }
    std::fprintf(stderr, "Reflection macro test failed: %s\n", Name);
    return 1;
}

} // namespace

namespace LE
{
namespace Detail
{

template <>
struct TReflectionGeneratedAccess<FReflectionMacroClass>
{
    static int ReadValue(const FReflectionMacroClass& Instance) noexcept
    {
        return Instance.Value;
    }
};

} // namespace Detail

int RunReflectionMacroTests()
{
    int Failures = 0;
    const FReflectionMacroClass Instance(37);
    Failures += ExpectReflectionMacro(
        Detail::TReflectionGeneratedAccess<FReflectionMacroClass>::ReadValue(Instance) == 37,
        "the generated-access hook reaches a private annotated property");

    FReflectionRegistry Registry;
    Failures += ExpectReflectionMacro(
        Registry.GetModuleCount() == 0 && Registry.GetTypeCount() == 0 &&
            Registry.GetEnumCount() == 0,
        "declaration macros perform no automatic static registration");
    Failures += ExpectReflectionMacro(
        static_cast<unsigned char>(EReflectionMacroEnum::First) == 1,
        "the enum declaration macro preserves the native enum value");
    return Failures;
}

} // namespace LE
