#pragma once

#include "Containers/HashMap.h"

namespace LE
{
namespace Detail
{
struct HashSetUnit
{
};
}

template <
    typename K,
    typename Hash = DefaultHash<K>,
    typename Equal = DefaultEqual<K>,
    typename Allocator = AllocatorRef>
class HashSet
{
    using MapType = HashMap<K, Detail::HashSetUnit, Hash, Equal, Allocator>;

public:
    using KeyType = K;
    using SizeType = std::size_t;

    template <bool IsConst>
    class BasicIterator
    {
        friend class HashSet;
        template <bool> friend class BasicIterator;
        using MapIterator = typename std::conditional<IsConst,
            typename MapType::ConstIterator, typename MapType::Iterator>::type;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = K;
        using difference_type = std::ptrdiff_t;
        using pointer = const K*;
        using reference = const K&;

        BasicIterator() noexcept = default;
        template <bool OtherConst, typename std::enable_if<IsConst && !OtherConst, int>::type = 0>
        BasicIterator(const BasicIterator<OtherConst>& Other) noexcept : IteratorValue(Other.IteratorValue) {}

#if !defined(NDEBUG)
        bool IsValid() const noexcept { return IteratorValue.IsValid(); }
#endif
        reference operator*() const { return IteratorValue->GetKey(); }
        pointer operator->() const { return &operator*(); }
        BasicIterator& operator++() { ++IteratorValue; return *this; }
        BasicIterator operator++(int) { BasicIterator Copy(*this); ++(*this); return Copy; }
        template <bool OtherConst>
        bool operator==(const BasicIterator<OtherConst>& Other) const noexcept
        {
            return IteratorValue == Other.IteratorValue;
        }
        template <bool OtherConst>
        bool operator!=(const BasicIterator<OtherConst>& Other) const noexcept { return !(*this == Other); }

    private:
        explicit BasicIterator(const MapIterator& Iterator) : IteratorValue(Iterator) {}
        MapIterator IteratorValue;
    };

    using Iterator = BasicIterator<false>;
    using ConstIterator = BasicIterator<true>;

    HashSet() noexcept(std::is_nothrow_default_constructible<MapType>::value) = default;
    explicit HashSet(IAllocator& Allocator)
        noexcept(std::is_nothrow_constructible<MapType, IAllocator&>::value)
        : Storage(Allocator)
    {
    }
    explicit HashSet(const Allocator& AllocatorPolicy)
        noexcept(std::is_nothrow_constructible<MapType, const Allocator&>::value)
        : Storage(AllocatorPolicy)
    {
    }
    HashSet(Hash InHash, Equal InEqual, const Allocator& AllocatorPolicy = Allocator{})
        noexcept(std::is_nothrow_constructible<
            MapType, Hash, Equal, const Allocator&>::value)
        : Storage(std::move(InHash), std::move(InEqual), AllocatorPolicy)
    {
    }

    SizeType Size() const noexcept { return Storage.Size(); }
    SizeType BucketCount() const noexcept { return Storage.BucketCount(); }
    SizeType Tombstones() const noexcept { return Storage.Tombstones(); }
    bool IsEmpty() const noexcept { return Storage.IsEmpty(); }
    float LoadFactor() const noexcept { return Storage.LoadFactor(); }
    static constexpr float MaxLoadFactor() noexcept { return MapType::MaxLoadFactor(); }
    IAllocator& GetAllocator() const noexcept { return Storage.GetAllocator(); }

    Iterator begin() noexcept { return Iterator(Storage.begin()); }
    Iterator end() noexcept { return Iterator(Storage.end()); }
    ConstIterator begin() const noexcept { return ConstIterator(Storage.begin()); }
    ConstIterator end() const noexcept { return ConstIterator(Storage.end()); }
    ConstIterator cbegin() const noexcept { return ConstIterator(Storage.cbegin()); }
    ConstIterator cend() const noexcept { return ConstIterator(Storage.cend()); }

    bool Contains(const K& Key) const { return Storage.Contains(Key); }
    template <typename Query>
    auto Contains(const Query& Key) const -> decltype(Storage.Contains(Key)) { return Storage.Contains(Key); }

    template <typename KeyArgument>
    EHashInsertResult TryInsert(KeyArgument&& Key)
    {
        return Storage.TryInsert(std::forward<KeyArgument>(Key), Detail::HashSetUnit{});
    }
    template <typename KeyArgument>
    bool Insert(KeyArgument&& Key)
    {
        return Storage.Insert(std::forward<KeyArgument>(Key), Detail::HashSetUnit{});
    }

    bool Erase(const K& Key) { return Storage.Erase(Key); }
    template <typename Query>
    auto Erase(const Query& Key) -> decltype(Storage.Erase(Key)) { return Storage.Erase(Key); }

    void Clear() noexcept { Storage.Clear(); }
    bool TryReserve(const SizeType Count) { return Storage.TryReserve(Count); }
    void Reserve(const SizeType Count) { Storage.Reserve(Count); }
    bool TryRehash(const SizeType Count) { return Storage.TryRehash(Count); }
    void Rehash(const SizeType Count) { Storage.Rehash(Count); }

private:
    MapType Storage;
};

} // namespace LE
