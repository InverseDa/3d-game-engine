#include "Types/EngineTypes.h"

#include <ostream>
#include <utility>

FString::FString() = default;

FString::FString(const UWString& InString)
    : Real(InString)
{
}

FString::FString(const char* InString)
    : Real(InString ? InString : "")
{
}

FString::FString(const FString& Other)
    : Real(Other.Real)
{
}

FString::FString(FString&& Other) noexcept
    : Real(std::move(Other.Real))
{
}

FString::~FString() = default;

FString& FString::operator=(const FString& Other)
{
    if (this != &Other)
    {
        Real = Other.Real;
    }
    return *this;
}

FString& FString::operator=(FString&& Other) noexcept
{
    if (this != &Other)
    {
        Real = std::move(Other.Real);
    }
    return *this;
}

const char* FString::operator*() const
{
    return Real.c_str();
}

const char* FString::GetData() const
{
    return Real.c_str();
}

bool FString::IsEmpty() const
{
    return Real.empty();
}

int32 FString::Length() const
{
    return static_cast<int32>(Real.length());
}

FString FString::operator+(const FString& Other) const
{
    return FString(Real + Other.Real);
}

FString& FString::operator+=(const FString& Other)
{
    Real += Other.Real;
    return *this;
}

bool FString::operator==(const FString& Other) const
{
    return Real == Other.Real;
}

bool FString::operator!=(const FString& Other) const
{
    return Real != Other.Real;
}

std::ostream& operator<<(std::ostream& Os, const FString& Str)
{
    Os << Str.Real;
    return Os;
}
