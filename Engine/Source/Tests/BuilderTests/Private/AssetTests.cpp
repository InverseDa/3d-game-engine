#include "Asset/AssetMinimal.h"
#include "Containers/Array.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace LE
{
struct FAssetTestTextAsset
{
    int Value = 0;
};

struct FAssetTestBinaryAsset
{
    int Value = 0;
};

struct FAssetTestInvalidTypeAsset
{
};

template <> struct TAssetTypeTraits<FAssetTestTextAsset>
{
    static FAssetTypeId GetTypeId() noexcept
    {
        FAssetTypeId Result;
        static_cast<void>(FAssetTypeId::TryParse("a0000000-0000-4000-8000-000000000001", Result));
        return Result;
    }
};

template <> struct TAssetTypeTraits<FAssetTestBinaryAsset>
{
    static FAssetTypeId GetTypeId() noexcept
    {
        FAssetTypeId Result;
        static_cast<void>(FAssetTypeId::TryParse("a0000000-0000-4000-8000-000000000002", Result));
        return Result;
    }
};

template <> struct TAssetTypeTraits<FAssetTestInvalidTypeAsset>
{
    static FAssetTypeId GetTypeId() noexcept { return {}; }
};
} // namespace LE

namespace
{

int ExpectAsset(const bool bCondition, const char* const Name)
{
    if (bCondition)
    {
        return 0;
    }
    std::fprintf(stderr, "FAILED: %s\n", Name);
    return 1;
}

template <typename Left, typename Right, typename = void> struct THasEquality : std::false_type
{
};

template <typename Left, typename Right> struct THasEquality<Left, Right, std::void_t<decltype(std::declval<const Left&>() == std::declval<const Right&>())>> : std::true_type
{
};

class FAssetFailingAllocator final : public LE::IAllocator
{
public:
    void* Allocate(const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        ++AllocationAttempts;
        if (bFailAllocations || (FailureAttempt != 0 && AllocationAttempts == FailureAttempt))
        {
            return nullptr;
        }
        void* const Result = LE::GetDefaultAllocator().Allocate(Size, Alignment);
        if (Result != nullptr)
        {
            ++LiveAllocations;
        }
        return Result;
    }

    void Deallocate(void* const Address, const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        if (Address != nullptr)
        {
            --LiveAllocations;
        }
        LE::GetDefaultAllocator().Deallocate(Address, Size, Alignment);
    }

    std::size_t AllocationAttempts = 0;
    std::size_t FailureAttempt = 0;
    std::size_t LiveAllocations = 0;
    bool bFailAllocations = false;
};

template <typename StableId> StableId ParseAssetStableId(const char* const Text)
{
    StableId Result;
    if (!StableId::TryParse(Text, Result))
    {
        std::abort();
    }
    return Result;
}

struct FAssetDestroyContext
{
    LE::FAssetRegistry* Registry = nullptr;
    LE::TAssetHandle<LE::FAssetTestTextAsset> ReentryHandle;
    LE::Array<int>* Events = nullptr;
    const bool* ProviderLoaded = nullptr;
    LE::EAssetUnregisterResult ReentryResult = LE::EAssetUnregisterResult::Success;
    int Marker = 0;
    int DestroyCount = 0;
    bool bDestroyedWhileProviderLoaded = true;
};

void DestroyTestAsset(void* const Context, void* const Object) noexcept
{
    FAssetDestroyContext* const TypedContext = static_cast<FAssetDestroyContext*>(Context);
    if (TypedContext == nullptr || Object == nullptr)
    {
        return;
    }
    ++TypedContext->DestroyCount;
    if (TypedContext->ProviderLoaded != nullptr && !*TypedContext->ProviderLoaded)
    {
        TypedContext->bDestroyedWhileProviderLoaded = false;
    }
    if (TypedContext->Events != nullptr)
    {
        TypedContext->Events->PushBack(TypedContext->Marker);
    }
    if (TypedContext->Registry != nullptr)
    {
        TypedContext->ReentryResult = TypedContext->Registry->Unregister(TypedContext->ReentryHandle);
    }
}

LE::TAssetRegistration<LE::FAssetTestTextAsset> MakeTextRegistration(const LE::FAssetId Id, const LE::StringView Path, LE::FAssetTestTextAsset& Object, const LE::EAssetOwnership Ownership = LE::EAssetOwnership::Borrowed, FAssetDestroyContext* const DestroyContext = nullptr) noexcept
{
    LE::TAssetRegistration<LE::FAssetTestTextAsset> Registration;
    Registration.Id = Id;
    Registration.SoftPath = Path;
    Registration.DisplayName = "Test asset";
    Registration.ProviderName = "BuilderTests.AssetProvider";
    Registration.Object = &Object;
    Registration.Ownership = Ownership;
    Registration.Destroy = Ownership == LE::EAssetOwnership::OwnedByProviderThunk ? &DestroyTestAsset : nullptr;
    Registration.ProviderContext = DestroyContext;
    return Registration;
}

} // namespace

namespace LE
{

int RunAssetTests()
{
    int Failures = 0;

    using FTextHandle = TAssetHandle<FAssetTestTextAsset>;
    using FBinaryHandle = TAssetHandle<FAssetTestBinaryAsset>;
    static_assert(sizeof(FTextHandle) == 16, "typed asset handles contain only slot generation and registry domain");
    static_assert(std::is_trivially_copyable<FTextHandle>::value, "typed asset handles are trivially copyable runtime values");
    static_assert(!std::is_same<FAssetId, FAssetTypeId>::value, "persistent asset identity and logical type identity use different domains");
    static_assert(!THasEquality<FTextHandle, FBinaryHandle>::value, "handles of different asset types are not comparable");

    const FAssetTypeId MeshTypeId = GetMeshAssetTypeId();
    const FAssetTypeId MaterialTypeId = GetMaterialAssetTypeId();
    Failures += ExpectAsset(MeshTypeId.IsValid() && MaterialTypeId.IsValid() && MeshTypeId != MaterialTypeId && MeshTypeId == GetAssetTypeId<FMeshAsset>() && MaterialTypeId == GetAssetTypeId<FMaterialAsset>(), "built-in render asset type IDs are valid, distinct, and match their traits");

    FAssetId AssetId;
    FAssetTypeId TypeIdWithSameUuid;
    Failures += ExpectAsset(FAssetId::TryParse("a1000000-0000-4000-8000-000000000001", AssetId) && FAssetTypeId::TryParse("a1000000-0000-4000-8000-000000000001", TypeIdWithSameUuid) && AssetId.IsValid() && TypeIdWithSameUuid.IsValid() && GetAssetTypeId<FAssetTestTextAsset>().IsValid() && GetAssetTypeId<FAssetTestTextAsset>() != GetAssetTypeId<FAssetTestBinaryAsset>(), "asset and type UUID domains remain stable and compile-time separated");

    FTextHandle EmptyHandle;
    const FTextHandle EmptyHandleCopy = EmptyHandle;
    Failures += ExpectAsset(!EmptyHandle.IsValid() && EmptyHandle == EmptyHandleCopy && DefaultHash<FTextHandle>{}(EmptyHandle) == DefaultHash<FTextHandle>{}(EmptyHandleCopy), "default typed handle is an invalid copyable hash value");
    EmptyHandle.Reset();
    Failures += ExpectAsset(!EmptyHandle.IsValid(), "typed handle reset preserves invalid runtime sentinel");

    FSoftAssetPath Path;
    Failures += ExpectAsset(Path.TryAssign("Game/Meshes/Cube") == ESoftAssetPathResult::Success && Path.View() == StringView("Game/Meshes/Cube"), "extensionless virtual asset path is valid");
    Failures += ExpectAsset(Path.TryAssign("Game\\Meshes\\\\Primitives/./Old/../Cube.mesh") == ESoftAssetPathResult::Success && Path.View() == StringView("Game/Meshes/Primitives/Cube.mesh"), "soft path canonicalizes separators and dot segments");

    FSoftAssetPath SamePath;
    FSoftAssetPath DifferentCase;
    Failures += ExpectAsset(SamePath.TryAssign("Game/Meshes/Primitives/Cube.mesh") == ESoftAssetPathResult::Success && DifferentCase.TryAssign("game/Meshes/Primitives/Cube.mesh") == ESoftAssetPathResult::Success && Path == SamePath && Path != DifferentCase && DefaultHash<FSoftAssetPath>{}(Path) == DefaultHash<FSoftAssetPath>{}(SamePath), "canonical paths compare and hash by case-sensitive UTF-8 bytes");

    const char Utf8Path[] = "Game/\xe6\x9d\x90\xe8\xb4\xa8/\xe6\x9c\xa8";
    Failures += ExpectAsset(Path.TryAssign(StringView(Utf8Path, sizeof(Utf8Path) - 1)) == ESoftAssetPathResult::Success && Path.View() == StringView(Utf8Path, sizeof(Utf8Path) - 1), "valid non-ASCII UTF-8 bytes are preserved without normalization");

    const auto ExpectRejected = [&Path, &Failures](const StringView Input, const ESoftAssetPathResult Expected, const char* const Name) {
        const String Before(Path.View());
        Failures += ExpectAsset(Path.TryAssign(Input) == Expected && Path.View() == Before.View(), Name);
    };

    ExpectRejected("", ESoftAssetPathResult::Empty, "empty soft path is rejected transactionally");
    ExpectRejected("/Game/Cube", ESoftAssetPathResult::AbsolutePath, "leading slash absolute path is rejected");
    ExpectRejected("\\\\server\\share\\Cube", ESoftAssetPathResult::AbsolutePath, "UNC path is rejected");
    ExpectRejected("C:/Game/Cube", ESoftAssetPathResult::InvalidCharacter, "drive-qualified path is rejected");
    ExpectRejected("Game", ESoftAssetPathResult::InvalidStructure, "soft path requires a mount and asset segment");
    ExpectRejected("Game/", ESoftAssetPathResult::InvalidStructure, "directory-ending soft path is rejected");
    ExpectRejected("Game/../Cube", ESoftAssetPathResult::RootEscape, "dot-dot cannot escape the virtual mount root");
    ExpectRejected("Game/A/../../Cube", ESoftAssetPathResult::RootEscape, "nested dot-dot cannot escape the virtual mount root");
    ExpectRejected("Game/My Asset", ESoftAssetPathResult::InvalidCharacter, "ASCII whitespace is not a portable identity byte");
    ExpectRejected("Game/A?B", ESoftAssetPathResult::InvalidCharacter, "reserved punctuation is rejected");

    const char EmbeddedNull[] = {'G', 'a', 'm', 'e', '/', 'A', '\0', 'B'};
    ExpectRejected(StringView(EmbeddedNull, sizeof(EmbeddedNull)), ESoftAssetPathResult::InvalidCharacter, "embedded NUL is rejected");
    const char OverlongUtf8[] = {'G', 'a', 'm', 'e', '/', char(0xc0), char(0x80)};
    ExpectRejected(StringView(OverlongUtf8, sizeof(OverlongUtf8)), ESoftAssetPathResult::InvalidUtf8, "overlong UTF-8 is rejected");
    const char SurrogateUtf8[] = {'G', 'a', 'm', 'e', '/', char(0xed), char(0xa0), char(0x80)};
    ExpectRejected(StringView(SurrogateUtf8, sizeof(SurrogateUtf8)), ESoftAssetPathResult::InvalidUtf8, "UTF-8 surrogate encoding is rejected");
    const char BeyondUnicode[] = {'G', 'a', 'm', 'e', '/', char(0xf4), char(0x90), char(0x80), char(0x80)};
    ExpectRejected(StringView(BeyondUnicode, sizeof(BeyondUnicode)), ESoftAssetPathResult::InvalidUtf8, "UTF-8 code points above U+10FFFF are rejected");

    FAssetFailingAllocator Allocator;
    {
        FSoftAssetPath TransactionalPath(Allocator);
        Failures += ExpectAsset(TransactionalPath.TryAssign("Game/Seed") == ESoftAssetPathResult::Success, "soft path OOM fixture has a committed seed value");
        const String Before(TransactionalPath.View());
        Allocator.bFailAllocations = true;
        Failures += ExpectAsset(TransactionalPath.TryAssign("Game/Long/Path/That/Needs/Storage") == ESoftAssetPathResult::AllocationFailed && TransactionalPath.View() == Before.View(), "soft path allocation failure preserves the previous canonical value");
        Allocator.bFailAllocations = false;
    }
    Failures += ExpectAsset(Allocator.LiveAllocations == 0, "soft path releases all allocator-owned temporary and committed storage");

    const FAssetId FirstId = ParseAssetStableId<FAssetId>("b0000000-0000-4000-8000-000000000001");
    const FAssetId SecondId = ParseAssetStableId<FAssetId>("b0000000-0000-4000-8000-000000000002");
    const FAssetId ThirdId = ParseAssetStableId<FAssetId>("b0000000-0000-4000-8000-000000000003");
    const FAssetId FourthId = ParseAssetStableId<FAssetId>("b0000000-0000-4000-8000-000000000004");

    {
        FAssetFailingAllocator InvalidRegistryAllocator;
        InvalidRegistryAllocator.bFailAllocations = true;
        FAssetRegistry InvalidRegistry(InvalidRegistryAllocator);
        FAssetTestTextAsset Object;
        FTextHandle Preserved;
        Failures += ExpectAsset(!InvalidRegistry.IsValid() && InvalidRegistry.GetAssetCount() == 0 && InvalidRegistry.Register(MakeTextRegistration(FirstId, "Game/Test/Invalid", Object), Preserved) == EAssetRegisterResult::InvalidRegistry && !Preserved.IsValid() && InvalidRegistryAllocator.LiveAllocations == 0, "PImpl allocation failure leaves an inert registry and preserves outputs");
    }

    {
        FAssetRegistry Registry;
        FAssetRegistry ForeignRegistry;
        FAssetTestTextAsset FirstObject{11};
        FAssetTestTextAsset SecondObject{22};
        FAssetTestTextAsset ThirdObject{33};
        FAssetTestTextAsset ForeignObject{55};
        FAssetTestBinaryAsset BinaryObject{44};
        FAssetTestInvalidTypeAsset InvalidTypeObject;
        char PathBytes[] = "Game\\Text/./Readme";
        char NameBytes[] = "Mutable name";
        char ProviderBytes[] = "BuilderTests.MutableProvider";
        TAssetRegistration<FAssetTestTextAsset> Registration = MakeTextRegistration(FirstId, PathBytes, FirstObject);
        Registration.DisplayName = NameBytes;
        Registration.ProviderName = ProviderBytes;

        FTextHandle FirstHandle;
        Failures += ExpectAsset(Registry.Register(Registration, FirstHandle) == EAssetRegisterResult::Success && FirstHandle.IsValid() && Registry.GetAssetCount() == 1 && FTextHandle(FirstHandle) == FirstHandle && DefaultHash<FTextHandle>{}(FTextHandle(FirstHandle)) == DefaultHash<FTextHandle>{}(FirstHandle), "registry commits one typed borrowed asset");

        FTextHandle ForeignHandle;
        Failures += ExpectAsset(ForeignRegistry.Register(MakeTextRegistration(FirstId, "Game/Text/Readme", ForeignObject), ForeignHandle) == EAssetRegisterResult::Success && ForeignHandle.GetIndex() == FirstHandle.GetIndex() && ForeignHandle.GetGeneration() == FirstHandle.GetGeneration() && ForeignHandle != FirstHandle, "registry domain participates in typed handle value equality");

        TAssetRegistration<FAssetTestInvalidTypeAsset> InvalidTypeRegistration;
        InvalidTypeRegistration.Id = FourthId;
        InvalidTypeRegistration.SoftPath = "Game/Test/InvalidType";
        InvalidTypeRegistration.ProviderName = "BuilderTests.InvalidTypeProvider";
        InvalidTypeRegistration.Object = &InvalidTypeObject;
        TAssetHandle<FAssetTestInvalidTypeAsset> InvalidTypeHandle;
        Failures += ExpectAsset(Registry.Register(InvalidTypeRegistration, InvalidTypeHandle) == EAssetRegisterResult::InvalidTypeId && Registry.Replace(InvalidTypeHandle, InvalidTypeRegistration, InvalidTypeHandle) == EAssetReplaceResult::InvalidTypeId && !InvalidTypeHandle.IsValid() && Registry.GetAssetCount() == 1, "invalid provider type ID is diagnosed before runtime handle validation");
        PathBytes[5] = 'X';
        NameBytes[0] = 'X';
        ProviderBytes[0] = 'X';

        FAssetInfo Info;
        FAssetId ResolvedId;
        const FAssetTestTextAsset* ResolvedObject = nullptr;
        FTextHandle FoundById;
        FTextHandle FoundByPath;
        FTextHandle PreservedHandle = FirstHandle;
        Failures += ExpectAsset(Registry.TryGetInfo(FirstHandle, Info) == EAssetResolveResult::Success && Info.Id == FirstId && Info.TypeId == GetAssetTypeId<FAssetTestTextAsset>() && Info.SoftPath == StringView("Game/Text/Readme") && Info.DisplayName == StringView("Mutable name") && Info.ProviderName == StringView("BuilderTests.MutableProvider") && Info.Ownership == EAssetOwnership::Borrowed && Registry.TryGetAssetId(FirstHandle, ResolvedId) == EAssetResolveResult::Success && ResolvedId == FirstId && Registry.Resolve(FirstHandle, ResolvedObject) == EAssetResolveResult::Success && ResolvedObject == &FirstObject && ResolvedObject->Value == 11 && Registry.TryFindById(FirstId, FoundById) == EAssetFindResult::Success && Registry.TryFindByPath("Game//Text/Folder/../Readme", FoundByPath) == EAssetFindResult::Success && FoundById == FirstHandle && FoundByPath == FirstHandle, "registry deep-copies metadata and resolves immutable typed objects by ID or path");
        Failures += ExpectAsset(Registry.TryFindByPath("game/Text/Readme", PreservedHandle) == EAssetFindResult::NotFound && PreservedHandle == FirstHandle, "registry path lookup is case-sensitive and preserves output on miss");

        Failures += ExpectAsset(Registry.Register(MakeTextRegistration(ThirdId, Info.SoftPath, SecondObject), PreservedHandle) == EAssetRegisterResult::DuplicatePath && PreservedHandle == FirstHandle, "registration safely canonicalizes a path view aliased from registry metadata");
        Failures += ExpectAsset(Registry.Register(MakeTextRegistration(FirstId, "Game/Test/Other", SecondObject), PreservedHandle) == EAssetRegisterResult::DuplicateAssetId && PreservedHandle == FirstHandle && Registry.GetAssetCount() == 1, "duplicate persistent ID is rejected without changing output");
        Failures += ExpectAsset(Registry.Register(MakeTextRegistration(SecondId, "Game/Text/./Readme", SecondObject), PreservedHandle) == EAssetRegisterResult::DuplicatePath && PreservedHandle == FirstHandle && Registry.GetAssetCount() == 1, "duplicate canonical path is rejected without changing output");
        Failures += ExpectAsset(Registry.Register(MakeTextRegistration(SecondId, "Game/Test/Other", FirstObject), PreservedHandle) == EAssetRegisterResult::DuplicateObject && PreservedHandle == FirstHandle && Registry.GetAssetCount() == 1, "one object address cannot back multiple registry entries");

        TAssetRegistration<FAssetTestTextAsset> InvalidRegistration = MakeTextRegistration(ThirdId, "Game/Test/Invalid", SecondObject);
        InvalidRegistration.Id = {};
        Failures += ExpectAsset(Registry.Register(InvalidRegistration, PreservedHandle) == EAssetRegisterResult::InvalidAssetId && PreservedHandle == FirstHandle, "registration rejects an invalid persistent ID transactionally");
        InvalidRegistration = MakeTextRegistration(ThirdId, "C:/Absolute/Asset", SecondObject);
        Failures += ExpectAsset(Registry.Register(InvalidRegistration, PreservedHandle) == EAssetRegisterResult::InvalidPath && PreservedHandle == FirstHandle, "registration rejects a filesystem path transactionally");
        InvalidRegistration = MakeTextRegistration(ThirdId, "Game/Test/InvalidOwnership", SecondObject);
        InvalidRegistration.Ownership = EAssetOwnership::OwnedByProviderThunk;
        Failures += ExpectAsset(Registry.Register(InvalidRegistration, PreservedHandle) == EAssetRegisterResult::InvalidOwnership && PreservedHandle == FirstHandle, "owned registration requires a provider destroy thunk");
        InvalidRegistration = MakeTextRegistration(ThirdId, "Game/Test/UnexpectedDestroy", SecondObject);
        InvalidRegistration.Destroy = &DestroyTestAsset;
        Failures += ExpectAsset(Registry.Register(InvalidRegistration, PreservedHandle) == EAssetRegisterResult::InvalidOwnership && PreservedHandle == FirstHandle, "borrowed registration rejects an ambiguous destroy thunk");
        InvalidRegistration = MakeTextRegistration(ThirdId, "Game/Test/NullObject", SecondObject);
        InvalidRegistration.Object = nullptr;
        Failures += ExpectAsset(Registry.Register(InvalidRegistration, PreservedHandle) == EAssetRegisterResult::NullObject && PreservedHandle == FirstHandle, "registration rejects a null object transactionally");

        TAssetRegistration<FAssetTestBinaryAsset> BinaryRegistration;
        BinaryRegistration.Id = SecondId;
        BinaryRegistration.SoftPath = "Game/Test/Binary";
        BinaryRegistration.DisplayName = "Binary";
        BinaryRegistration.ProviderName = "BuilderTests.BinaryProvider";
        BinaryRegistration.Object = &BinaryObject;
        FBinaryHandle BinaryHandle;
        Failures += ExpectAsset(Registry.Register(BinaryRegistration, BinaryHandle) == EAssetRegisterResult::Success && Registry.GetAssetCount() == 2, "registry accepts a second logical asset type");

        FBinaryHandle PreservedBinary = BinaryHandle;
        Failures += ExpectAsset(Registry.TryFindById(FirstId, PreservedBinary) == EAssetFindResult::TypeMismatch && PreservedBinary == BinaryHandle, "typed ID lookup reports type mismatch and preserves output");
        FBinaryHandle ForgedBinary;
        std::memcpy(&ForgedBinary, &FirstHandle, sizeof(ForgedBinary));
        const FAssetTestBinaryAsset* PreservedBinaryObject = &BinaryObject;
        Failures += ExpectAsset(Registry.Resolve(ForgedBinary, PreservedBinaryObject) == EAssetResolveResult::TypeMismatch && PreservedBinaryObject == &BinaryObject, "registry type ID validation rejects a byte-forged cross-type handle");

        const FAssetTestTextAsset* PreservedTextObject = &SecondObject;
        Failures += ExpectAsset(ForeignRegistry.Resolve(FirstHandle, PreservedTextObject) == EAssetResolveResult::ForeignRegistry && PreservedTextObject == &SecondObject, "registry domain rejects a handle issued by another registry");
        FTextHandle InvalidHandle;
        Failures += ExpectAsset(Registry.Resolve(InvalidHandle, PreservedTextObject) == EAssetResolveResult::InvalidHandle && PreservedTextObject == &SecondObject, "default runtime handle is rejected without changing object output");

        Failures += ExpectAsset(Registry.Unregister(FirstHandle) == EAssetUnregisterResult::Success && Registry.GetAssetCount() == 1 && Registry.Resolve(FirstHandle, PreservedTextObject) == EAssetResolveResult::StaleHandle && PreservedTextObject == &SecondObject, "unregister invalidates a borrowed handle without destroying its object");

        FTextHandle ReusedHandle;
        Failures += ExpectAsset(Registry.Register(MakeTextRegistration(ThirdId, "Game/Test/Reused", ThirdObject), ReusedHandle) == EAssetRegisterResult::Success && ReusedHandle.GetIndex() == FirstHandle.GetIndex() && ReusedHandle.GetGeneration() == FirstHandle.GetGeneration() + 1 && Registry.TryFindById(FirstId, PreservedHandle) == EAssetFindResult::NotFound, "slot reuse advances generation while persistent ID lookup forgets removed entries");
    }

    {
        FAssetRegistry Registry;
        FAssetTestTextAsset OldObject{101};
        FAssetTestTextAsset NewObject{202};
        FAssetTestTextAsset OtherObject{303};
        FAssetTestTextAsset CandidateObject{404};
        FAssetDestroyContext OldDestroy;
        FAssetDestroyContext NewDestroy;
        OldDestroy.Registry = &Registry;
        NewDestroy.Registry = &Registry;

        FTextHandle OldHandle;
        Failures += ExpectAsset(Registry.Register(MakeTextRegistration(FirstId, "Game/Owned/Original", OldObject, EAssetOwnership::OwnedByProviderThunk, &OldDestroy), OldHandle) == EAssetRegisterResult::Success, "owned registration transfers cleanup to a provider thunk");
        OldDestroy.ReentryHandle = OldHandle;

        FTextHandle OtherHandle;
        Failures += ExpectAsset(Registry.Register(MakeTextRegistration(SecondId, "Game/Owned/Other", OtherObject), OtherHandle) == EAssetRegisterResult::Success, "replace conflict fixture registers an independent borrowed entry");

        FTextHandle PreservedNew = OtherHandle;
        Failures += ExpectAsset(Registry.Replace(OldHandle, MakeTextRegistration(FirstId, "Game/Owned/Original", OldObject, EAssetOwnership::OwnedByProviderThunk, &OldDestroy), PreservedNew) == EAssetReplaceResult::DuplicateObject && PreservedNew == OtherHandle && OldDestroy.DestroyCount == 0, "replace rejects same-pointer ownership handoff without consuming it");
        Failures += ExpectAsset(Registry.Replace(OldHandle, MakeTextRegistration(FirstId, "Game/Owned/Other", CandidateObject), PreservedNew) == EAssetReplaceResult::DuplicatePath && PreservedNew == OtherHandle && OldDestroy.DestroyCount == 0, "replace rejects a path owned by another entry transactionally");
        Failures += ExpectAsset(Registry.Replace(OldHandle, MakeTextRegistration(FirstId, "Game/Owned/Candidate", OtherObject), PreservedNew) == EAssetReplaceResult::DuplicateObject && PreservedNew == OtherHandle && OldDestroy.DestroyCount == 0, "replace rejects an object owned by another entry transactionally");
        Failures += ExpectAsset(Registry.Replace(OldHandle, MakeTextRegistration(ThirdId, "Game/Owned/Candidate", CandidateObject), PreservedNew) == EAssetReplaceResult::IdentityMismatch && PreservedNew == OtherHandle && OldDestroy.DestroyCount == 0, "replace cannot change persistent or logical type identity");

        FAssetInfo OldInfo;
        Failures += ExpectAsset(Registry.TryGetInfo(OldHandle, OldInfo) == EAssetResolveResult::Success, "replace path-alias fixture borrows current metadata");
        TAssetRegistration<FAssetTestTextAsset> Replacement = MakeTextRegistration(FirstId, OldInfo.SoftPath, NewObject, EAssetOwnership::OwnedByProviderThunk, &NewDestroy);
        FTextHandle NewHandle = OtherHandle;
        Failures += ExpectAsset(Registry.Replace(OldHandle, Replacement, NewHandle) == EAssetReplaceResult::Success && NewHandle.GetIndex() == OldHandle.GetIndex() && NewHandle.GetGeneration() == OldHandle.GetGeneration() + 1 && NewHandle != OtherHandle && Registry.GetAssetCount() == 2 && OldDestroy.DestroyCount == 1 && OldDestroy.ReentryResult == EAssetUnregisterResult::InvalidState, "replace copies aliased path before commit then destroys old ownership under guard");
        NewDestroy.ReentryHandle = NewHandle;
        const FAssetTestTextAsset* ResolvedNew = nullptr;
        Failures += ExpectAsset(Registry.Resolve(OldHandle, ResolvedNew) == EAssetResolveResult::StaleHandle && Registry.Resolve(NewHandle, ResolvedNew) == EAssetResolveResult::Success && ResolvedNew == &NewObject && ResolvedNew->Value == 202, "successful replace stales the old handle and publishes immutable new object");
        Failures += ExpectAsset(Registry.Unregister(NewHandle) == EAssetUnregisterResult::Success && NewDestroy.DestroyCount == 1 && NewDestroy.ReentryResult == EAssetUnregisterResult::InvalidState, "owned unregister calls provider cleanup exactly once under mutation guard");
    }

    {
        bool bReachedSuccessfulRegistration = false;
        bool bEveryFailureWasTransactional = true;
        std::size_t CoveredFailurePoints = 0;
        for (std::size_t FailureOffset = 1; FailureOffset <= 96; ++FailureOffset)
        {
            FAssetFailingAllocator FailingAllocator;
            FAssetDestroyContext DestroyContext;
            FAssetTestTextAsset Object{501};
            EAssetRegisterResult Result = EAssetRegisterResult::InvalidRegistry;
            bool bInsideScopeValid = false;
            {
                FAssetRegistry Registry(FailingAllocator);
                const std::size_t FirstRegistrationAllocation = FailingAllocator.AllocationAttempts + FailureOffset;
                FailingAllocator.FailureAttempt = FirstRegistrationAllocation;
                FTextHandle Output;
                Result = Registry.Register(MakeTextRegistration(FourthId, "Game/Oom/FreshRegistration", Object, EAssetOwnership::OwnedByProviderThunk, &DestroyContext), Output);
                bInsideScopeValid = Result == EAssetRegisterResult::Success ? Output.IsValid() && Registry.GetAssetCount() == 1 && DestroyContext.DestroyCount == 0 : Result == EAssetRegisterResult::AllocationFailed && !Output.IsValid() && Registry.GetAssetCount() == 0 && DestroyContext.DestroyCount == 0;
            }

            bEveryFailureWasTransactional = bEveryFailureWasTransactional && bInsideScopeValid && FailingAllocator.LiveAllocations == 0;
            if (Result == EAssetRegisterResult::Success)
            {
                bReachedSuccessfulRegistration = true;
                bEveryFailureWasTransactional = bEveryFailureWasTransactional && DestroyContext.DestroyCount == 1;
                break;
            }
            ++CoveredFailurePoints;
            bEveryFailureWasTransactional = bEveryFailureWasTransactional && DestroyContext.DestroyCount == 0;
        }
        Failures += ExpectAsset(bReachedSuccessfulRegistration && CoveredFailurePoints >= 8 && bEveryFailureWasTransactional, "fresh Register is transactional at every allocator failure point and transfers ownership only on success");
    }

    {
        FAssetFailingAllocator FailingAllocator;
        FAssetTestTextAsset FirstObject{551};
        FAssetTestTextAsset SecondObject{552};
        FAssetRegistry Registry(FailingAllocator);
        FTextHandle FirstHandle;
        Failures += ExpectAsset(Registry.Register(MakeTextRegistration(FirstId, "Game/Oom/RetiredSlot", FirstObject), FirstHandle) == EAssetRegisterResult::Success, "free-slot OOM fixture registers an asset");
        FailingAllocator.bFailAllocations = true;
        Failures += ExpectAsset(Registry.Unregister(FirstHandle) == EAssetUnregisterResult::Success && Registry.GetAssetCount() == 0, "unregister completes when free-slot tracking allocation fails");
        FailingAllocator.bFailAllocations = false;
        FTextHandle SecondHandle;
        Failures += ExpectAsset(Registry.Register(MakeTextRegistration(SecondId, "Game/Oom/NewSlot", SecondObject), SecondHandle) == EAssetRegisterResult::Success && SecondHandle.GetIndex() != FirstHandle.GetIndex(), "untracked free slot is retired rather than reused unsafely");
    }

    {
        bool bReachedSuccessfulReuse = false;
        bool bEveryFailurePreservedGeneration = true;
        std::size_t CoveredFailurePoints = 0;
        for (std::size_t FailureOffset = 1; FailureOffset <= 96; ++FailureOffset)
        {
            FAssetFailingAllocator FailingAllocator;
            FAssetDestroyContext DestroyContext;
            FAssetTestTextAsset SeedObject{601};
            FAssetTestTextAsset CandidateObject{602};
            EAssetRegisterResult Result = EAssetRegisterResult::InvalidRegistry;
            bool bInsideScopeValid = false;
            {
                FAssetRegistry Registry(FailingAllocator);
                FTextHandle ReleasedHandle;
                bInsideScopeValid = Registry.Register(MakeTextRegistration(FirstId, "Game/Oom/Released", SeedObject), ReleasedHandle) == EAssetRegisterResult::Success && Registry.Unregister(ReleasedHandle) == EAssetUnregisterResult::Success;

                FailingAllocator.FailureAttempt = FailingAllocator.AllocationAttempts + FailureOffset;
                FTextHandle Output = ReleasedHandle;
                Result = Registry.Register(MakeTextRegistration(SecondId, "Game/Oom/Reused", CandidateObject, EAssetOwnership::OwnedByProviderThunk, &DestroyContext), Output);
                if (Result == EAssetRegisterResult::AllocationFailed)
                {
                    bInsideScopeValid = bInsideScopeValid && Output == ReleasedHandle && Registry.GetAssetCount() == 0 && DestroyContext.DestroyCount == 0;
                    FailingAllocator.FailureAttempt = 0;
                    FTextHandle Verification;
                    bInsideScopeValid = bInsideScopeValid && Registry.Register(MakeTextRegistration(SecondId, "Game/Oom/Reused", CandidateObject), Verification) == EAssetRegisterResult::Success && Verification.GetIndex() == ReleasedHandle.GetIndex() && Verification.GetGeneration() == ReleasedHandle.GetGeneration() + 1;
                }
                else
                {
                    bInsideScopeValid = bInsideScopeValid && Result == EAssetRegisterResult::Success && Output.GetIndex() == ReleasedHandle.GetIndex() && Output.GetGeneration() == ReleasedHandle.GetGeneration() + 1 && DestroyContext.DestroyCount == 0;
                }
            }

            bEveryFailurePreservedGeneration = bEveryFailurePreservedGeneration && bInsideScopeValid && FailingAllocator.LiveAllocations == 0;
            if (Result == EAssetRegisterResult::Success)
            {
                bReachedSuccessfulReuse = true;
                bEveryFailurePreservedGeneration = bEveryFailurePreservedGeneration && DestroyContext.DestroyCount == 1;
                break;
            }
            ++CoveredFailurePoints;
            bEveryFailurePreservedGeneration = bEveryFailurePreservedGeneration && DestroyContext.DestroyCount == 0;
        }
        Failures += ExpectAsset(bReachedSuccessfulReuse && CoveredFailurePoints >= 6 && bEveryFailurePreservedGeneration, "failed reused-slot Register never consumes generation or ownership at any allocation point");
    }

    {
        bool bReachedSuccessfulReplace = false;
        bool bEveryFailureWasTransactional = true;
        std::size_t CoveredFailurePoints = 0;
        for (std::size_t FailureOffset = 1; FailureOffset <= 96; ++FailureOffset)
        {
            FAssetFailingAllocator FailingAllocator;
            FAssetDestroyContext OldDestroy;
            FAssetDestroyContext NewDestroy;
            FAssetTestTextAsset OldObject{701};
            FAssetTestTextAsset NewObject{702};
            EAssetReplaceResult Result = EAssetReplaceResult::InvalidRegistry;
            bool bInsideScopeValid = false;
            {
                FAssetRegistry Registry(FailingAllocator);
                FTextHandle OldHandle;
                bInsideScopeValid = Registry.Register(MakeTextRegistration(FirstId, "Game/Oom/ReplaceOld", OldObject, EAssetOwnership::OwnedByProviderThunk, &OldDestroy), OldHandle) == EAssetRegisterResult::Success;
                const FTextHandle PreservedOutput = OldHandle;
                FTextHandle Output = PreservedOutput;
                FailingAllocator.FailureAttempt = FailingAllocator.AllocationAttempts + FailureOffset;
                Result = Registry.Replace(OldHandle, MakeTextRegistration(FirstId, "Game/Oom/ReplaceNew", NewObject, EAssetOwnership::OwnedByProviderThunk, &NewDestroy), Output);

                const FAssetTestTextAsset* Resolved = nullptr;
                if (Result == EAssetReplaceResult::AllocationFailed)
                {
                    bInsideScopeValid = bInsideScopeValid && Output == PreservedOutput && Registry.GetAssetCount() == 1 && OldDestroy.DestroyCount == 0 && NewDestroy.DestroyCount == 0 && Registry.Resolve(OldHandle, Resolved) == EAssetResolveResult::Success && Resolved == &OldObject;
                }
                else
                {
                    bInsideScopeValid = bInsideScopeValid && Result == EAssetReplaceResult::Success && Output.GetGeneration() == OldHandle.GetGeneration() + 1 && Registry.GetAssetCount() == 1 && OldDestroy.DestroyCount == 1 && NewDestroy.DestroyCount == 0 && Registry.Resolve(OldHandle, Resolved) == EAssetResolveResult::StaleHandle && Registry.Resolve(Output, Resolved) == EAssetResolveResult::Success && Resolved == &NewObject;
                }
            }

            bEveryFailureWasTransactional = bEveryFailureWasTransactional && bInsideScopeValid && FailingAllocator.LiveAllocations == 0;
            if (Result == EAssetReplaceResult::Success)
            {
                bReachedSuccessfulReplace = true;
                bEveryFailureWasTransactional = bEveryFailureWasTransactional && OldDestroy.DestroyCount == 1 && NewDestroy.DestroyCount == 1;
                break;
            }
            ++CoveredFailurePoints;
            bEveryFailureWasTransactional = bEveryFailureWasTransactional && OldDestroy.DestroyCount == 1 && NewDestroy.DestroyCount == 0;
        }
        Failures += ExpectAsset(bReachedSuccessfulReplace && CoveredFailurePoints >= 6 && bEveryFailureWasTransactional, "Replace is transactional at every allocator failure point and destroys only committed ownership");
    }

    {
        Array<int> DestroyEvents;
        bool bProviderLoaded = true;
        FAssetDestroyContext Contexts[3];
        FAssetTestTextAsset Objects[3] = {{1}, {2}, {3}};
        {
            FAssetRegistry Registry;
            const FAssetId Ids[3] = {FirstId, SecondId, ThirdId};
            const char* const Paths[3] = {"Game/Order/First", "Game/Order/Second", "Game/Order/Third"};
            for (int Index = 0; Index < 3; ++Index)
            {
                Contexts[Index].Events = &DestroyEvents;
                Contexts[Index].ProviderLoaded = &bProviderLoaded;
                Contexts[Index].Marker = Index + 1;
                FTextHandle Handle;
                Failures += ExpectAsset(Registry.Register(MakeTextRegistration(Ids[Index], Paths[Index], Objects[Index], EAssetOwnership::OwnedByProviderThunk, &Contexts[Index]), Handle) == EAssetRegisterResult::Success, "reverse destruction fixture registers owned provider object");
            }
        }
        bProviderLoaded = false;
        Failures += ExpectAsset(DestroyEvents.Size() == 3 && DestroyEvents[0] == 3 && DestroyEvents[1] == 2 && DestroyEvents[2] == 1 && Contexts[0].DestroyCount == 1 && Contexts[1].DestroyCount == 1 && Contexts[2].DestroyCount == 1 && Contexts[0].bDestroyedWhileProviderLoaded && Contexts[1].bDestroyedWhileProviderLoaded && Contexts[2].bDestroyedWhileProviderLoaded, "registry destruction invokes owned provider thunks once in reverse order before provider unload");
    }

    return Failures;
}

} // namespace LE
