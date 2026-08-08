#pragma once

#include "Containers/Span.h"
#include "Memory/Allocator.h"
#include "Memory/Relocation.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace LE
{

// Allocator is a small value policy with Get() -> IAllocator&. The default
// AllocatorRef retains the exact allocator identity used by the storage.
template <typename T, typename Allocator = AllocatorRef>
class Array
{
public:
    using ValueType = T;
    using SizeType = std::size_t;

    template <bool IsConst>
    class BasicIterator
    {
        friend class Array;
        template <bool>
        friend class BasicIterator;

        using OwnerType = typename std::conditional<IsConst, const Array, Array>::type;
        using PointerType = typename std::conditional<IsConst, const T*, T*>::type;

    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = PointerType;
        using reference = typename std::conditional<IsConst, const T&, T&>::type;

        BasicIterator() noexcept = default;

        template <bool OtherConst, typename std::enable_if<IsConst && !OtherConst, int>::type = 0>
        BasicIterator(const BasicIterator<OtherConst>& Other) noexcept
            : Owner(Other.Owner)
            , Index(Other.Index)
            , StorageGeneration(Other.StorageGeneration)
#if !defined(NDEBUG)
            , PositionToken(Other.PositionToken)
#endif
        {
        }

#if !defined(NDEBUG)
        bool IsValid() const noexcept
        {
            return Owner != nullptr && Owner->IsIteratorValid(*this, true);
        }
#endif

        reference operator*() const
        {
            ValidateDereference();
            return Owner->DataValue[Index];
        }

        pointer operator->() const { return &operator*(); }

        BasicIterator& operator++()
        {
            ValidatePosition(true);
            if (Index >= Owner->SizeValue)
            {
                HandleContractViolation("Array iterator incremented past end", __FILE__, __LINE__);
            }
            ++Index;
            BindCurrentPosition();
            return *this;
        }
        BasicIterator operator++(int) { BasicIterator Copy(*this); ++(*this); return Copy; }
        BasicIterator& operator--()
        {
            ValidatePosition(true);
            if (Index == 0)
            {
                HandleContractViolation("Array iterator decremented before begin", __FILE__, __LINE__);
            }
            --Index;
            BindCurrentPosition();
            return *this;
        }
        BasicIterator operator--(int) { BasicIterator Copy(*this); --(*this); return Copy; }

        template <bool OtherConst>
        bool operator==(const BasicIterator<OtherConst>& Other) const noexcept
        {
            return Owner == Other.Owner && Index == Other.Index
                && StorageGeneration == Other.StorageGeneration
#if !defined(NDEBUG)
                && PositionToken == Other.PositionToken;
#else
                ;
#endif
        }
        template <bool OtherConst>
        bool operator!=(const BasicIterator<OtherConst>& Other) const noexcept { return !(*this == Other); }

    private:
        BasicIterator(OwnerType* const InOwner, const SizeType InIndex) noexcept
            : Owner(InOwner)
            , Index(InIndex)
            , StorageGeneration(InOwner->StorageGeneration)
#if !defined(NDEBUG)
            , PositionToken(InIndex < InOwner->SizeValue
                ? InOwner->IteratorTokens[InIndex]
                : InOwner->EndToken)
#endif
        {
        }

        void ValidatePosition(const bool AllowEnd) const
        {
            if (Owner == nullptr || !Owner->IsIteratorValid(*this, AllowEnd))
            {
                HandleContractViolation("invalidated Array iterator", __FILE__, __LINE__);
            }
        }

        void ValidateDereference() const { ValidatePosition(false); }

        void BindCurrentPosition() noexcept
        {
#if !defined(NDEBUG)
            PositionToken = Index < Owner->SizeValue ? Owner->IteratorTokens[Index] : Owner->EndToken;
#endif
        }

        OwnerType* Owner = nullptr;
        SizeType Index = 0;
        std::uint64_t StorageGeneration = 0;
#if !defined(NDEBUG)
        std::uint64_t PositionToken = 0;
#endif
    };

    using Iterator = BasicIterator<false>;
    using ConstIterator = BasicIterator<true>;

    Array() noexcept = default;

    explicit Array(const Allocator& InAllocator) noexcept
        : AllocatorValue(InAllocator)
    {
    }

    explicit Array(IAllocator& InAllocator) noexcept
        : AllocatorValue(InAllocator)
    {
    }

    Array(std::initializer_list<T> Items, const Allocator& InAllocator = Allocator())
        : AllocatorValue(InAllocator)
    {
        Reserve(Items.size());
        for (const T& Item : Items)
        {
            new (DataValue + SizeValue) T(Item);
#if !defined(NDEBUG)
            IteratorTokens[SizeValue] = NextIteratorToken();
#endif
            ++SizeValue;
        }
#if !defined(NDEBUG)
        EndToken = NextIteratorToken();
#endif
    }

    Array(const Array& Other)
        : AllocatorValue(Other.AllocatorValue)
    {
        static_assert(std::is_copy_constructible<T>::value,
            "copying LE::Array requires a copy-constructible element type");
        Reserve(Other.SizeValue);
        for (; SizeValue < Other.SizeValue; ++SizeValue)
        {
            new (DataValue + SizeValue) T(Other.DataValue[SizeValue]);
#if !defined(NDEBUG)
            IteratorTokens[SizeValue] = NextIteratorToken();
#endif
        }
#if !defined(NDEBUG)
        EndToken = NextIteratorToken();
#endif
    }

    Array(Array&& Other) noexcept
        : DataValue(Other.DataValue)
#if !defined(NDEBUG)
        , IteratorTokens(Other.IteratorTokens)
#endif
        , SizeValue(Other.SizeValue)
        , CapacityValue(Other.CapacityValue)
        , AllocatorValue(std::move(Other.AllocatorValue))
#if !defined(NDEBUG)
        , NextTokenValue(Other.NextTokenValue)
        , EndToken(Other.EndToken)
#endif
    {
        Other.DataValue = nullptr;
#if !defined(NDEBUG)
        Other.IteratorTokens = nullptr;
#endif
        Other.SizeValue = 0;
        Other.CapacityValue = 0;
        Other.InvalidateStorage();
    }

    ~Array()
    {
        DestroyAndDeallocate();
    }

    Array& operator=(const Array& Other)
    {
        static_assert(std::is_copy_constructible<T>::value,
            "copying LE::Array requires a copy-constructible element type");
        if (this != &Other)
        {
            Array Copy(Other);
            *this = std::move(Copy);
        }
        return *this;
    }

    Array& operator=(Array&& Other) noexcept
    {
        if (this != &Other)
        {
            DestroyAndDeallocate();
            DataValue = Other.DataValue;
#if !defined(NDEBUG)
            IteratorTokens = Other.IteratorTokens;
#endif
            SizeValue = Other.SizeValue;
            CapacityValue = Other.CapacityValue;
            AllocatorValue = std::move(Other.AllocatorValue);
#if !defined(NDEBUG)
            NextTokenValue = Other.NextTokenValue;
            EndToken = Other.EndToken;
#endif
            InvalidateStorage();

            Other.DataValue = nullptr;
#if !defined(NDEBUG)
            Other.IteratorTokens = nullptr;
#endif
            Other.SizeValue = 0;
            Other.CapacityValue = 0;
            Other.InvalidateStorage();
        }
        return *this;
    }

    SizeType Size() const noexcept { return SizeValue; }
    SizeType Capacity() const noexcept { return CapacityValue; }
    bool IsEmpty() const noexcept { return SizeValue == 0; }
    T* Data() noexcept { return DataValue; }
    const T* Data() const noexcept { return DataValue; }
    IAllocator& GetAllocator() const noexcept { return AllocatorValue.Get(); }

    static constexpr SizeType MaxSize() noexcept
    {
        constexpr SizeType ElementMax = (std::numeric_limits<SizeType>::max)() / sizeof(T);
#if !defined(NDEBUG)
        constexpr SizeType DebugTokenMax = (std::numeric_limits<SizeType>::max)() / sizeof(std::uint64_t);
        return ElementMax < DebugTokenMax ? ElementMax : DebugTokenMax;
#else
        return ElementMax;
#endif
    }

    T& operator[](const SizeType Index)
    {
#if !defined(NDEBUG)
        CheckIndex(Index);
#endif
        return DataValue[Index];
    }

    const T& operator[](const SizeType Index) const
    {
#if !defined(NDEBUG)
        CheckIndex(Index);
#endif
        return DataValue[Index];
    }

    T* TryGet(const SizeType Index) noexcept { return Index < SizeValue ? DataValue + Index : nullptr; }
    const T* TryGet(const SizeType Index) const noexcept { return Index < SizeValue ? DataValue + Index : nullptr; }

    T& Front() { CheckNotEmpty(); return DataValue[0]; }
    const T& Front() const { CheckNotEmpty(); return DataValue[0]; }
    T& Back() { CheckNotEmpty(); return DataValue[SizeValue - 1]; }
    const T& Back() const { CheckNotEmpty(); return DataValue[SizeValue - 1]; }

    Iterator begin() noexcept { return Iterator(this, 0); }
    Iterator end() noexcept { return Iterator(this, SizeValue); }
    ConstIterator begin() const noexcept { return ConstIterator(this, 0); }
    ConstIterator end() const noexcept { return ConstIterator(this, SizeValue); }
    ConstIterator cbegin() const noexcept { return ConstIterator(this, 0); }
    ConstIterator cend() const noexcept { return ConstIterator(this, SizeValue); }

    Span<T> AsSpan() & noexcept { return Span<T>(DataValue, SizeValue); }
    Span<const T> AsSpan() const & noexcept { return Span<const T>(DataValue, SizeValue); }
    Span<T> AsSpan() && = delete;
    Span<const T> AsSpan() const && = delete;

    bool TryReserve(const SizeType RequestedCapacity)
    {
        if (RequestedCapacity <= CapacityValue)
        {
            return true;
        }
        if (RequestedCapacity > MaxSize())
        {
            return false;
        }

        T* const NewData = TryAllocateArray<T>(AllocatorValue.Get(), RequestedCapacity);
        if (NewData == nullptr)
        {
            return false;
        }

#if !defined(NDEBUG)
        std::uint64_t* const NewIteratorTokens =
            TryAllocateArray<std::uint64_t>(AllocatorValue.Get(), RequestedCapacity);
        if (NewIteratorTokens == nullptr)
        {
            DeallocateArray(AllocatorValue.Get(), NewData, RequestedCapacity);
            return false;
        }
#endif

        RelocateConstructRange(NewData, DataValue, SizeValue);
#if !defined(NDEBUG)
        for (SizeType Index = 0; Index < SizeValue; ++Index)
        {
            NewIteratorTokens[Index] = NextIteratorToken();
        }
#endif
        DeallocateArray(AllocatorValue.Get(), DataValue, CapacityValue);
#if !defined(NDEBUG)
        DeallocateArray(AllocatorValue.Get(), IteratorTokens, CapacityValue);
#endif
        DataValue = NewData;
#if !defined(NDEBUG)
        IteratorTokens = NewIteratorTokens;
#endif
        CapacityValue = RequestedCapacity;
        InvalidateStorage();
        return true;
    }

    void Reserve(const SizeType RequestedCapacity)
    {
        if (!TryReserve(RequestedCapacity))
        {
            FailAllocation(RequestedCapacity);
        }
    }

    bool TryShrink()
    {
        if (SizeValue == CapacityValue)
        {
            return true;
        }
        if (SizeValue == 0)
        {
            DeallocateArray(AllocatorValue.Get(), DataValue, CapacityValue);
#if !defined(NDEBUG)
            DeallocateArray(AllocatorValue.Get(), IteratorTokens, CapacityValue);
#endif
            DataValue = nullptr;
#if !defined(NDEBUG)
            IteratorTokens = nullptr;
#endif
            CapacityValue = 0;
            InvalidateStorage();
            return true;
        }

        T* const NewData = TryAllocateArray<T>(AllocatorValue.Get(), SizeValue);
        if (NewData == nullptr)
        {
            return false;
        }
#if !defined(NDEBUG)
        std::uint64_t* const NewIteratorTokens =
            TryAllocateArray<std::uint64_t>(AllocatorValue.Get(), SizeValue);
        if (NewIteratorTokens == nullptr)
        {
            DeallocateArray(AllocatorValue.Get(), NewData, SizeValue);
            return false;
        }
#endif
        RelocateConstructRange(NewData, DataValue, SizeValue);
#if !defined(NDEBUG)
        for (SizeType Index = 0; Index < SizeValue; ++Index)
        {
            NewIteratorTokens[Index] = NextIteratorToken();
        }
#endif
        DeallocateArray(AllocatorValue.Get(), DataValue, CapacityValue);
#if !defined(NDEBUG)
        DeallocateArray(AllocatorValue.Get(), IteratorTokens, CapacityValue);
#endif
        DataValue = NewData;
#if !defined(NDEBUG)
        IteratorTokens = NewIteratorTokens;
#endif
        CapacityValue = SizeValue;
        InvalidateStorage();
        return true;
    }

    void Shrink()
    {
        if (!TryShrink())
        {
            FailAllocation(SizeValue);
        }
    }

    template <typename... Args>
    bool TryEmplaceBack(Args&&... Arguments)
    {
        // Stage constructor arguments before storage can move. Direct T
        // lvalue/rvalue overloads below preserve self-aliasing more precisely.
        T Staged(std::forward<Args>(Arguments)...);
        bool Reallocated = false;
        if (!TryEnsureCapacityForOne(Reallocated))
        {
            return false;
        }
        new (DataValue + SizeValue) T(std::move(Staged));
#if !defined(NDEBUG)
        IteratorTokens[SizeValue] = NextIteratorToken();
#endif
        ++SizeValue;
#if !defined(NDEBUG)
        EndToken = NextIteratorToken();
#endif
        return true;
    }

    bool TryEmplaceBack(const T& Value) { return TryPushBack(Value); }
    bool TryEmplaceBack(T&& Value) { return TryPushBack(std::move(Value)); }

    template <typename... Args>
    T& EmplaceBack(Args&&... Arguments)
    {
        if (!TryEmplaceBack(std::forward<Args>(Arguments)...))
        {
            FailAllocationForOne();
        }
        return Back();
    }

    bool TryPushBack(const T& Value) { return TryInsert(SizeValue, Value); }
    bool TryPushBack(T&& Value) { return TryInsert(SizeValue, std::move(Value)); }
    void PushBack(const T& Value) { EmplaceBack(Value); }
    void PushBack(T&& Value) { EmplaceBack(std::move(Value)); }

    bool TryInsert(const SizeType Index, const T& Value)
    {
        CheckInsertIndex(Index);
        SizeType AliasIndex = 0;
        const bool AliasesThisArray = FindAlias(&Value, AliasIndex);
        bool Reallocated = false;
        if (!TryEnsureCapacityForOne(Reallocated))
        {
            return false;
        }
        const T& ReboundValue = AliasesThisArray ? DataValue[AliasIndex] : Value;
        T Staged(ReboundValue);
        InsertStaged(Index, std::move(Staged), Reallocated);
        return true;
    }

    bool TryInsert(const SizeType Index, T&& Value)
    {
        CheckInsertIndex(Index);
        SizeType AliasIndex = 0;
        const bool AliasesThisArray = FindAlias(&Value, AliasIndex);
        bool Reallocated = false;
        if (!TryEnsureCapacityForOne(Reallocated))
        {
            return false;
        }
        T& ReboundValue = AliasesThisArray ? DataValue[AliasIndex] : Value;
        T Staged(std::move(ReboundValue));
        InsertStaged(Index, std::move(Staged), Reallocated);
        return true;
    }

    void Insert(const SizeType Index, const T& Value)
    {
        if (!TryInsert(Index, Value)) { FailAllocationForOne(); }
    }
    void Insert(const SizeType Index, T&& Value)
    {
        if (!TryInsert(Index, std::move(Value))) { FailAllocationForOne(); }
    }

    void Erase(const SizeType Index, const SizeType Count = 1)
    {
        if (Index > SizeValue || Count > SizeValue - Index)
        {
            HandleContractViolation("Array erase range out of bounds", __FILE__, __LINE__);
        }
        if (Count == 0)
        {
            return;
        }

        DestroyRange(DataValue + Index, Count);
        if constexpr (IsBitwiseRelocatableV<T>)
        {
            std::size_t Bytes = 0;
            if (!TryMultiplySize(sizeof(T), SizeValue - Index - Count, Bytes))
            {
                HandleContractViolation("Array erase relocation size overflow", __FILE__, __LINE__);
            }
            std::memmove(DataValue + Index, DataValue + Index + Count, Bytes);
        }
        else
        {
            for (SizeType Source = Index + Count; Source < SizeValue; ++Source)
            {
                new (DataValue + Source - Count) T(std::move(DataValue[Source]));
                DataValue[Source].~T();
            }
        }
        SizeValue -= Count;
        RefreshTokensFrom(Index);
    }

    void PopBack()
    {
        CheckNotEmpty();
        DataValue[SizeValue - 1].~T();
        --SizeValue;
#if !defined(NDEBUG)
        EndToken = NextIteratorToken();
#endif
    }

    bool TryResize(const SizeType NewSize)
    {
        static_assert(std::is_nothrow_default_constructible<T>::value,
            "Array::Resize requires non-throwing default construction");
        if (NewSize < SizeValue)
        {
            Erase(NewSize, SizeValue - NewSize);
            return true;
        }
        if (NewSize == SizeValue)
        {
            return true;
        }
        if (NewSize > MaxSize())
        {
            return false;
        }
        if (NewSize > CapacityValue && !TryReserve(NextCapacity(NewSize)))
        {
            return false;
        }
        const SizeType OldSize = SizeValue;
        for (SizeType Index = OldSize; Index < NewSize; ++Index)
        {
            new (DataValue + Index) T();
#if !defined(NDEBUG)
            IteratorTokens[Index] = NextIteratorToken();
#endif
        }
        SizeValue = NewSize;
#if !defined(NDEBUG)
        EndToken = NextIteratorToken();
#endif
        return true;
    }

    bool TryResize(const SizeType NewSize, const T& Value)
    {
        static_assert(std::is_nothrow_copy_constructible<T>::value,
            "Array::Resize fill requires non-throwing copy construction");
        if (NewSize <= SizeValue)
        {
            return TryResize(NewSize);
        }
        if (NewSize > MaxSize())
        {
            return false;
        }
        // Stage before storage can move so an element from this Array is a
        // valid fill value even when growth reallocates.
        T Staged(Value);
        if (NewSize > CapacityValue && !TryReserve(NextCapacity(NewSize)))
        {
            return false;
        }
        const SizeType OldSize = SizeValue;
        for (SizeType Index = OldSize; Index < NewSize; ++Index)
        {
            new (DataValue + Index) T(Staged);
#if !defined(NDEBUG)
            IteratorTokens[Index] = NextIteratorToken();
#endif
        }
        SizeValue = NewSize;
#if !defined(NDEBUG)
        EndToken = NextIteratorToken();
#endif
        return true;
    }

    void Resize(const SizeType NewSize)
    {
        if (!TryResize(NewSize))
        {
            FailAllocation(NewSize);
        }
    }

    void Resize(const SizeType NewSize, const T& Value)
    {
        if (!TryResize(NewSize, Value))
        {
            FailAllocation(NewSize);
        }
    }

    void Clear() noexcept
    {
        DestroyRange(DataValue, SizeValue);
        SizeValue = 0;
#if !defined(NDEBUG)
        EndToken = NextIteratorToken();
#endif
    }

private:
    void InsertStaged(const SizeType Index, T&& Staged, const bool Reallocated)
    {
        if (Index == SizeValue)
        {
            new (DataValue + SizeValue) T(std::move(Staged));
        }
        else if constexpr (IsBitwiseRelocatableV<T>)
        {
            std::size_t Bytes = 0;
            if (!TryMultiplySize(sizeof(T), SizeValue - Index, Bytes))
            {
                HandleContractViolation("Array insert relocation size overflow", __FILE__, __LINE__);
            }
            std::memmove(DataValue + Index + 1, DataValue + Index, Bytes);
            new (DataValue + Index) T(std::move(Staged));
        }
        else
        {
            for (SizeType Source = SizeValue; Source > Index; --Source)
            {
                new (DataValue + Source) T(std::move(DataValue[Source - 1]));
                DataValue[Source - 1].~T();
            }
            new (DataValue + Index) T(std::move(Staged));
        }
        ++SizeValue;
        if (Reallocated)
        {
#if !defined(NDEBUG)
            // TryReserve initialized tokens for the old size. A middle insert
            // shifts those elements and creates one additional live slot, so
            // rebuild every position token after the final layout is known.
            RefreshTokensFrom(0);
#endif
        }
        else
        {
            RefreshTokensFrom(Index);
        }
#if !defined(NDEBUG)
        EndToken = NextIteratorToken();
#endif
    }

    template <bool IsConst>
    bool IsIteratorValid(const BasicIterator<IsConst>& Candidate, const bool AllowEnd) const noexcept
    {
        if (Candidate.Owner != this || Candidate.StorageGeneration != StorageGeneration)
        {
            return false;
        }
#if !defined(NDEBUG)
        if (Candidate.Index < SizeValue)
        {
            return IteratorTokens[Candidate.Index] == Candidate.PositionToken;
        }
        return AllowEnd && Candidate.Index == SizeValue && Candidate.PositionToken == EndToken;
#else
        return AllowEnd ? Candidate.Index <= SizeValue : Candidate.Index < SizeValue;
#endif
    }

    static SizeType NextCapacity(const SizeType Minimum)
    {
        if (Minimum > MaxSize())
        {
            return MaxSize();
        }
        if (Minimum <= 4)
        {
            return 4;
        }

        const SizeType Previous = Minimum - 1;
        const SizeType Growth = Previous / 2 + 1;
        if (Growth > MaxSize() - Previous)
        {
            return MaxSize();
        }
        const SizeType Candidate = Previous + Growth;
        return Candidate < Minimum ? Minimum : Candidate;
    }

    bool TryEnsureCapacityForOne(bool& Reallocated)
    {
        Reallocated = false;
        if (SizeValue >= MaxSize())
        {
            return false;
        }
        if (SizeValue < CapacityValue)
        {
            return true;
        }
        Reallocated = true;
        return TryReserve(NextCapacity(SizeValue + 1));
    }

    [[noreturn]] void FailAllocationForOne() const
    {
        FailAllocation(SizeValue >= MaxSize() ? MaxSize() : NextCapacity(SizeValue + 1));
    }

    [[noreturn]] static void FailAllocation(const SizeType Capacity)
    {
        std::size_t Bytes = 0;
        if (!TryMultiplySize(sizeof(T), Capacity, Bytes))
        {
            Bytes = (std::numeric_limits<SizeType>::max)();
        }
        HandleOutOfMemory(Bytes, alignof(T));
    }

    void CheckIndex(const SizeType Index) const
    {
        if (Index >= SizeValue)
        {
            HandleContractViolation("Array index out of bounds", __FILE__, __LINE__);
        }
    }

    void CheckNotEmpty() const
    {
        if (SizeValue == 0)
        {
            HandleContractViolation("Array is empty", __FILE__, __LINE__);
        }
    }

    void CheckInsertIndex(const SizeType Index) const
    {
        if (Index > SizeValue)
        {
            HandleContractViolation("Array insert index out of bounds", __FILE__, __LINE__);
        }
    }

    bool FindAlias(const T* const Address, SizeType& Index) const noexcept
    {
        for (SizeType Candidate = 0; Candidate < SizeValue; ++Candidate)
        {
            if (Address == DataValue + Candidate)
            {
                Index = Candidate;
                return true;
            }
        }
        return false;
    }

    std::uint64_t NextIteratorToken() noexcept
    {
#if !defined(NDEBUG)
        ++NextTokenValue;
        if (NextTokenValue == 0)
        {
            HandleContractViolation("Array iterator token overflow", __FILE__, __LINE__);
        }
        return NextTokenValue;
#else
        return 0;
#endif
    }

    void RefreshTokensFrom(const SizeType Index)
    {
#if !defined(NDEBUG)
        for (SizeType Candidate = Index; Candidate < SizeValue; ++Candidate)
        {
            IteratorTokens[Candidate] = NextIteratorToken();
        }
#else
        static_cast<void>(Index);
#endif
#if !defined(NDEBUG)
        EndToken = NextIteratorToken();
#endif
    }

    void DestroyAndDeallocate() noexcept
    {
        DestroyRange(DataValue, SizeValue);
        DeallocateArray(AllocatorValue.Get(), DataValue, CapacityValue);
#if !defined(NDEBUG)
        DeallocateArray(AllocatorValue.Get(), IteratorTokens, CapacityValue);
#endif
        DataValue = nullptr;
#if !defined(NDEBUG)
        IteratorTokens = nullptr;
#endif
        SizeValue = 0;
        CapacityValue = 0;
    }

    void InvalidateStorage() noexcept
    {
        ++StorageGeneration;
        if (StorageGeneration == 0)
        {
            HandleContractViolation("Array storage generation overflow", __FILE__, __LINE__);
        }
#if !defined(NDEBUG)
        EndToken = NextIteratorToken();
#endif
    }

    T* DataValue = nullptr;
#if !defined(NDEBUG)
    std::uint64_t* IteratorTokens = nullptr;
#endif
    SizeType SizeValue = 0;
    SizeType CapacityValue = 0;
    Allocator AllocatorValue{};
    std::uint64_t StorageGeneration = 1;
#if !defined(NDEBUG)
    std::uint64_t NextTokenValue = 1;
    std::uint64_t EndToken = 1;
#endif
};

} // namespace LE
