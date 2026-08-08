#pragma once

#include "Containers/HashMap.h"
#include "Containers/StringView.h"
#include "Memory/Allocator.h"

#include <cstddef>
#include <cstring>
#include <iosfwd>
#include <limits>
#include <utility>

namespace LE
{

// An owned, allocator-backed sequence of UTF-8 bytes. Core validates storage
// safety, not Unicode scalar validity. Embedded NUL bytes are preserved.
class CORE_API String
{
public:
    using SizeType = std::size_t;

    String() noexcept = default;
    explicit String(IAllocator& Allocator) noexcept : AllocatorValue(Allocator) {}
    explicit String(const AllocatorRef Allocator) noexcept : AllocatorValue(Allocator) {}

    String(const char* const NullTerminated) noexcept
    {
        Assign(StringView(NullTerminated));
    }

    String(const char* const Data, const SizeType Size) noexcept
    {
        Assign(StringView(Data, Size));
    }

    String(const StringView View) noexcept
    {
        Assign(View);
    }

    String(const String& Other) noexcept
        : AllocatorValue(Other.AllocatorValue)
    {
        Assign(Other.View());
    }

    String(String&& Other) noexcept
        : DataValue(Other.DataValue)
        , SizeValue(Other.SizeValue)
        , CapacityValue(Other.CapacityValue)
        , AllocatorValue(Other.AllocatorValue)
    {
        Other.DataValue = nullptr;
        Other.SizeValue = 0;
        Other.CapacityValue = 0;
    }

    ~String()
    {
        Release();
    }

    String& operator=(const String& Other) noexcept
    {
        if (this != &Other)
        {
            String Copy(Other);
            Swap(Copy);
        }
        return *this;
    }

    String& operator=(String&& Other) noexcept
    {
        if (this != &Other)
        {
            String Moved(std::move(Other));
            Swap(Moved);
        }
        return *this;
    }

    String& operator=(const StringView View) noexcept
    {
        Assign(View);
        return *this;
    }

    String& operator=(const char* const NullTerminated) noexcept
    {
        Assign(StringView(NullTerminated));
        return *this;
    }

    const char* Data() const noexcept { return DataValue == nullptr ? EmptyData() : DataValue; }
    const char* GetData() const noexcept { return Data(); }
    const char* operator*() const noexcept { return Data(); }
    SizeType Size() const noexcept { return SizeValue; }
    SizeType Length() const noexcept { return SizeValue; }
    SizeType Capacity() const noexcept { return CapacityValue; }
    bool IsEmpty() const noexcept { return SizeValue == 0; }
    IAllocator& GetAllocator() const noexcept { return AllocatorValue.Get(); }
    StringView View() const noexcept { return StringView(SizeValue == 0 ? nullptr : DataValue, SizeValue); }
    operator StringView() const noexcept { return View(); }

    const char& operator[](const SizeType Index) const
    {
        if (Index >= SizeValue)
        {
            HandleContractViolation("String index out of bounds", __FILE__, __LINE__);
        }
        return DataValue[Index];
    }

    bool TryReserve(const SizeType RequestedCapacity) noexcept
    {
        if (RequestedCapacity <= CapacityValue)
        {
            return true;
        }
        if (RequestedCapacity == (std::numeric_limits<SizeType>::max)())
        {
            return false;
        }

        char* const NewData = TryAllocateArray<char>(AllocatorValue.Get(), RequestedCapacity + 1);
        if (NewData == nullptr)
        {
            return false;
        }
        if (SizeValue != 0)
        {
            std::memcpy(NewData, DataValue, SizeValue);
        }
        NewData[SizeValue] = '\0';
        DeallocateArray(AllocatorValue.Get(), DataValue, CapacityValue == 0 ? 0 : CapacityValue + 1);
        DataValue = NewData;
        CapacityValue = RequestedCapacity;
        return true;
    }

    void Reserve(const SizeType RequestedCapacity) noexcept
    {
        if (!TryReserve(RequestedCapacity))
        {
            const SizeType Bytes = RequestedCapacity == (std::numeric_limits<SizeType>::max)()
                ? RequestedCapacity
                : RequestedCapacity + 1;
            HandleOutOfMemory(Bytes, alignof(char));
        }
    }

    bool TryAssign(const StringView Source) noexcept
    {
        if (Source.Size() == 0)
        {
            Clear();
            return true;
        }
        if (Source.Size() == (std::numeric_limits<SizeType>::max)())
        {
            return false;
        }

        if (Source.Size() <= CapacityValue)
        {
            std::memmove(DataValue, Source.Data(), Source.Size());
            SizeValue = Source.Size();
            DataValue[SizeValue] = '\0';
            return true;
        }

        char* const NewData = TryAllocateArray<char>(AllocatorValue.Get(), Source.Size() + 1);
        if (NewData == nullptr)
        {
            return false;
        }
        std::memcpy(NewData, Source.Data(), Source.Size());
        NewData[Source.Size()] = '\0';
        Release();
        DataValue = NewData;
        SizeValue = Source.Size();
        CapacityValue = Source.Size();
        return true;
    }

    void Assign(const StringView Source) noexcept
    {
        if (!TryAssign(Source))
        {
            const SizeType Bytes = Source.Size() == (std::numeric_limits<SizeType>::max)()
                ? Source.Size()
                : Source.Size() + 1;
            HandleOutOfMemory(Bytes, alignof(char));
        }
    }

    bool TryAppend(const StringView Suffix) noexcept
    {
        if (Suffix.Size() == 0)
        {
            return true;
        }
        if (Suffix.Size() > (std::numeric_limits<SizeType>::max)() - SizeValue)
        {
            return false;
        }
        const SizeType NewSize = SizeValue + Suffix.Size();
        if (NewSize > CapacityValue)
        {
            SizeType NewCapacity = CapacityValue == 0 ? 16 : CapacityValue;
            while (NewCapacity < NewSize)
            {
                const SizeType Growth = NewCapacity / 2 + 1;
                if (NewCapacity > (std::numeric_limits<SizeType>::max)() - Growth)
                {
                    NewCapacity = NewSize;
                    break;
                }
                NewCapacity += Growth;
            }
            if (NewCapacity == (std::numeric_limits<SizeType>::max)())
            {
                return false;
            }

            char* const NewData = TryAllocateArray<char>(AllocatorValue.Get(), NewCapacity + 1);
            if (NewData == nullptr)
            {
                return false;
            }
            if (SizeValue != 0)
            {
                std::memcpy(NewData, DataValue, SizeValue);
            }
            std::memcpy(NewData + SizeValue, Suffix.Data(), Suffix.Size());
            NewData[NewSize] = '\0';
            DeallocateArray(AllocatorValue.Get(), DataValue, CapacityValue == 0 ? 0 : CapacityValue + 1);
            DataValue = NewData;
            SizeValue = NewSize;
            CapacityValue = NewCapacity;
            return true;
        }

        std::memmove(DataValue + SizeValue, Suffix.Data(), Suffix.Size());
        SizeValue = NewSize;
        DataValue[SizeValue] = '\0';
        return true;
    }

    void Append(const StringView Suffix) noexcept
    {
        if (!TryAppend(Suffix))
        {
            const SizeType Maximum = (std::numeric_limits<SizeType>::max)();
            const SizeType Requested = Suffix.Size() >= Maximum - SizeValue
                ? Maximum
                : SizeValue + Suffix.Size() + 1;
            HandleOutOfMemory(Requested, alignof(char));
        }
    }

    String& operator+=(const StringView Suffix) noexcept { Append(Suffix); return *this; }
    String& operator+=(const String& Suffix) noexcept { return *this += Suffix.View(); }
    String& operator+=(const char* const Suffix) noexcept { return *this += StringView(Suffix); }

    void Clear() noexcept
    {
        SizeValue = 0;
        if (DataValue != nullptr)
        {
            DataValue[0] = '\0';
        }
    }

    void Swap(String& Other) noexcept
    {
        using std::swap;
        swap(DataValue, Other.DataValue);
        swap(SizeValue, Other.SizeValue);
        swap(CapacityValue, Other.CapacityValue);
        swap(AllocatorValue, Other.AllocatorValue);
    }

private:
    static const char* EmptyData() noexcept
    {
        static const char Empty = '\0';
        return &Empty;
    }

    void Release() noexcept
    {
        DeallocateArray(AllocatorValue.Get(), DataValue, CapacityValue == 0 ? 0 : CapacityValue + 1);
        DataValue = nullptr;
        SizeValue = 0;
        CapacityValue = 0;
    }

    char* DataValue = nullptr;
    SizeType SizeValue = 0;
    SizeType CapacityValue = 0;
    AllocatorRef AllocatorValue;
};

inline bool operator==(const String& Left, const String& Right) noexcept { return Left.View() == Right.View(); }
inline bool operator!=(const String& Left, const String& Right) noexcept { return !(Left == Right); }
inline bool operator==(const String& Left, const StringView Right) noexcept { return Left.View() == Right; }
inline bool operator==(const StringView Left, const String& Right) noexcept { return Left == Right.View(); }
inline bool operator!=(const String& Left, const StringView Right) noexcept { return !(Left == Right); }
inline bool operator!=(const StringView Left, const String& Right) noexcept { return !(Left == Right); }
inline bool operator<(const String& Left, const String& Right) noexcept { return Left.View() < Right.View(); }
inline bool operator<(const String& Left, const StringView Right) noexcept { return Left.View() < Right; }
inline bool operator<(const StringView Left, const String& Right) noexcept { return Left < Right.View(); }

inline String operator+(const String& Left, const StringView Right) noexcept
{
    String Result(Left);
    Result.Append(Right);
    return Result;
}

inline String operator+(const String& Left, const String& Right) noexcept { return Left + Right.View(); }

struct StringHash
{
    using is_transparent = void;

    std::size_t operator()(const StringView Value) const noexcept
    {
        return HashStringBytes(Value);
    }

    std::size_t operator()(const String& Value) const noexcept { return (*this)(Value.View()); }
    std::size_t operator()(const char* const Value) const noexcept { return (*this)(StringView(Value)); }
};

struct StringEqual
{
    using is_transparent = void;
    bool operator()(const StringView Left, const StringView Right) const noexcept { return Left == Right; }
    bool operator()(const String& Left, const String& Right) const noexcept { return Left == Right; }
    bool operator()(const String& Left, const StringView Right) const noexcept { return Left == Right; }
    bool operator()(const StringView Left, const String& Right) const noexcept { return Left == Right; }
    bool operator()(const String& Left, const char* const Right) const noexcept { return Left == StringView(Right); }
    bool operator()(const char* const Left, const String& Right) const noexcept { return StringView(Left) == Right; }
};

template <>
struct DefaultHash<String> : StringHash
{
};

template <>
struct DefaultEqual<String> : StringEqual
{
};

CORE_API std::ostream& operator<<(std::ostream& Stream, const String& Value);

} // namespace LE
