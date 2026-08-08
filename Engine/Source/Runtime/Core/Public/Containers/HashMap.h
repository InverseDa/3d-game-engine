#pragma once

#include "Memory/Allocator.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace LE
{

enum class EHashInsertResult : std::uint8_t
{
    Inserted,
    Updated,
    AlreadyExists,
    AllocationFailed,
};

namespace Detail
{
inline std::size_t MixHash(std::size_t Value) noexcept
{
#if SIZE_MAX > UINT32_MAX
    Value ^= Value >> 30;
    Value *= static_cast<std::size_t>(0xbf58476d1ce4e5b9ULL);
    Value ^= Value >> 27;
    Value *= static_cast<std::size_t>(0x94d049bb133111ebULL);
    Value ^= Value >> 31;
#else
    Value ^= Value >> 16;
    Value *= static_cast<std::size_t>(0x7feb352dU);
    Value ^= Value >> 15;
    Value *= static_cast<std::size_t>(0x846ca68bU);
    Value ^= Value >> 16;
#endif
    return Value;
}

template <typename...>
using VoidT = void;

template <typename Policy, typename = void>
struct IsTransparent : std::false_type
{
};

template <typename Policy>
struct IsTransparent<Policy, VoidT<typename Policy::is_transparent>> : std::true_type
{
};

template <typename Hash, typename Equal, typename Query, typename Key>
struct CanHeterogeneousLookup : std::integral_constant<bool,
    IsTransparent<Hash>::value && IsTransparent<Equal>::value
    && std::is_invocable_r<std::size_t, const Hash&, const Query&>::value
    && (std::is_invocable_r<bool, const Equal&, const Query&, const Key&>::value
        || std::is_invocable_r<bool, const Equal&, const Key&, const Query&>::value)>
{
};
} // namespace Detail

// Intentionally incomplete for unsupported business types. Callers must pass
// an explicit policy instead of hashing object representation or padding.
template <typename T, typename Enable = void>
struct DefaultHash;

template <typename T>
struct DefaultHash<T, typename std::enable_if<std::is_integral<T>::value>::type>
{
    std::size_t operator()(const T Value) const noexcept
    {
        return Detail::MixHash(static_cast<std::size_t>(Value));
    }
};

template <typename T>
struct DefaultHash<T, typename std::enable_if<std::is_enum<T>::value>::type>
{
    std::size_t operator()(const T Value) const noexcept
    {
        using Underlying = typename std::underlying_type<T>::type;
        return Detail::MixHash(static_cast<std::size_t>(static_cast<Underlying>(Value)));
    }
};

template <typename T>
struct DefaultHash<T*, void>
{
    std::size_t operator()(const T* const Value) const noexcept
    {
        return Detail::MixHash(reinterpret_cast<std::size_t>(Value));
    }
};

template <typename T>
struct DefaultEqual
{
    constexpr bool operator()(const T& Left, const T& Right) const
        noexcept(noexcept(Left == Right))
    {
        return Left == Right;
    }
};

template <
    typename K,
    typename V,
    typename Hash = DefaultHash<K>,
    typename Equal = DefaultEqual<K>,
    typename Allocator = AllocatorRef>
class HashMap
{
    static_assert(std::is_nothrow_move_constructible<Hash>::value
        && std::is_nothrow_move_assignable<Hash>::value
        && std::is_nothrow_swappable<Hash>::value,
        "HashMap Hash policy must support non-throwing move and swap");
    static_assert(std::is_nothrow_move_constructible<Equal>::value
        && std::is_nothrow_move_assignable<Equal>::value
        && std::is_nothrow_swappable<Equal>::value,
        "HashMap Equal policy must support non-throwing move and swap");
    static_assert(std::is_nothrow_move_constructible<Allocator>::value
        && std::is_nothrow_move_assignable<Allocator>::value
        && std::is_nothrow_swappable<Allocator>::value,
        "HashMap Allocator policy must support non-throwing move and swap");

public:
    using KeyType = K;
    using MappedType = V;
    using SizeType = std::size_t;

    class Entry
    {
        friend class HashMap;

    public:
        const K& GetKey() const noexcept { return KeyValue; }
        V& GetValue() noexcept { return MappedValue; }
        const V& GetValue() const noexcept { return MappedValue; }

    private:
        template <typename KeyArgument, typename ValueArgument>
        Entry(KeyArgument&& Key, ValueArgument&& Value)
            : KeyValue(std::forward<KeyArgument>(Key))
            , MappedValue(std::forward<ValueArgument>(Value))
        {
        }

        K KeyValue;
        V MappedValue;
    };

private:
    enum class EBucketState : std::uint8_t { Empty, Occupied, Tombstone };

    struct Bucket
    {
        Entry* GetEntry() noexcept { return std::launder(reinterpret_cast<Entry*>(&Storage)); }
        const Entry* GetEntry() const noexcept
        {
            return std::launder(reinterpret_cast<const Entry*>(&Storage));
        }

        EBucketState State = EBucketState::Empty;
        typename std::aligned_storage<sizeof(Entry), alignof(Entry)>::type Storage;
    };

public:
    template <bool IsConst>
    class BasicIterator
    {
        friend class HashMap;
        template <bool> friend class BasicIterator;

        using OwnerType = typename std::conditional<IsConst, const HashMap, HashMap>::type;
        using EntryType = typename std::conditional<IsConst, const Entry, Entry>::type;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Entry;
        using difference_type = std::ptrdiff_t;
        using pointer = EntryType*;
        using reference = EntryType&;

        BasicIterator() noexcept = default;

        template <bool OtherConst, typename std::enable_if<IsConst && !OtherConst, int>::type = 0>
        BasicIterator(const BasicIterator<OtherConst>& Other) noexcept
            : Owner(Other.Owner), Index(Other.Index)
#if !defined(NDEBUG)
            , Generation(Other.Generation)
#endif
        {
        }

#if !defined(NDEBUG)
        bool IsValid() const noexcept
        {
            return Owner != nullptr && Generation == Owner->StorageGeneration
                && Index <= Owner->BucketCountValue;
        }
#endif

        reference operator*() const
        {
            ValidateDereference();
            return *Owner->Buckets[Index].GetEntry();
        }
        pointer operator->() const { return &operator*(); }

        BasicIterator& operator++()
        {
            ValidatePosition();
            if (Index >= Owner->BucketCountValue)
            {
                HandleContractViolation("HashMap iterator incremented past end", __FILE__, __LINE__);
            }
            ++Index;
            SkipEmpty();
            return *this;
        }
        BasicIterator operator++(int) { BasicIterator Copy(*this); ++(*this); return Copy; }

        template <bool OtherConst>
        bool operator==(const BasicIterator<OtherConst>& Other) const noexcept
        {
            return Owner == Other.Owner && Index == Other.Index
#if !defined(NDEBUG)
                && Generation == Other.Generation
#endif
                ;
        }
        template <bool OtherConst>
        bool operator!=(const BasicIterator<OtherConst>& Other) const noexcept { return !(*this == Other); }

    private:
        BasicIterator(OwnerType* const InOwner, const SizeType InIndex) noexcept
            : Owner(InOwner), Index(InIndex)
#if !defined(NDEBUG)
            , Generation(InOwner->StorageGeneration)
#endif
        {
            SkipEmpty();
        }

        void SkipEmpty() noexcept
        {
            while (Owner != nullptr && Index < Owner->BucketCountValue
                && Owner->Buckets[Index].State != EBucketState::Occupied)
            {
                ++Index;
            }
        }

        void ValidatePosition() const
        {
            if (Owner == nullptr || Index > Owner->BucketCountValue
#if !defined(NDEBUG)
                || Generation != Owner->StorageGeneration
#endif
                )
            {
                HandleContractViolation("invalidated HashMap iterator", __FILE__, __LINE__);
            }
        }

        void ValidateDereference() const
        {
            ValidatePosition();
            if (Index >= Owner->BucketCountValue
                || Owner->Buckets[Index].State != EBucketState::Occupied)
            {
                HandleContractViolation("HashMap iterator is not dereferenceable", __FILE__, __LINE__);
            }
        }

        OwnerType* Owner = nullptr;
        SizeType Index = 0;
#if !defined(NDEBUG)
        std::uint64_t Generation = 0;
#endif
    };

    using Iterator = BasicIterator<false>;
    using ConstIterator = BasicIterator<true>;

    HashMap() noexcept(std::is_nothrow_default_constructible<Hash>::value
        && std::is_nothrow_default_constructible<Equal>::value
        && std::is_nothrow_default_constructible<Allocator>::value) = default;
    explicit HashMap(IAllocator& InAllocator)
        noexcept(std::is_nothrow_constructible<Allocator, IAllocator&>::value)
        : AllocatorValue(InAllocator)
    {
    }
    explicit HashMap(const Allocator& InAllocator)
        noexcept(std::is_nothrow_copy_constructible<Allocator>::value)
        : AllocatorValue(InAllocator)
    {
    }
    HashMap(Hash InHash, Equal InEqual, const Allocator& InAllocator = Allocator{})
        noexcept(std::is_nothrow_move_constructible<Hash>::value
            && std::is_nothrow_move_constructible<Equal>::value
            && std::is_nothrow_copy_constructible<Allocator>::value)
        : HashValue(std::move(InHash))
        , EqualValue(std::move(InEqual))
        , AllocatorValue(InAllocator)
    {
    }

    HashMap(const HashMap& Other)
        : HashValue(Other.HashValue), EqualValue(Other.EqualValue), AllocatorValue(Other.AllocatorValue)
    {
        CopyFrom(Other);
    }

    HashMap(HashMap&& Other) noexcept
        : Buckets(Other.Buckets), SizeValue(Other.SizeValue), BucketCountValue(Other.BucketCountValue)
        , TombstoneCount(Other.TombstoneCount), HashValue(std::move(Other.HashValue))
        , EqualValue(std::move(Other.EqualValue)), AllocatorValue(std::move(Other.AllocatorValue))
    {
        Other.Buckets = nullptr;
        Other.SizeValue = 0;
        Other.BucketCountValue = 0;
        Other.TombstoneCount = 0;
        Other.InvalidateIterators();
    }

    ~HashMap() { DestroyAndDeallocate(); }

    HashMap& operator=(const HashMap& Other)
    {
        if (this != &Other)
        {
            HashMap Copy(Other);
            Swap(Copy);
        }
        return *this;
    }

    HashMap& operator=(HashMap&& Other) noexcept
    {
        if (this != &Other)
        {
            DestroyAndDeallocate();
            Buckets = Other.Buckets;
            SizeValue = Other.SizeValue;
            BucketCountValue = Other.BucketCountValue;
            TombstoneCount = Other.TombstoneCount;
            HashValue = std::move(Other.HashValue);
            EqualValue = std::move(Other.EqualValue);
            AllocatorValue = std::move(Other.AllocatorValue);
            Other.Buckets = nullptr;
            Other.SizeValue = 0;
            Other.BucketCountValue = 0;
            Other.TombstoneCount = 0;
            InvalidateIterators();
            Other.InvalidateIterators();
        }
        return *this;
    }

    SizeType Size() const noexcept { return SizeValue; }
    SizeType BucketCount() const noexcept { return BucketCountValue; }
    SizeType Tombstones() const noexcept { return TombstoneCount; }
    bool IsEmpty() const noexcept { return SizeValue == 0; }
    float LoadFactor() const noexcept
    {
        return BucketCountValue == 0 ? 0.0f
            : static_cast<float>(SizeValue) / static_cast<float>(BucketCountValue);
    }
    static constexpr float MaxLoadFactor() noexcept { return 0.7f; }
    IAllocator& GetAllocator() const noexcept { return AllocatorValue.Get(); }

    Iterator begin() noexcept { return Iterator(this, 0); }
    Iterator end() noexcept { return Iterator(this, BucketCountValue); }
    ConstIterator begin() const noexcept { return ConstIterator(this, 0); }
    ConstIterator end() const noexcept { return ConstIterator(this, BucketCountValue); }
    ConstIterator cbegin() const noexcept { return ConstIterator(this, 0); }
    ConstIterator cend() const noexcept { return ConstIterator(this, BucketCountValue); }

    V* Find(const K& Key) { return FindImpl(Key); }
    const V* Find(const K& Key) const { return FindImpl(Key); }

    template <typename Query, typename std::enable_if<
        Detail::CanHeterogeneousLookup<Hash, Equal, Query, K>::value
        && !std::is_same<typename std::decay<Query>::type, K>::value, int>::type = 0>
    V* Find(const Query& Key) { return FindImpl(Key); }

    template <typename Query, typename std::enable_if<
        Detail::CanHeterogeneousLookup<Hash, Equal, Query, K>::value
        && !std::is_same<typename std::decay<Query>::type, K>::value, int>::type = 0>
    const V* Find(const Query& Key) const { return FindImpl(Key); }

    bool Contains(const K& Key) const { return Find(Key) != nullptr; }

    template <typename Query, typename std::enable_if<
        Detail::CanHeterogeneousLookup<Hash, Equal, Query, K>::value
        && !std::is_same<typename std::decay<Query>::type, K>::value, int>::type = 0>
    bool Contains(const Query& Key) const { return Find(Key) != nullptr; }

    template <typename KeyArgument, typename ValueArgument>
    EHashInsertResult TryInsert(KeyArgument&& Key, ValueArgument&& Value)
    {
        return TryInsertImpl<false>(std::forward<KeyArgument>(Key), std::forward<ValueArgument>(Value));
    }

    template <typename KeyArgument, typename ValueArgument>
    bool Insert(KeyArgument&& Key, ValueArgument&& Value)
    {
        const EHashInsertResult Result =
            TryInsert(std::forward<KeyArgument>(Key), std::forward<ValueArgument>(Value));
        if (Result == EHashInsertResult::AllocationFailed) { FailAllocation(NextInsertionBucketCount()); }
        return Result == EHashInsertResult::Inserted;
    }

    template <typename KeyArgument, typename ValueArgument>
    EHashInsertResult TryInsertOrAssign(KeyArgument&& Key, ValueArgument&& Value)
    {
        return TryInsertImpl<true>(std::forward<KeyArgument>(Key), std::forward<ValueArgument>(Value));
    }

    template <typename KeyArgument, typename ValueArgument>
    bool InsertOrAssign(KeyArgument&& Key, ValueArgument&& Value)
    {
        const EHashInsertResult Result =
            TryInsertOrAssign(std::forward<KeyArgument>(Key), std::forward<ValueArgument>(Value));
        if (Result == EHashInsertResult::AllocationFailed) { FailAllocation(NextInsertionBucketCount()); }
        return Result == EHashInsertResult::Inserted;
    }

    bool Erase(const K& Key) { return EraseImpl(Key); }

    template <typename Query, typename std::enable_if<
        Detail::CanHeterogeneousLookup<Hash, Equal, Query, K>::value
        && !std::is_same<typename std::decay<Query>::type, K>::value, int>::type = 0>
    bool Erase(const Query& Key) { return EraseImpl(Key); }

    void Clear() noexcept
    {
        if (SizeValue == 0 && TombstoneCount == 0) { return; }
        for (SizeType Index = 0; Index < BucketCountValue; ++Index)
        {
            if (Buckets[Index].State == EBucketState::Occupied) { Buckets[Index].GetEntry()->~Entry(); }
            Buckets[Index].State = EBucketState::Empty;
        }
        SizeValue = 0;
        TombstoneCount = 0;
        InvalidateIterators();
    }

    bool TryReserve(const SizeType ElementCount)
    {
        SizeType RequiredBuckets = 0;
        if (!TryBucketsForElements(ElementCount, RequiredBuckets)) { return false; }
        return RequiredBuckets <= BucketCountValue || TryRehash(RequiredBuckets);
    }

    void Reserve(const SizeType ElementCount)
    {
        if (!TryReserve(ElementCount)) { FailAllocationForElements(ElementCount); }
    }

    bool TryRehash(const SizeType RequestedBucketCount)
    {
        SizeType MinimumBuckets = 0;
        if (!TryBucketsForElements(SizeValue, MinimumBuckets)) { return false; }
        SizeType Rounded = 0;
        if (!TryRoundBucketCount(RequestedBucketCount > MinimumBuckets
                ? RequestedBucketCount : MinimumBuckets, Rounded))
        {
            return false;
        }
        if (Rounded == 0)
        {
            DestroyAndDeallocate();
            InvalidateIterators();
            return true;
        }
        if (Rounded == BucketCountValue && TombstoneCount == 0) { return true; }
        return TryRebuild(Rounded);
    }

    void Rehash(const SizeType RequestedBucketCount)
    {
        if (!TryRehash(RequestedBucketCount)) { FailAllocation(RequestedBucketCount); }
    }

private:
    struct ProbeResult
    {
        SizeType Index = 0;
        bool bFound = false;
        bool bHasInsertionSlot = false;
    };

    static constexpr SizeType MinimumBucketCount = 8;

    static constexpr SizeType MaximumBucketCount() noexcept
    {
        return (std::numeric_limits<SizeType>::max)() / sizeof(Bucket);
    }

    static SizeType MaxElementsForBuckets(const SizeType Count) noexcept
    {
        // floor(Count * 7 / 10), written without an overflowing multiply.
        return (Count / 10) * 7 + ((Count % 10) * 7) / 10;
    }

    static bool TryRoundBucketCount(const SizeType Requested, SizeType& Result) noexcept
    {
        if (Requested == 0) { Result = 0; return true; }
        if (Requested > MaximumBucketCount()) { Result = 0; return false; }
        SizeType Candidate = MinimumBucketCount;
        while (Candidate < Requested)
        {
            if (Candidate > MaximumBucketCount() / 2) { Result = 0; return false; }
            Candidate *= 2;
        }
        Result = Candidate;
        return true;
    }

    static bool TryBucketsForElements(const SizeType Elements, SizeType& Result) noexcept
    {
        if (Elements == 0) { Result = 0; return true; }
        if (Elements > MaxElementsForBuckets(MaximumBucketCount())) { Result = 0; return false; }
        SizeType Candidate = MinimumBucketCount;
        while (MaxElementsForBuckets(Candidate) < Elements)
        {
            if (Candidate > MaximumBucketCount() / 2) { Result = 0; return false; }
            Candidate *= 2;
        }
        Result = Candidate;
        return true;
    }

    template <typename Query>
    bool KeysEqual(const Query& QueryKey, const K& StoredKey) const
    {
        if constexpr (std::is_invocable_r<bool, const Equal&, const Query&, const K&>::value)
        {
            return EqualValue(QueryKey, StoredKey);
        }
        else
        {
            return EqualValue(StoredKey, QueryKey);
        }
    }

    template <typename Query>
    ProbeResult Probe(const Query& Key) const
    {
        ProbeResult Result;
        if (BucketCountValue == 0) { return Result; }
        const SizeType Mask = BucketCountValue - 1;
        SizeType Index = HashValue(Key) & Mask;
        SizeType FirstTombstone = BucketCountValue;
        for (SizeType Attempt = 0; Attempt < BucketCountValue; ++Attempt)
        {
            const Bucket& Candidate = Buckets[Index];
            if (Candidate.State == EBucketState::Empty)
            {
                Result.Index = FirstTombstone != BucketCountValue ? FirstTombstone : Index;
                Result.bHasInsertionSlot = true;
                return Result;
            }
            if (Candidate.State == EBucketState::Tombstone)
            {
                if (FirstTombstone == BucketCountValue) { FirstTombstone = Index; }
            }
            else if (KeysEqual(Key, Candidate.GetEntry()->KeyValue))
            {
                Result.Index = Index;
                Result.bFound = true;
                return Result;
            }
            Index = (Index + 1) & Mask;
        }
        if (FirstTombstone != BucketCountValue)
        {
            Result.Index = FirstTombstone;
            Result.bHasInsertionSlot = true;
        }
        return Result;
    }

    template <typename Query>
    V* FindImpl(const Query& Key)
    {
        const ProbeResult Result = Probe(Key);
        return Result.bFound ? &Buckets[Result.Index].GetEntry()->MappedValue : nullptr;
    }

    template <typename Query>
    const V* FindImpl(const Query& Key) const
    {
        const ProbeResult Result = Probe(Key);
        return Result.bFound ? &Buckets[Result.Index].GetEntry()->MappedValue : nullptr;
    }

    template <bool AssignExisting, typename KeyArgument, typename ValueArgument>
    EHashInsertResult TryInsertImpl(KeyArgument&& Key, ValueArgument&& Value)
    {
        static_assert(std::is_same<typename std::decay<KeyArgument>::type, K>::value,
            "HashMap insertion key must be KeyType");
        ProbeResult Result = Probe(Key);
        if (Result.bFound)
        {
            if constexpr (AssignExisting)
            {
                Buckets[Result.Index].GetEntry()->MappedValue = std::forward<ValueArgument>(Value);
                return EHashInsertResult::Updated;
            }
            return EHashInsertResult::AlreadyExists;
        }

        SizeType RebuildBucketCount = 0;
        bool bNeedsRebuild = false;
        if (BucketCountValue == 0 || SizeValue >= MaxElementsForBuckets(BucketCountValue))
        {
            bNeedsRebuild = true;
            RebuildBucketCount = NextInsertionBucketCount();
        }
        else if (!Result.bHasInsertionSlot)
        {
            bNeedsRebuild = true;
            RebuildBucketCount = BucketCountValue > MaximumBucketCount() / 2
                ? 0 : BucketCountValue * 2;
        }

        // Allocation happens before staging. Consequently an allocator failure
        // cannot move-from a mapped value passed as an rvalue argument from this
        // map. Duplicate detection also happens first and does not consume
        // either rvalue.
        Bucket* PreparedBuckets = nullptr;
        if (bNeedsRebuild && RebuildBucketCount == 0)
        {
            return EHashInsertResult::AllocationFailed;
        }
        if (RebuildBucketCount != 0)
        {
            PreparedBuckets = TryCreateBuckets(RebuildBucketCount);
            if (PreparedBuckets == nullptr) { return EHashInsertResult::AllocationFailed; }
        }
        else if (BucketCountValue == 0 || !Result.bHasInsertionSlot)
        {
            return EHashInsertResult::AllocationFailed;
        }

        K StagedKey(std::forward<KeyArgument>(Key));
        V StagedValue(std::forward<ValueArgument>(Value));
        if (PreparedBuckets != nullptr)
        {
            RebuildInto(PreparedBuckets, RebuildBucketCount);
            Result = Probe(StagedKey);
        }
        if (!Result.bHasInsertionSlot)
        {
            HandleContractViolation("HashMap probe failed to terminate with an insertion slot", __FILE__, __LINE__);
        }

        Bucket& Destination = Buckets[Result.Index];
        const bool bReusedTombstone = Destination.State == EBucketState::Tombstone;
        new (&Destination.Storage) Entry(
            std::move(StagedKey), std::move(StagedValue));
        Destination.State = EBucketState::Occupied;
        ++SizeValue;
        if (bReusedTombstone) { --TombstoneCount; }
        InvalidateIterators();
        return EHashInsertResult::Inserted;
    }

    template <typename Query>
    bool EraseImpl(const Query& Key)
    {
        const ProbeResult Result = Probe(Key);
        if (!Result.bFound) { return false; }
        Bucket& Removed = Buckets[Result.Index];
        Removed.GetEntry()->~Entry();
        Removed.State = EBucketState::Tombstone;
        --SizeValue;
        ++TombstoneCount;
        InvalidateIterators();
        return true;
    }

    SizeType NextInsertionBucketCount() const noexcept
    {
        if (BucketCountValue == 0) { return MinimumBucketCount; }
        if (SizeValue < MaxElementsForBuckets(BucketCountValue)) { return BucketCountValue; }
        return BucketCountValue > MaximumBucketCount() / 2 ? 0 : BucketCountValue * 2;
    }

    bool TryRebuild(const SizeType NewBucketCount)
    {
        Bucket* const NewBuckets = TryCreateBuckets(NewBucketCount);
        if (NewBuckets == nullptr) { return false; }
        RebuildInto(NewBuckets, NewBucketCount);
        return true;
    }

    Bucket* TryCreateBuckets(const SizeType NewBucketCount)
    {
        Bucket* const NewBuckets = TryAllocateArray<Bucket>(AllocatorValue.Get(), NewBucketCount);
        if (NewBuckets == nullptr) { return nullptr; }
        for (SizeType Index = 0; Index < NewBucketCount; ++Index) { new (NewBuckets + Index) Bucket(); }
        return NewBuckets;
    }

    void RebuildInto(Bucket* const NewBuckets, const SizeType NewBucketCount)
    {
        const SizeType NewMask = NewBucketCount - 1;
        for (SizeType OldIndex = 0; OldIndex < BucketCountValue; ++OldIndex)
        {
            Bucket& OldBucket = Buckets[OldIndex];
            if (OldBucket.State != EBucketState::Occupied) { continue; }
            Entry* const OldEntry = OldBucket.GetEntry();
            SizeType NewIndex = HashValue(OldEntry->KeyValue) & NewMask;
            SizeType Attempt = 0;
            for (; Attempt < NewBucketCount; ++Attempt)
            {
                if (NewBuckets[NewIndex].State == EBucketState::Empty) { break; }
                NewIndex = (NewIndex + 1) & NewMask;
            }
            if (Attempt == NewBucketCount)
            {
                HandleContractViolation("HashMap rehash probe exhausted", __FILE__, __LINE__);
            }
            new (&NewBuckets[NewIndex].Storage) Entry(
                std::move(OldEntry->KeyValue), std::move(OldEntry->MappedValue));
            NewBuckets[NewIndex].State = EBucketState::Occupied;
            OldEntry->~Entry();
        }
        DeallocateArray(AllocatorValue.Get(), Buckets, BucketCountValue);
        Buckets = NewBuckets;
        BucketCountValue = NewBucketCount;
        TombstoneCount = 0;
        InvalidateIterators();
    }

    void CopyFrom(const HashMap& Other)
    {
        if (!TryReserve(Other.SizeValue)) { FailAllocationForElements(Other.SizeValue); }
        for (const Entry& Item : Other)
        {
            const EHashInsertResult Result = TryInsert(Item.KeyValue, Item.MappedValue);
            if (Result == EHashInsertResult::AllocationFailed) { FailAllocationForElements(Other.SizeValue); }
        }
    }

    void DestroyAndDeallocate() noexcept
    {
        for (SizeType Index = 0; Index < BucketCountValue; ++Index)
        {
            if (Buckets[Index].State == EBucketState::Occupied) { Buckets[Index].GetEntry()->~Entry(); }
        }
        DeallocateArray(AllocatorValue.Get(), Buckets, BucketCountValue);
        Buckets = nullptr;
        SizeValue = 0;
        BucketCountValue = 0;
        TombstoneCount = 0;
    }

    void Swap(HashMap& Other) noexcept
    {
        using std::swap;
        swap(Buckets, Other.Buckets);
        swap(SizeValue, Other.SizeValue);
        swap(BucketCountValue, Other.BucketCountValue);
        swap(TombstoneCount, Other.TombstoneCount);
        swap(HashValue, Other.HashValue);
        swap(EqualValue, Other.EqualValue);
        swap(AllocatorValue, Other.AllocatorValue);
        InvalidateIterators();
        Other.InvalidateIterators();
    }

    [[noreturn]] static void FailAllocation(const SizeType BucketCount)
    {
        std::size_t Bytes = 0;
        if (!TryMultiplySize(sizeof(Bucket), BucketCount, Bytes))
        {
            Bytes = (std::numeric_limits<SizeType>::max)();
        }
        HandleOutOfMemory(Bytes, alignof(Bucket));
    }

    [[noreturn]] static void FailAllocationForElements(const SizeType Elements)
    {
        SizeType BucketsNeeded = 0;
        if (!TryBucketsForElements(Elements, BucketsNeeded))
        {
            HandleOutOfMemory((std::numeric_limits<SizeType>::max)(), alignof(Bucket));
        }
        FailAllocation(BucketsNeeded);
    }

    void InvalidateIterators() noexcept
    {
#if !defined(NDEBUG)
        ++StorageGeneration;
        if (StorageGeneration == 0)
        {
            HandleContractViolation("HashMap iterator generation overflow", __FILE__, __LINE__);
        }
#endif
    }

    Bucket* Buckets = nullptr;
    SizeType SizeValue = 0;
    SizeType BucketCountValue = 0;
    SizeType TombstoneCount = 0;
    Hash HashValue{};
    Equal EqualValue{};
    Allocator AllocatorValue{};
#if !defined(NDEBUG)
    std::uint64_t StorageGeneration = 1;
#endif
};

} // namespace LE
