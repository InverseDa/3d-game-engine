#include "World/WorldMinimal.h"

#include "Memory/Allocator.h"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace LE
{
namespace
{

int Expect(const bool bCondition, const char* const Name)
{
    if (bCondition)
    {
        return 0;
    }
    std::fprintf(stderr, "FAILED: %s\n", Name);
    return 1;
}

class FWorldFailingAllocator final : public IAllocator
{
public:
    explicit FWorldFailingAllocator(const bool bInFail = false) noexcept
        : bFail(bInFail)
    {
    }

    void* Allocate(const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        ++AllocationAttempts;
        if (Alignment > MaximumAlignment) MaximumAlignment = Alignment;
        if (bFail || (FailOnAttempt != 0 && AllocationAttempts == FailOnAttempt)) return nullptr;
        void* const Result = GetDefaultAllocator().Allocate(Size, Alignment);
        if (Result != nullptr) ++LiveAllocations;
        return Result;
    }

    void Deallocate(
        void* const Address,
        const std::size_t Size,
        const std::size_t Alignment) noexcept override
    {
        if (Address != nullptr)
        {
            --LiveAllocations;
            ++DeallocationCount;
        }
        GetDefaultAllocator().Deallocate(Address, Size, Alignment);
    }

    bool bFail = false;
    std::size_t FailOnAttempt = 0;
    std::size_t AllocationAttempts = 0;
    std::size_t DeallocationCount = 0;
    std::size_t LiveAllocations = 0;
    std::size_t MaximumAlignment = 0;
};

struct FIntComponent
{
    int Value = 0;
};

struct FOtherComponent
{
    int Value = 0;
};

struct FMoveOnlyComponent
{
    explicit FMoveOnlyComponent(const int InValue = 0) noexcept
        : Value(InValue)
    {
        ++ConstructionCount;
        ++LiveCount;
    }

    FMoveOnlyComponent(const FMoveOnlyComponent&) = delete;
    FMoveOnlyComponent& operator=(const FMoveOnlyComponent&) = delete;

    FMoveOnlyComponent(FMoveOnlyComponent&& Other) noexcept
        : Value(Other.Value)
    {
        Other.Value = -1;
        ++ConstructionCount;
        ++MoveCount;
        ++LiveCount;
    }

    ~FMoveOnlyComponent() noexcept
    {
        ++DestructionCount;
        --LiveCount;
    }

    static void ResetCounts() noexcept
    {
        ConstructionCount = 0;
        MoveCount = 0;
        DestructionCount = 0;
        LiveCount = 0;
    }

    int Value = 0;
    static int ConstructionCount;
    static int MoveCount;
    static int DestructionCount;
    static int LiveCount;
};

int FMoveOnlyComponent::ConstructionCount = 0;
int FMoveOnlyComponent::MoveCount = 0;
int FMoveOnlyComponent::DestructionCount = 0;
int FMoveOnlyComponent::LiveCount = 0;

struct alignas(128) FAlignedComponent
{
    explicit FAlignedComponent(const int InValue = 0) noexcept
        : Value(InValue)
    {
    }

    int Value = 0;
};

struct FQueryWorldOnDestroyComponent
{
    FQueryWorldOnDestroyComponent() noexcept = default;
    FQueryWorldOnDestroyComponent(const FQueryWorldOnDestroyComponent&) = delete;
    FQueryWorldOnDestroyComponent& operator=(const FQueryWorldOnDestroyComponent&) = delete;

    FQueryWorldOnDestroyComponent(FQueryWorldOnDestroyComponent&& Other) noexcept
        : World(Other.World)
        , Entity(Other.Entity)
        , Output(Other.Output)
        , QueryResult(Other.QueryResult)
        , bArmed(Other.bArmed)
    {
        Other.bArmed = false;
    }

    ~FQueryWorldOnDestroyComponent() noexcept
    {
        if (bArmed)
        {
            *QueryResult = World->TryGetWorldMatrix(Entity, *Output);
        }
    }

    FWorld* World = nullptr;
    FEntity Entity;
    Math::Matrix4f* Output = nullptr;
    bool* QueryResult = nullptr;
    bool bArmed = false;
};

struct FDeferredReentryComponent
{
    FDeferredReentryComponent() noexcept = default;
    FDeferredReentryComponent(const FDeferredReentryComponent&) = delete;
    FDeferredReentryComponent& operator=(const FDeferredReentryComponent&) = delete;
    FDeferredReentryComponent(FDeferredReentryComponent&& Other) noexcept
        : World(Other.World)
        , Commands(Other.Commands)
        , Entity(Other.Entity)
        , Token(Other.Token)
        , RecordResult(Other.RecordResult)
        , ResolveResult(Other.ResolveResult)
        , ImmediateResult(Other.ImmediateResult)
        , bArmed(Other.bArmed)
    {
        Other.bArmed = false;
    }
    ~FDeferredReentryComponent() noexcept
    {
        if (!bArmed) return;
        *RecordResult = Commands->TryDestroyEntity(Entity);
        FEntity Preserved{FEntitySlotHandle{7, 7}, 7};
        *ResolveResult = Commands->TryResolveDeferredEntity(Token, Preserved);
        *ImmediateResult = World->DestroyEntity(Entity);
    }

    FWorld* World = nullptr;
    FWorldCommandBuffer* Commands = nullptr;
    FEntity Entity;
    FDeferredEntityToken Token;
    EWorldCommandRecordResult* RecordResult = nullptr;
    EDeferredEntityResolveResult* ResolveResult = nullptr;
    bool* ImmediateResult = nullptr;
    bool bArmed = false;
};

static_assert(!std::is_copy_constructible<TComponentStorage<FMoveOnlyComponent>>::value,
    "component storage must preserve a single registration node and allocation owner");
static_assert(!std::is_move_constructible<TComponentStorage<FMoveOnlyComponent>>::value,
    "bound component storage must not move");
static_assert(std::is_standard_layout<FTransform>::value,
    "Transform must remain a standard-layout World API value");
static_assert(std::is_trivially_copyable<FTransform>::value,
    "Transform must remain a trivially-copyable World API value");

bool NearlyEqual(const float Left, const float Right) noexcept
{
    return std::fabs(Left - Right) <= 1.0e-4f;
}

bool MatrixNearlyEqual(const Math::Matrix4f& Left, const Math::Matrix4f& Right) noexcept
{
    for (std::size_t Index = 0; Index < 16; ++Index)
    {
        if (!NearlyEqual(Left.Values[Index], Right.Values[Index]))
        {
            return false;
        }
    }
    return true;
}

bool HasTranslation(
    const Math::Matrix4f& Matrix,
    const float X,
    const float Y,
    const float Z) noexcept
{
    return NearlyEqual(Matrix(0, 3), X) && NearlyEqual(Matrix(1, 3), Y) &&
        NearlyEqual(Matrix(2, 3), Z) && NearlyEqual(Matrix(3, 3), 1.0f);
}

int RunEntityLifetimeTests()
{
    int Failures = 0;
    FWorld World;
    FEntity First;
    FEntity Second;
    Failures += Expect(World.IsValid() && World.TryCreateEntity(First) &&
        World.TryCreateEntity(Second) && First.IsValid() && Second.IsValid() &&
        First.GetIndex() == 0 && First.GetGeneration() == 1 &&
        Second.GetIndex() == 1 && Second.GetGeneration() == 1 &&
        World.IsAlive(First) && World.IsAlive(Second) && World.GetEntityCount() == 2,
        "World creates live index-generation Entities");

    Failures += Expect(World.DestroyEntity(First) && !World.IsAlive(First) &&
        !World.DestroyEntity(First) && World.GetEntityCount() == 1,
        "Entity destruction rejects stale and double destroy");
    FEntity Reused;
    Failures += Expect(World.TryCreateEntity(Reused) &&
        Reused.GetIndex() == First.GetIndex() &&
        Reused.GetGeneration() == First.GetGeneration() + 1 &&
        !World.IsAlive(First) && World.IsAlive(Reused),
        "Entity slot reuse increments generation and preserves stale rejection");

    FWorld OtherWorld;
    FEntity Foreign;
    Failures += Expect(OtherWorld.TryCreateEntity(Foreign) &&
        Foreign.GetIndex() == 0 && Foreign.GetGeneration() == 1 &&
        Foreign.WorldDomain != Reused.WorldDomain &&
        !World.IsAlive(Foreign) && !World.DestroyEntity(Foreign) &&
        DefaultHash<FEntity>{}(Foreign) != DefaultHash<FEntity>{}(Reused),
        "World domain rejects a foreign Entity with a colliding slot pair");

    FEntity Forged = Reused;
    ++Forged.Slot.Generation;
    Failures += Expect(!World.IsAlive(Forged) && !World.DestroyEntity(Forged),
        "World rejects a forged Entity generation");

    FWorldFailingAllocator GrowthAllocator;
    {
        FWorld GrowthWorld(GrowthAllocator);
        const std::size_t AttemptsBeforeBatch = GrowthAllocator.AllocationAttempts;
        bool bBatchCreated = true;
        for (int Index = 0; Index < 64; ++Index)
        {
            FEntity Entity;
            bBatchCreated = bBatchCreated && GrowthWorld.TryCreateEntity(Entity);
        }
        const std::size_t BatchAttempts =
            GrowthAllocator.AllocationAttempts - AttemptsBeforeBatch;
        Failures += Expect(bBatchCreated && GrowthWorld.GetEntityCount() == 64 &&
            BatchAttempts < 40,
            "dense live-Entity and slot arrays grow geometrically rather than once per create");
    }
    Failures += Expect(GrowthAllocator.LiveAllocations == 0,
        "batch Entity storage returns every geometric allocation");
    return Failures;
}

int RunComponentStorageTests()
{
    int Failures = 0;
    FWorld World;
    TComponentStorage<FIntComponent> Values;
    TComponentStorage<FOtherComponent> Others;
    Failures += Expect(World.AttachComponentStorage(Values) &&
        World.AttachComponentStorage(Others) && Values.IsBound() && Others.IsBound(),
        "typed component storages attach without allocation");

    FEntity A;
    FEntity B;
    FEntity C;
    World.TryCreateEntity(A);
    World.TryCreateEntity(B);
    World.TryCreateEntity(C);

    FIntComponent One{10};
    Failures += Expect(World.TryAddComponent(Values, A, One) == EComponentMutationResult::Added &&
        World.TryAddComponent(Values, B, FIntComponent{20}) == EComponentMutationResult::Added &&
        World.TryAddComponent(Values, C, FIntComponent{30}) == EComponentMutationResult::Added &&
        Values.Size() == 3 && World.GetComponent(Values, B) != nullptr &&
        World.GetComponent(Values, B)->Value == 20,
        "typed Add and Get address components by Entity");

    const uint64 DuplicateVersion = Values.GetMutationVersion();
    Failures += Expect(World.TryAddComponent(Values, A, FIntComponent{99}) ==
            EComponentMutationResult::AlreadyPresent &&
        Values.GetMutationVersion() == DuplicateVersion &&
        World.GetComponent(Values, A)->Value == 10,
        "duplicate Add preserves the component and mutation version");

    Failures += Expect(World.TrySetComponent(Values, B, FIntComponent{22}) ==
            EComponentMutationResult::Replaced &&
        World.GetComponent(Values, B)->Value == 22 &&
        World.TrySetComponent(Others, A, FOtherComponent{7}) ==
            EComponentMutationResult::Added,
        "Set replaces an existing component and adds a missing component");

    const Span<const FEntity> BeforeEntities = Values.GetEntities();
    const Span<const FIntComponent> BeforeComponents = Values.GetComponents();
    bool bDensePairsValid = BeforeEntities.Size() == 3 && BeforeComponents.Size() == 3;
    for (std::size_t Index = 0; Index < BeforeEntities.Size(); ++Index)
    {
        const FIntComponent* const Found = World.GetComponent(Values, BeforeEntities[Index]);
        bDensePairsValid = bDensePairsValid && Found == &BeforeComponents[Index];
    }
    Failures += Expect(bDensePairsValid,
        "dense Entity and component spans share one contiguous index mapping");

    const uint64 RemoveVersion = Values.GetMutationVersion();
    Failures += Expect(World.RemoveComponent(Values, B) == EComponentRemoveResult::Removed &&
        Values.Size() == 2 && Values.GetMutationVersion() == RemoveVersion + 1 &&
        World.GetComponent(Values, B) == nullptr &&
        Values.GetEntities()[1] == C && World.GetComponent(Values, C)->Value == 30,
        "Remove swap-compacts dense storage and repairs the moved sparse entry");
    const uint64 MissingVersion = Values.GetMutationVersion();
    Failures += Expect(World.RemoveComponent(Values, B) == EComponentRemoveResult::NotPresent &&
        Values.GetMutationVersion() == MissingVersion,
        "missing Remove is a version-preserving no-op");

    FWorld ForeignWorld;
    FEntity Foreign;
    ForeignWorld.TryCreateEntity(Foreign);
    const uint64 ForeignVersion = Values.GetMutationVersion();
    Failures += Expect(World.TryAddComponent(Values, Foreign, FIntComponent{1}) ==
            EComponentMutationResult::InvalidEntity &&
        World.GetComponent(Values, Foreign) == nullptr &&
        World.RemoveComponent(Values, Foreign) == EComponentRemoveResult::InvalidEntity &&
        Values.GetMutationVersion() == ForeignVersion,
        "component helpers reject foreign Entities without mutation");

    Failures += Expect(World.DestroyEntity(A) &&
        World.GetComponent(Values, A) == nullptr &&
        World.GetComponent(Others, A) == nullptr,
        "Entity destruction removes components from every attached storage");
    Failures += Expect(World.Reset() && Values.IsEmpty() && Others.IsEmpty() &&
        World.GetEntityCount() == 0 && !World.IsAlive(C),
        "World Reset clears all storages and invalidates every live Entity");
    return Failures;
}

int RunComponentLifetimeAndAlignmentTests()
{
    int Failures = 0;
    FMoveOnlyComponent::ResetCounts();
    {
        FWorld World;
        TComponentStorage<FMoveOnlyComponent> Values;
        World.AttachComponentStorage(Values);
        FEntity A;
        FEntity B;
        FEntity C;
        World.TryCreateEntity(A);
        World.TryCreateEntity(B);
        World.TryCreateEntity(C);
        World.TryAddComponent(Values, A, FMoveOnlyComponent{1});
        World.TryAddComponent(Values, B, FMoveOnlyComponent{2});
        World.TryAddComponent(Values, C, FMoveOnlyComponent{3});
        FMoveOnlyComponent Duplicate{99};
        const uint64 DuplicateVersion = Values.GetMutationVersion();
        Failures += Expect(World.TryAddComponent(Values, A, std::move(Duplicate)) ==
                EComponentMutationResult::AlreadyPresent &&
            Duplicate.Value == 99 && Values.GetMutationVersion() == DuplicateVersion,
            "duplicate move-only Add neither consumes its rvalue nor advances mutation version");
        Failures += Expect(World.TrySetComponent(Values, A, FMoveOnlyComponent{11}) ==
                EComponentMutationResult::Replaced &&
            World.GetComponent(Values, A)->Value == 11,
            "component replacement supports move-only non-trivial values");
        Failures += Expect(World.RemoveComponent(Values, B) == EComponentRemoveResult::Removed &&
            World.GetComponent(Values, C) != nullptr &&
            World.GetComponent(Values, C)->Value == 3,
            "swap-remove supports move-only non-trivial components");
        World.DestroyEntity(A);
    }
    Failures += Expect(FMoveOnlyComponent::LiveCount == 0 &&
        FMoveOnlyComponent::ConstructionCount == FMoveOnlyComponent::DestructionCount &&
        FMoveOnlyComponent::MoveCount > 0,
        "component relocation and cleanup balance every non-trivial lifetime exactly once");

    FWorldFailingAllocator AlignedAllocator;
    {
        FWorld World;
        TComponentStorage<FAlignedComponent> Aligned(AlignedAllocator);
        World.AttachComponentStorage(Aligned);
        FEntity Entity;
        World.TryCreateEntity(Entity);
        Failures += Expect(World.TryAddComponent(Aligned, Entity, FAlignedComponent{9}) ==
                EComponentMutationResult::Added &&
            reinterpret_cast<std::uintptr_t>(Aligned.GetComponents().Data()) %
                alignof(FAlignedComponent) == 0 &&
            AlignedAllocator.MaximumAlignment >= alignof(FAlignedComponent),
            "component storage preserves the real address of an over-aligned dense T array");
    }
    Failures += Expect(AlignedAllocator.LiveAllocations == 0,
        "over-aligned component storage returns memory through its creating allocator");
    return Failures;
}

int RunWorldFailureAndDetachTests()
{
    int Failures = 0;
    FWorldFailingAllocator InvalidAllocator(true);
    FWorld InvalidWorld(InvalidAllocator);
    FEntity Preserved{FEntitySlotHandle{7, 9}, 11};
    Failures += Expect(!InvalidWorld.IsValid() && !InvalidWorld.TryCreateEntity(Preserved) &&
        Preserved == FEntity{FEntitySlotHandle{7, 9}, 11} &&
        InvalidWorld.GetEntityCount() == 0 && InvalidWorld.GetMeshComponents() == nullptr &&
        InvalidWorld.GetTransformMutationVersion() == 0 && !InvalidWorld.UpdateTransforms(),
        "FWorld remains safely constructed and preserves outputs when PImpl allocation fails");

    FWorldFailingAllocator EntityAllocator;
    FWorld EntityWorld(EntityAllocator);
    FEntity EntityOutput{FEntitySlotHandle{4, 5}, 6};
    const std::size_t AttemptsBeforeCreate = EntityAllocator.AllocationAttempts;
#if !defined(NDEBUG)
    EntityAllocator.FailOnAttempt = AttemptsBeforeCreate + 3;
#else
    EntityAllocator.FailOnAttempt = AttemptsBeforeCreate + 2;
#endif
    Failures += Expect(!EntityWorld.TryCreateEntity(EntityOutput) &&
        EntityOutput == FEntity{FEntitySlotHandle{4, 5}, 6} &&
        EntityWorld.GetEntityCount() == 0 &&
        EntityAllocator.AllocationAttempts > AttemptsBeforeCreate,
        "Entity create OOM does not issue a generation or mutate output/count");
    EntityAllocator.FailOnAttempt = 0;
    FEntity FirstAfterFailure;
    Failures += Expect(EntityWorld.TryCreateEntity(FirstAfterFailure) &&
        FirstAfterFailure.GetIndex() == 0 && FirstAfterFailure.GetGeneration() == 1,
        "Entity create retry starts at the first unconsumed generation");

    FWorldFailingAllocator ComponentAllocator;
    FWorld ComponentWorld;
    TComponentStorage<FMoveOnlyComponent> Values(ComponentAllocator);
    ComponentWorld.AttachComponentStorage(Values);
    FEntity Entity;
    ComponentWorld.TryCreateEntity(Entity);
    FMoveOnlyComponent Source{44};
    ComponentAllocator.bFail = true;
    const uint64 VersionBeforeFailure = Values.GetMutationVersion();
    FMoveOnlyComponent* const DataBeforeFailure = Values.GetComponents().Data();
    Failures += Expect(ComponentWorld.TryAddComponent(Values, Entity, std::move(Source)) ==
            EComponentMutationResult::AllocationFailed &&
        Source.Value == 44 && Values.IsEmpty() &&
        Values.GetComponents().Data() == DataBeforeFailure &&
        Values.GetMutationVersion() == VersionBeforeFailure,
        "component Add OOM is transactional and does not consume an rvalue");

    ComponentAllocator.bFail = false;
    FEntity ExistingEntities[4];
    ExistingEntities[0] = Entity;
    for (int Index = 1; Index < 4; ++Index)
    {
        ComponentWorld.TryCreateEntity(ExistingEntities[Index]);
    }
    bool bFilled = true;
    for (int Index = 0; Index < 4; ++Index)
    {
        bFilled = bFilled && ComponentWorld.TryAddComponent(
            Values, ExistingEntities[Index], FMoveOnlyComponent{Index + 1}) ==
            EComponentMutationResult::Added;
    }
    FEntity GrowthEntity;
    ComponentWorld.TryCreateEntity(GrowthEntity);
    FMoveOnlyComponent GrowthSource{55};
    FMoveOnlyComponent* const ExistingData = Values.GetComponents().Data();
    const uint64 ExistingVersion = Values.GetMutationVersion();
    ComponentAllocator.bFail = true;
    Failures += Expect(bFilled &&
        ComponentWorld.TryAddComponent(Values, GrowthEntity, std::move(GrowthSource)) ==
            EComponentMutationResult::AllocationFailed &&
        GrowthSource.Value == 55 && Values.Size() == 4 &&
        Values.GetComponents().Data() == ExistingData &&
        Values.GetMutationVersion() == ExistingVersion,
        "growth OOM preserves existing dense storage, order, version, and rvalue");
    bool bExistingValuesPreserved = true;
    for (int Index = 0; Index < 4; ++Index)
    {
        const FMoveOnlyComponent* const Existing =
            ComponentWorld.GetComponent(Values, ExistingEntities[Index]);
        bExistingValuesPreserved = bExistingValuesPreserved && Existing != nullptr &&
            Existing->Value == Index + 1;
    }
    Failures += Expect(bExistingValuesPreserved,
        "growth OOM preserves every existing sparse-to-dense mapping and value");

    TComponentStorage<FIntComponent> Detached;
    {
        FWorld TemporaryWorld;
        TemporaryWorld.AttachComponentStorage(Detached);
        FEntity TemporaryEntity;
        TemporaryWorld.TryCreateEntity(TemporaryEntity);
        TemporaryWorld.TryAddComponent(Detached, TemporaryEntity, FIntComponent{8});
        Failures += Expect(Detached.IsBound() && Detached.Size() == 1,
            "external storage is live while its World owns the intrusive registration");
    }
    FWorld NewWorld;
    FEntity NewEntity;
    NewWorld.TryCreateEntity(NewEntity);
    Failures += Expect(!Detached.IsBound() && Detached.IsEmpty() &&
        NewWorld.TryAddComponent(Detached, NewEntity, FIntComponent{1}) ==
            EComponentMutationResult::InvalidStorage,
        "World destruction clears and detaches external storage for safe later failure");

    FWorld SurvivingWorld;
    FEntity SurvivingEntity;
    SurvivingWorld.TryCreateEntity(SurvivingEntity);
    {
        TComponentStorage<FIntComponent> ShortLived;
        SurvivingWorld.AttachComponentStorage(ShortLived);
        SurvivingWorld.TryAddComponent(ShortLived, SurvivingEntity, FIntComponent{3});
    }
    Failures += Expect(SurvivingWorld.DestroyEntity(SurvivingEntity),
        "storage destruction detaches before a surviving World mutates its list");
    return Failures;
}

int RunTransformValueAndHierarchyTests()
{
    int Failures = 0;
    constexpr float HalfPi = 1.57079632679489661923f;
    FTransform Composed;
    Composed.Translation = Math::Vector3f(3.0f, 4.0f, 5.0f);
    Composed.Rotation = Math::Quaternionf::FromAxisAngle(
        Math::Vector3f(0.0f, 0.0f, 1.0f), HalfPi);
    Composed.Scale = Math::Vector3f(2.0f, 3.0f, 4.0f);
    const Math::Vector4f Point = Composed.ToMatrix() * Math::Vector4f(1.0f, 0.0f, 0.0f, 1.0f);
    Failures += Expect(NearlyEqual(Point[0], 3.0f) && NearlyEqual(Point[1], 6.0f) &&
        NearlyEqual(Point[2], 5.0f) && NearlyEqual(Point[3], 1.0f),
        "Transform matrix composes column-vector Translation Rotation Scale order");

    FWorld World;
    FEntity Root;
    FEntity Middle;
    FEntity Leaf;
    World.TryCreateEntity(Root);
    World.TryCreateEntity(Middle);
    World.TryCreateEntity(Leaf);
    const uint64 InitialVersion = World.GetTransformMutationVersion();

    FTransform RootLocal;
    RootLocal.Translation = Math::Vector3f(1.0f, 0.0f, 0.0f);
    RootLocal.Rotation = Math::Quaternionf(0.0f, 0.0f, 0.0f, 0.0f);
    FTransform MiddleLocal;
    MiddleLocal.Translation = Math::Vector3f(0.0f, 2.0f, 0.0f);
    FTransform LeafLocal;
    LeafLocal.Translation = Math::Vector3f(0.0f, 0.0f, 3.0f);
    Failures += Expect(
        World.TryAddTransform(Root, RootLocal) == EComponentMutationResult::Added &&
        World.TryAddTransform(Middle, MiddleLocal) == EComponentMutationResult::Added &&
        World.TryAddTransform(Leaf, LeafLocal) == EComponentMutationResult::Added &&
        World.GetTransformMutationVersion() == InitialVersion + 3,
        "Transform Add authors one version per successful component");

    FTransform CanonicalRoot;
    Failures += Expect(World.TryGetLocalTransform(Root, CanonicalRoot) &&
        CanonicalRoot.Rotation == Math::Quaternionf::Identity(),
        "zero-length authored rotation canonicalizes to identity");
    FTransform TinyRotation;
    TinyRotation.Rotation = Math::Quaternionf(1.0e-20f, 0.0f, 0.0f, 0.0f);
    Failures += Expect(World.SetLocalTransform(Root, TinyRotation) ==
            ETransformUpdateResult::Updated &&
        World.TryGetLocalTransform(Root, CanonicalRoot) &&
        CanonicalRoot.Rotation == Math::Quaternionf(1.0f, 0.0f, 0.0f, 0.0f),
        "every nonzero authored rotation is normalized without underflow fallback");
    Failures += Expect(World.SetLocalTransform(Root, RootLocal) ==
        ETransformUpdateResult::Updated,
        "Transform test restores the authored root local value");

    const uint64 ParentVersion = World.GetTransformMutationVersion();
    Failures += Expect(World.SetTransformParent(Middle, Root) ==
            ETransformParentResult::Reparented &&
        World.SetTransformParent(Leaf, Middle) == ETransformParentResult::Reparented &&
        World.GetTransformMutationVersion() == ParentVersion + 2,
        "Transform parent links author one version each");
    Math::Matrix4f LeafWorld;
    Failures += Expect(World.TryGetWorldMatrix(Leaf, LeafWorld) &&
        HasTranslation(LeafWorld, 1.0f, 2.0f, 3.0f),
        "iterative parent-first update composes a three-level hierarchy");
    const uint64 ResolvedVersion = World.GetTransformMutationVersion();
    Failures += Expect(World.UpdateTransforms() &&
        World.GetTransformMutationVersion() == ResolvedVersion,
        "derived Transform cache resolution does not author a mutation version");

    RootLocal.Translation = Math::Vector3f(5.0f, 0.0f, 0.0f);
    Failures += Expect(World.SetLocalTransform(Root, RootLocal) ==
            ETransformUpdateResult::Updated &&
        World.GetTransformMutationVersion() == ResolvedVersion + 1 &&
        World.TryGetWorldMatrix(Leaf, LeafWorld) &&
        HasTranslation(LeafWorld, 5.0f, 2.0f, 3.0f),
        "local mutation dirties and recomputes every descendant");

    const uint64 NoOpVersion = World.GetTransformMutationVersion();
    const Math::Matrix4f BeforeRejectedCycle = LeafWorld;
    Failures += Expect(World.SetLocalTransform(Root, RootLocal) ==
            ETransformUpdateResult::Unchanged &&
        World.SetTransformParent(Leaf, Middle) == ETransformParentResult::Unchanged &&
        World.SetTransformParent(Root, Leaf) == ETransformParentResult::CycleRejected &&
        World.GetTransformMutationVersion() == NoOpVersion &&
        World.TryGetWorldMatrix(Leaf, LeafWorld) &&
        MatrixNearlyEqual(LeafWorld, BeforeRejectedCycle),
        "no-op and rejected cycle preserve links cache and mutation version");

    Failures += Expect(World.SetTransformParent(Leaf, Root) ==
            ETransformParentResult::Reparented &&
        World.TryGetLocalTransform(Leaf, LeafLocal) &&
        LeafLocal.Translation == Math::Vector3f(0.0f, 0.0f, 3.0f) &&
        World.TryGetWorldMatrix(Leaf, LeafWorld) &&
        HasTranslation(LeafWorld, 5.0f, 0.0f, 3.0f),
        "reparent has explicit KeepLocal semantics");
    Failures += Expect(World.ClearTransformParent(Leaf) ==
            ETransformParentResult::Reparented &&
        World.TryGetWorldMatrix(Leaf, LeafWorld) &&
        HasTranslation(LeafWorld, 0.0f, 0.0f, 3.0f),
        "clearing a parent keeps local state and recomputes world state");
    return Failures;
}

int RunTransformValidationAndDestroyTests()
{
    int Failures = 0;
    FWorld World;
    FEntity Parent;
    FEntity ChildA;
    FEntity ChildB;
    FEntity Grandchild;
    FEntity Missing;
    World.TryCreateEntity(Parent);
    World.TryCreateEntity(ChildA);
    World.TryCreateEntity(ChildB);
    World.TryCreateEntity(Grandchild);
    World.TryCreateEntity(Missing);

    FTransform ParentLocal;
    ParentLocal.Translation = Math::Vector3f(10.0f, 0.0f, 0.0f);
    FTransform ChildALocal;
    ChildALocal.Translation = Math::Vector3f(1.0f, 0.0f, 0.0f);
    FTransform ChildBLocal;
    ChildBLocal.Translation = Math::Vector3f(0.0f, 2.0f, 0.0f);
    FTransform GrandchildLocal;
    GrandchildLocal.Translation = Math::Vector3f(0.0f, 0.0f, 3.0f);
    World.TryAddTransform(Parent, ParentLocal);
    World.TryAddTransform(ChildA, ChildALocal);
    World.TryAddTransform(ChildB, ChildBLocal);
    World.TryAddTransform(Grandchild, GrandchildLocal);
    World.SetTransformParent(ChildA, Parent);
    World.SetTransformParent(ChildB, Parent);
    World.SetTransformParent(Grandchild, ChildA);

    FWorld ForeignWorld;
    FEntity Foreign;
    ForeignWorld.TryCreateEntity(Foreign);
    ForeignWorld.TryAddTransform(Foreign);
    FEntity Forged = ChildA;
    ++Forged.Slot.Generation;
    const uint64 ValidationVersion = World.GetTransformMutationVersion();
    Math::Matrix4f Preserved(7.0f);
    FTransform PreservedLocal;
    PreservedLocal.Translation = Math::Vector3f(7.0f);
    FEntity PreservedParent{FEntitySlotHandle{7, 7}, 7};
    Failures += Expect(World.SetTransformParent(ChildA, Missing) ==
            ETransformParentResult::MissingParentTransform &&
        World.SetTransformParent(ChildA, Foreign) == ETransformParentResult::InvalidEntity &&
        World.SetTransformParent(Forged, Parent) == ETransformParentResult::InvalidEntity &&
        !World.TryGetWorldMatrix(Foreign, Preserved) && Preserved == Math::Matrix4f(7.0f) &&
        !World.TryGetLocalTransform(Missing, PreservedLocal) &&
        PreservedLocal.Translation == Math::Vector3f(7.0f) &&
        !World.TryGetTransformParent(Missing, PreservedParent) &&
        PreservedParent == FEntity{FEntitySlotHandle{7, 7}, 7} &&
        World.GetTransformMutationVersion() == ValidationVersion,
        "failed Transform queries and mutations preserve every output and version");

    const uint64 DestroyVersion = World.GetTransformMutationVersion();
    Failures += Expect(World.DestroyEntity(Parent) &&
        World.GetTransformMutationVersion() == DestroyVersion + 1,
        "destroying a parent performs one top-level authored Transform mutation");
    FEntity ChildAParent{FEntitySlotHandle{9, 9}, 9};
    FEntity ChildBParent{FEntitySlotHandle{9, 9}, 9};
    Math::Matrix4f ChildAWorld;
    Math::Matrix4f ChildBWorld;
    Math::Matrix4f GrandchildWorld;
    Failures += Expect(World.TryGetTransformParent(ChildA, ChildAParent) &&
        World.TryGetTransformParent(ChildB, ChildBParent) &&
        !ChildAParent.IsValid() && !ChildBParent.IsValid() &&
        World.TryGetWorldMatrix(ChildA, ChildAWorld) &&
        World.TryGetWorldMatrix(ChildB, ChildBWorld) &&
        World.TryGetWorldMatrix(Grandchild, GrandchildWorld) &&
        HasTranslation(ChildAWorld, 1.0f, 0.0f, 0.0f) &&
        HasTranslation(ChildBWorld, 0.0f, 2.0f, 0.0f) &&
        HasTranslation(GrandchildWorld, 1.0f, 0.0f, 3.0f),
        "parent destroy roots direct children with KeepLocal and preserves descendant links");

    FEntity Reused;
    World.TryCreateEntity(Reused);
    Failures += Expect(Reused.GetIndex() == Parent.GetIndex() &&
        Reused.GetGeneration() != Parent.GetGeneration() && !World.HasTransform(Reused) &&
        World.SetTransformParent(ChildA, Reused) ==
            ETransformParentResult::MissingParentTransform,
        "slot reuse cannot inherit stale Transform hierarchy links");

    const uint64 RemoveVersion = World.GetTransformMutationVersion();
    Failures += Expect(World.RemoveTransform(ChildA) == EComponentRemoveResult::Removed &&
        World.GetTransformMutationVersion() == RemoveVersion + 1 &&
        !World.HasTransform(ChildA) && World.HasTransform(ChildB) &&
        World.HasTransform(Grandchild),
        "Transform swap-remove keeps other Entity-handle hierarchy links valid");
    FEntity GrandchildParent{FEntitySlotHandle{8, 8}, 8};
    Failures += Expect(World.TryGetTransformParent(Grandchild, GrandchildParent) &&
        !GrandchildParent.IsValid(),
        "removing a Transform roots its direct children before dense swap-remove");

    FWorld ReentrantWorld;
    FEntity ReentrantEntity;
    ReentrantWorld.TryCreateEntity(ReentrantEntity);
    ReentrantWorld.TryAddTransform(ReentrantEntity);
    TComponentStorage<FQueryWorldOnDestroyComponent> QueryStorage;
    ReentrantWorld.AttachComponentStorage(QueryStorage);
    Math::Matrix4f ReentrantOutput(6.0f);
    bool bReentrantQueryResult = true;
    FQueryWorldOnDestroyComponent QueryComponent;
    QueryComponent.World = &ReentrantWorld;
    QueryComponent.Entity = ReentrantEntity;
    QueryComponent.Output = &ReentrantOutput;
    QueryComponent.QueryResult = &bReentrantQueryResult;
    QueryComponent.bArmed = true;
    ReentrantWorld.TryAddComponent(QueryStorage, ReentrantEntity, std::move(QueryComponent));
    Failures += Expect(ReentrantWorld.DestroyEntity(ReentrantEntity) &&
        !bReentrantQueryResult && ReentrantOutput == Math::Matrix4f(6.0f),
        "world-matrix query rejects structural reentry and preserves its output");
    return Failures;
}

int RunTransformDepthOomAndExtractionTests()
{
    int Failures = 0;
    FWorld DeepWorld;
    constexpr std::size_t Depth = 4096;
    Array<FEntity> Chain;
    Chain.Reserve(Depth);
    bool bDeepBuilt = true;
    for (std::size_t Index = 0; Index < Depth; ++Index)
    {
        FEntity Entity;
        bDeepBuilt = bDeepBuilt && DeepWorld.TryCreateEntity(Entity) &&
            DeepWorld.TryAddTransform(Entity) == EComponentMutationResult::Added &&
            Chain.TryPushBack(Entity);
        if (Index != 0)
        {
            bDeepBuilt = bDeepBuilt && DeepWorld.SetTransformParent(Entity, Chain[Index - 1]) ==
                ETransformParentResult::Reparented;
        }
    }
    FTransform DeepRoot;
    DeepRoot.Translation = Math::Vector3f(2.0f, 0.0f, 0.0f);
    Math::Matrix4f DeepLeafWorld;
    Failures += Expect(bDeepBuilt &&
        DeepWorld.SetLocalTransform(Chain[0], DeepRoot) == ETransformUpdateResult::Updated &&
        DeepWorld.TryGetWorldMatrix(Chain[Depth - 1], DeepLeafWorld) &&
        HasTranslation(DeepLeafWorld, 2.0f, 0.0f, 0.0f),
        "4096-deep dirty propagation and update are iterative rather than recursive");

    FWorldFailingAllocator TransformAllocator;
    FWorld OomWorld(TransformAllocator);
    FEntity OomEntity;
    OomWorld.TryCreateEntity(OomEntity);
    const uint64 OomVersion = OomWorld.GetTransformMutationVersion();
    TransformAllocator.bFail = true;
    Failures += Expect(OomWorld.TryAddTransform(OomEntity) ==
            EComponentMutationResult::AllocationFailed &&
        !OomWorld.HasTransform(OomEntity) &&
        OomWorld.GetTransformMutationVersion() == OomVersion,
        "Transform Add OOM preserves hierarchy and authored mutation version");
    TransformAllocator.bFail = false;
    Failures += Expect(OomWorld.TryAddTransform(OomEntity) ==
            EComponentMutationResult::Added && OomWorld.HasTransform(OomEntity),
        "Transform Add succeeds on the first retry after OOM");

    FWorld SceneWorld;
    TComponentStorage<FWorldMeshComponent>* const Meshes = SceneWorld.GetMeshComponents();
    FEntity TransformedMesh;
    FEntity IdentityMesh;
    SceneWorld.TryCreateEntity(TransformedMesh);
    SceneWorld.TryCreateEntity(IdentityMesh);
    FTransform MeshTransform;
    MeshTransform.Translation = Math::Vector3f(4.0f, 5.0f, 6.0f);
    SceneWorld.TryAddTransform(TransformedMesh, MeshTransform);
    SceneWorld.TryAddComponent(*Meshes, TransformedMesh, FWorldMeshComponent{});
    SceneWorld.TryAddComponent(*Meshes, IdentityMesh, FWorldMeshComponent{});
    FRenderScene RenderScene;
    FWorldRenderSceneExtractor::ExtractRenderScene(SceneWorld, RenderScene);
    Failures += Expect(RenderScene.Meshes.Size() == 2 &&
        HasTranslation(RenderScene.Meshes[0].LocalToWorld, 4.0f, 5.0f, 6.0f) &&
        RenderScene.Meshes[1].LocalToWorld == Math::Matrix4f::Identity(),
        "render extraction consumes Transform world matrices with identity fallback");
    return Failures;
}

int RunWorldTickBoundaryTests()
{
    int Failures = 0;
    FWorld World;
    FWorldCommandBuffer* const Commands = World.GetCommandBuffer();
    FEntity Entity;
    World.TryCreateEntity(Entity);
    World.TryAddTransform(Entity);

    FTransform One;
    One.Translation = Math::Vector3f(1.0f, 0.0f, 0.0f);
    Failures += Expect(Commands != nullptr &&
        Commands->TrySetLocalTransform(Entity, One) == EWorldCommandRecordResult::Recorded &&
        Commands->GetPendingCommandCount() == 1,
        "World command buffer records idle work for the next Begin boundary");

    FWorldCommandFlushResult PreservedFlush;
    PreservedFlush.ExecutedCount = 99;
    FWorldTickContext InvalidContext;
    InvalidContext.DeltaSeconds = -1.0;
    Failures += Expect(World.BeginTick(InvalidContext, &PreservedFlush) ==
            EWorldTickResult::InvalidContext &&
        PreservedFlush.ExecutedCount == 99 && Commands->GetPendingCommandCount() == 1 &&
        World.GetTickState() == EWorldTickState::Idle,
        "invalid Begin context preserves queue, output, context, and tick state");

    FWorldTickContext Context;
    Context.FrameIndex = 7;
    Context.DeltaSeconds = 0.25;
    Context.ElapsedSeconds = 3.0;
    FWorldCommandFlushResult BeginFlush;
    FTransform Local;
    Failures += Expect(World.BeginTick(Context, &BeginFlush) == EWorldTickResult::Success &&
        BeginFlush.ExecutedCount == 1 && BeginFlush.SucceededCount == 1 &&
        World.GetTickState() == EWorldTickState::Updating &&
        World.TryGetLocalTransform(Entity, Local) && Local == One,
        "Begin flushes idle FIFO before exposing the Updating snapshot");
    FWorldCommandFlushResult NestedFlush;
    NestedFlush.ExecutedCount = 77;
    Failures += Expect(World.BeginTick(Context, &NestedFlush) ==
            EWorldTickResult::InvalidState && NestedFlush.ExecutedCount == 77 &&
        World.GetTickState() == EWorldTickState::Updating,
        "nested Begin rejects without altering flush output or accepted state");

    FEntity PreservedEntity{FEntitySlotHandle{9, 9}, 9};
    TComponentStorage<FIntComponent> LateStorage;
    Failures += Expect(!World.TryCreateEntity(PreservedEntity) &&
        PreservedEntity == FEntity{FEntitySlotHandle{9, 9}, 9} &&
        !World.AttachComponentStorage(LateStorage) && !World.Reset(),
        "immediate structural mutation and storage attachment reject Updating");

    FTransform Two;
    Two.Translation = Math::Vector3f(2.0f, 0.0f, 0.0f);
    Failures += Expect(Commands->TrySetLocalTransform(Entity, Two) ==
            EWorldCommandRecordResult::Recorded &&
        World.TryGetLocalTransform(Entity, Local) && Local == One,
        "commands recorded during Updating remain invisible until End");
    FWorldCommandFlushResult EndFlush;
    Failures += Expect(World.EndTick(&EndFlush) == EWorldTickResult::Success &&
        EndFlush.ExecutedCount == 1 && World.GetTickState() == EWorldTickState::Idle &&
        World.TryGetLocalTransform(Entity, Local) && Local == Two,
        "End flushes Updating commands into the same frame before Render");
    NestedFlush.ExecutedCount = 78;
    Failures += Expect(World.EndTick(&NestedFlush) == EWorldTickResult::InvalidState &&
        NestedFlush.ExecutedCount == 78,
        "an unmatched End rejects without altering its flush output");

    FTransform Three;
    Three.Translation = Math::Vector3f(3.0f, 0.0f, 0.0f);
    Commands->TrySetLocalTransform(Entity, Three);
    Failures += Expect(World.TryGetLocalTransform(Entity, Local) && Local == Two,
        "commands recorded after End stay invisible during the idle Render interval");
    Context.FrameIndex = 8;
    Context.DeltaSeconds = 0.5;
    Context.ElapsedSeconds = 3.5;
    Failures += Expect(World.BeginTick(Context) == EWorldTickResult::Success &&
        World.TryGetLocalTransform(Entity, Local) && Local == Three &&
        World.EndTick() == EWorldTickResult::Success,
        "the next Begin publishes commands recorded after the previous End");

    FWorldTickContext Accepted;
    Failures += Expect(World.TryGetTickContext(Accepted) && Accepted.FrameIndex == 8 &&
        Accepted.DeltaSeconds == 0.5 && Accepted.ElapsedSeconds == 3.5,
        "World retains only the last successfully accepted tick context");
    Context.FrameIndex = 8;
    Context.ElapsedSeconds = 4.0;
    Failures += Expect(World.BeginTick(Context) == EWorldTickResult::InvalidContext &&
        World.TryGetTickContext(Accepted) && Accepted.FrameIndex == 8,
        "subsequent Begin requires a strictly increasing frame and nondecreasing elapsed time");
    Accepted = FWorldTickContext{99, 99.0, 99.0};
    Failures += Expect(World.Reset() && !World.TryGetTickContext(Accepted) &&
        Accepted.FrameIndex == 99 && Accepted.DeltaSeconds == 99.0 &&
        Accepted.ElapsedSeconds == 99.0,
        "full World Reset forgets the accepted tick context and preserves failed query output");
    Context = FWorldTickContext{1, 0.0, 0.0};
    Failures += Expect(World.BeginTick(Context) == EWorldTickResult::Success &&
        World.EndTick() == EWorldTickResult::Success,
        "the first Begin after Reset may establish a fresh arbitrary frame timeline");
    return Failures;
}

int RunDeferredCommandFifoAndTokenTests()
{
    int Failures = 0;
    FWorld World;
    TComponentStorage<FIntComponent> Values;
    World.AttachComponentStorage(Values);
    FWorldCommandBuffer* const Commands = World.GetCommandBuffer();
    FDeferredEntityToken Token;
    FEntity Preserved{FEntitySlotHandle{6, 6}, 6};
    Failures += Expect(Commands->TryCreateEntity(Token) == EWorldCommandRecordResult::Recorded &&
        Commands->TryResolveDeferredEntity(Token, Preserved) ==
            EDeferredEntityResolveResult::Pending &&
        Preserved == FEntity{FEntitySlotHandle{6, 6}, 6} &&
        Commands->TryAddComponent(Values, Token, FIntComponent{1}) ==
            EWorldCommandRecordResult::Recorded &&
        Commands->TrySetComponent(Values, Token, FIntComponent{2}) ==
            EWorldCommandRecordResult::Recorded &&
        Commands->TryRemoveComponent(Values, Token) == EWorldCommandRecordResult::Recorded &&
        Commands->TryAddTransform(Token) == EWorldCommandRecordResult::Recorded,
        "deferred Create token records dependent generic and Transform commands");

    FWorldTickContext Context{10, 0.1, 1.0};
    FWorldCommandFlushResult Flush;
    Failures += Expect(World.BeginTick(Context, &Flush) == EWorldTickResult::Success &&
        Flush.ExecutedCount == 5 && Flush.SucceededCount == 5 &&
        Commands->TryResolveDeferredEntity(Token, Preserved) ==
            EDeferredEntityResolveResult::Resolved &&
        World.IsAlive(Preserved) && World.GetComponent(Values, Preserved) == nullptr &&
        World.HasTransform(Preserved) &&
        Commands->TryResolveDeferredEntity(Token, Preserved) ==
            EDeferredEntityResolveResult::InvalidToken,
        "FIFO executes Create/Add/Set/Remove/Transform and token resolution consumes once");
    World.EndTick();

    FDeferredEntityToken CreatedThenDestroyed;
    Commands->TryCreateEntity(CreatedThenDestroyed);
    Commands->TryDestroyEntity(CreatedThenDestroyed);
    Context.FrameIndex = 11;
    Context.ElapsedSeconds = 1.1;
    Failures += Expect(World.BeginTick(Context, &Flush) == EWorldTickResult::Success &&
        Flush.ExecutedCount == 2 &&
        Commands->TryAddTransform(CreatedThenDestroyed) ==
            EWorldCommandRecordResult::InvalidToken &&
        Commands->TryResolveDeferredEntity(CreatedThenDestroyed, Preserved) ==
            EDeferredEntityResolveResult::NoLongerAlive,
        "a Created token whose FIFO suffix destroyed the Entity rejects later records and resolves dead");
    World.EndTick();

    FDeferredEntityToken Independent;
    Commands->TryDestroyEntity(Preserved);
    Commands->TryDestroyEntity(Preserved);
    Commands->TryCreateEntity(Independent);
    Context.FrameIndex = 12;
    Context.ElapsedSeconds = 1.2;
    Failures += Expect(World.BeginTick(Context, &Flush) ==
            EWorldTickResult::CompletedWithCommandFailures &&
        Flush.ExecutedCount == 3 && Flush.SucceededCount == 2 && Flush.FailedCount == 1 &&
        Flush.FirstFailureIndex == 1 &&
        Flush.FirstFailureReason == EWorldCommandFailureReason::InvalidEntity &&
        Commands->TryResolveDeferredEntity(Independent, Preserved) ==
            EDeferredEntityResolveResult::Resolved && World.IsAlive(Preserved),
        "flush keeps its successful prefix, reports first FIFO failure, and continues an independent suffix");
    Failures += Expect(World.GetTickState() == EWorldTickState::Updating &&
        World.EndTick() == EWorldTickResult::Success,
        "a Begin with command failures still enters Updating and requires End");
    return Failures;
}

int RunDeferredCommandOomAndReentryTests()
{
    int Failures = 0;
    FWorldFailingAllocator Allocator;
    FWorld World(Allocator);
    FWorldCommandBuffer* const Commands = World.GetCommandBuffer();
    TComponentStorage<FMoveOnlyComponent> MoveValues;
    World.AttachComponentStorage(MoveValues);
    FEntity Entities[4];
    for (FEntity& Entity : Entities) World.TryCreateEntity(Entity);
    World.TryAddTransform(Entities[0]);

    FMoveOnlyComponent Value(44);
    const std::size_t PendingBeforeOom = Commands->GetPendingCommandCount();
    Allocator.bFail = true;
    Failures += Expect(Commands->TryAddComponent(MoveValues, Entities[0], std::move(Value)) ==
            EWorldCommandRecordResult::AllocationFailed &&
        Value.Value == 44 && Commands->GetPendingCommandCount() == PendingBeforeOom,
        "record OOM preserves a move-only rvalue and the logical queue");
    Allocator.bFail = false;
    World.TryAddComponent(MoveValues, Entities[0], FMoveOnlyComponent{45});
    FMoveOnlyComponent* const Aliased = World.GetComponent(MoveValues, Entities[0]);
    Failures += Expect(Aliased != nullptr &&
        Commands->TrySetComponent(MoveValues, Entities[0], std::move(*Aliased)) ==
            EWorldCommandRecordResult::AliasedPayload &&
        Aliased->Value == 45 && Commands->GetPendingCommandCount() == PendingBeforeOom,
        "record rejects a storage-alias rvalue before it can mutate the committed snapshot");
    Allocator.bFail = true;
    FDeferredEntityToken PreservedToken;
    Failures += Expect(Commands->TryCreateEntity(PreservedToken) ==
            EWorldCommandRecordResult::AllocationFailed && !PreservedToken.IsValid(),
        "Create record OOM preserves its output and does not issue a token");
    Allocator.bFail = false;

    FDeferredEntityToken FailedCreate;
    FTransform Changed;
    Changed.Translation = Math::Vector3f(5.0f, 0.0f, 0.0f);
    Commands->TryCreateEntity(FailedCreate);
    Commands->TryAddTransform(FailedCreate);
    Commands->TrySetLocalTransform(Entities[0], Changed);
    Allocator.bFail = true;
    FWorldTickContext Context{20, 0.1, 2.0};
    FWorldCommandFlushResult Flush;
    FTransform Local;
    FEntity Preserved{FEntitySlotHandle{8, 8}, 8};
    Failures += Expect(World.BeginTick(Context, &Flush) ==
            EWorldTickResult::CompletedWithCommandFailures &&
        Flush.ExecutedCount == 3 && Flush.SucceededCount == 1 && Flush.FailedCount == 2 &&
        Flush.FirstFailureIndex == 0 &&
        Flush.FirstFailureReason == EWorldCommandFailureReason::AllocationFailed &&
        Commands->TryResolveDeferredEntity(FailedCreate, Preserved) ==
            EDeferredEntityResolveResult::CreationFailed &&
        Preserved == FEntity{FEntitySlotHandle{8, 8}, 8} &&
        World.TryGetLocalTransform(Entities[0], Local) && Local == Changed,
        "flush OOM commits an independent suffix and makes Create dependency failure observable");
    Failures += Expect(World.EndTick() == EWorldTickResult::Success,
        "End closes a failed Begin even while the allocator remains exhausted");
    Allocator.bFail = false;

    TComponentStorage<FDeferredReentryComponent> ReentryValues;
    World.AttachComponentStorage(ReentryValues);
    FDeferredReentryComponent Existing;
    World.TryAddComponent(ReentryValues, Entities[0], std::move(Existing));
    FDeferredEntityToken ReentryToken;
    Commands->TryCreateEntity(ReentryToken);
    EWorldCommandRecordResult NestedRecord = EWorldCommandRecordResult::Recorded;
    EDeferredEntityResolveResult NestedResolve = EDeferredEntityResolveResult::Resolved;
    bool bImmediateDestroy = true;
    FDeferredReentryComponent Reentrant;
    Reentrant.World = &World;
    Reentrant.Commands = Commands;
    Reentrant.Entity = Entities[0];
    Reentrant.Token = ReentryToken;
    Reentrant.RecordResult = &NestedRecord;
    Reentrant.ResolveResult = &NestedResolve;
    Reentrant.ImmediateResult = &bImmediateDestroy;
    Reentrant.bArmed = true;
    Commands->TryAddComponent(ReentryValues, Entities[0], std::move(Reentrant));
    Context.FrameIndex = 21;
    Context.ElapsedSeconds = 2.1;
    Failures += Expect(World.BeginTick(Context, &Flush) ==
            EWorldTickResult::CompletedWithCommandFailures &&
        NestedRecord == EWorldCommandRecordResult::InvalidState &&
        NestedResolve == EDeferredEntityResolveResult::InvalidState && !bImmediateDestroy &&
        World.IsAlive(Entities[0]),
        "payload destruction cannot enqueue, consume a token, or mutate World during flush");
    FEntity Created;
    Failures += Expect(Commands->TryResolveDeferredEntity(ReentryToken, Created) ==
            EDeferredEntityResolveResult::Resolved && World.IsAlive(Created) &&
        World.EndTick() == EWorldTickResult::Success,
        "reentry rejection preserves the created token for normal post-flush resolution");
    return Failures;
}

int RunDeferredCommandCancellationAndHierarchyTests()
{
    int Failures = 0;
    FWorldFailingAllocator Allocator;
    FDeferredEntityToken StaleToken;
    FMoveOnlyComponent::ResetCounts();
    TComponentStorage<FMoveOnlyComponent> Values;
    {
        FWorld World(Allocator);
        World.AttachComponentStorage(Values);
        FEntity Entity;
        World.TryCreateEntity(Entity);
        FWorldCommandBuffer* const Commands = World.GetCommandBuffer();
        Commands->TryCreateEntity(StaleToken);
        {
            FMoveOnlyComponent Payload(31);
            Commands->TryAddComponent(Values, Entity, std::move(Payload));
        }
        const int DestructionsBeforeReset = FMoveOnlyComponent::DestructionCount;
        Failures += Expect(Commands->GetPendingCommandCount() == 2 && World.Reset() &&
            Commands->GetPendingCommandCount() == 0 && FMoveOnlyComponent::LiveCount == 0 &&
            FMoveOnlyComponent::DestructionCount == DestructionsBeforeReset + 1,
            "Reset cancels one queued move-only payload exactly once before clearing World");
        FEntity Preserved{FEntitySlotHandle{4, 4}, 4};
        Failures += Expect(Commands->TryResolveDeferredEntity(StaleToken, Preserved) ==
                EDeferredEntityResolveResult::InvalidToken &&
            Preserved == FEntity{FEntitySlotHandle{4, 4}, 4},
            "Reset generation-invalidates every pending deferred Entity token");

        World.TryCreateEntity(Entity);
        {
            FMoveOnlyComponent Payload(32);
            Commands->TryAddComponent(Values, Entity, std::move(Payload));
        }
        Failures += Expect(Commands->GetPendingCommandCount() == 1,
            "World owns a queued move-only payload until destruction");
    }
    Failures += Expect(FMoveOnlyComponent::LiveCount == 0 &&
        FMoveOnlyComponent::ConstructionCount == FMoveOnlyComponent::DestructionCount &&
        Allocator.LiveAllocations == 0,
        "World destruction cancels queued payloads exactly once and returns command memory");

    FWorld HierarchyWorld;
    FEntity Parent;
    FEntity Child;
    HierarchyWorld.TryCreateEntity(Parent);
    HierarchyWorld.TryCreateEntity(Child);
    HierarchyWorld.TryAddTransform(Parent);
    HierarchyWorld.TryAddTransform(Child);
    FWorldCommandBuffer* const Commands = HierarchyWorld.GetCommandBuffer();
    FTransform Changed;
    Changed.Translation = Math::Vector3f(9.0f, 0.0f, 0.0f);
    Commands->TrySetTransformParent(Child, Parent);
    Commands->TrySetTransformParent(Parent, Child);
    Commands->TrySetLocalTransform(Parent, Changed);
    FWorldTickContext Context{30, 0.1, 3.0};
    FWorldCommandFlushResult Flush;
    FTransform Local;
    Failures += Expect(HierarchyWorld.BeginTick(Context, &Flush) ==
            EWorldTickResult::CompletedWithCommandFailures &&
        Flush.ExecutedCount == 3 && Flush.SucceededCount == 2 && Flush.FailedCount == 1 &&
        Flush.FirstFailureIndex == 1 &&
        Flush.FirstFailureReason == EWorldCommandFailureReason::CycleRejected &&
        HierarchyWorld.TryGetLocalTransform(Parent, Local) && Local == Changed,
        "deferred cycle rejection reports its FIFO index and continues an independent Transform suffix");
    Failures += Expect(HierarchyWorld.EndTick() == EWorldTickResult::Success,
        "hierarchy command failure still leaves an accepted tick closable");
    return Failures;
}

struct FNativeGameplayProbe
{
    int Id = 0;
    int* Order = nullptr;
    std::size_t* OrderCount = nullptr;
    FNativeGameplaySystemUpdateContext LastContext;
    uint32 CallCount = 0;
    ENativeGameplaySystemResult Result = ENativeGameplaySystemResult::Success;
    FEntity Entity;
    FTransform TransformToRecord;
    bool bRecordTransform = false;
    bool bObservedOldTransform = false;
    bool bTryReentry = false;
    ENativeGameplaySystemRegisterResult ReentryRegister =
        ENativeGameplaySystemRegisterResult::Success;
    ENativeGameplayFixedStepConfigResult ReentryConfig =
        ENativeGameplayFixedStepConfigResult::Success;
    ENativeGameplayScheduleResult ReentryCompile = ENativeGameplayScheduleResult::Success;
    ENativeGameplayUpdateResult ReentryUpdate = ENativeGameplayUpdateResult::Success;
    bool bImmediateDestroy = true;
};

ENativeGameplaySystemResult ProbeNativeGameplaySystem(
    void* const ProviderContext,
    const FNativeGameplaySystemUpdateContext& Context) noexcept
{
    FNativeGameplayProbe& Probe = *static_cast<FNativeGameplayProbe*>(ProviderContext);
    Probe.LastContext = Context;
    ++Probe.CallCount;
    if (Probe.Order != nullptr && Probe.OrderCount != nullptr)
    {
        Probe.Order[(*Probe.OrderCount)++] = Probe.Id;
    }
    if (Probe.bRecordTransform)
    {
        FTransform Before;
        Probe.bObservedOldTransform = Context.World->TryGetLocalTransform(Probe.Entity, Before) &&
            Before.Translation[0] != Probe.TransformToRecord.Translation[0] &&
            Context.Commands->TrySetLocalTransform(Probe.Entity, Probe.TransformToRecord) ==
                EWorldCommandRecordResult::Recorded;
    }
    if (Probe.bTryReentry)
    {
        FNativeGameplaySystemDescriptor Descriptor;
        Descriptor.Name = "Nested.System";
        Descriptor.Update = &ProbeNativeGameplaySystem;
        Descriptor.ProviderContext = &Probe;
        Probe.ReentryRegister = Context.World->RegisterNativeGameplaySystem(Descriptor);
        Probe.ReentryConfig = Context.World->ConfigureNativeGameplayFixedStep({0.1, 4});
        Probe.ReentryCompile = Context.World->CompileNativeGameplaySystemSchedule();
        Probe.ReentryUpdate = Context.World->UpdateNativeGameplaySystems();
        Probe.bImmediateDestroy = Context.World->DestroyEntity(Probe.Entity);
    }
    return Probe.Result;
}

FNativeGameplaySystemDescriptor MakeNativeGameplayDescriptor(
    const StringView Name,
    FNativeGameplayProbe& Probe,
    const ENativeGameplaySystemPhase Phase,
    const Span<const StringView> Dependencies = {}) noexcept
{
    FNativeGameplaySystemDescriptor Descriptor;
    Descriptor.Name = Name;
    Descriptor.Dependencies = Dependencies;
    Descriptor.ProviderContext = &Probe;
    Descriptor.Update = &ProbeNativeGameplaySystem;
    Descriptor.Phase = Phase;
    return Descriptor;
}

int RunNativeGameplayGraphTests()
{
    int Failures = 0;
    FWorld World;
    FNativeGameplayProbe ProbeA;
    FNativeGameplayProbe ProbeB;
    FNativeGameplayProbe ProbeC;
    FNativeGameplayProbe ProbeVariable;
    FNativeGameplaySystemDescriptor NullUpdate;
    NullUpdate.Name = "Invalid.NullUpdate";
    Failures += Expect(World.RegisterNativeGameplaySystem(NullUpdate) ==
            ENativeGameplaySystemRegisterResult::InvalidDescriptor,
        "native gameplay rejects a null update thunk");
    FNativeGameplaySystemDescriptor Invalid;
    Invalid.Name = "Invalid Name";
    Invalid.Update = &ProbeNativeGameplaySystem;
    Failures += Expect(World.RegisterNativeGameplaySystem(Invalid) ==
            ENativeGameplaySystemRegisterResult::InvalidDescriptor,
        "native gameplay rejects malformed system names");
    StringView DuplicateDependencies[] = {"Graph.A", "Graph.A"};
    Invalid = MakeNativeGameplayDescriptor(
        "Graph.Bad", ProbeA, ENativeGameplaySystemPhase::Fixed, DuplicateDependencies);
    Failures += Expect(World.RegisterNativeGameplaySystem(Invalid) ==
            ENativeGameplaySystemRegisterResult::InvalidDescriptor,
        "native gameplay rejects duplicate dependency declarations");

    int Order[16] = {};
    std::size_t OrderCount = 0;
    ProbeA.Id = 1;
    ProbeA.Order = Order;
    ProbeA.OrderCount = &OrderCount;
    ProbeB.Id = 2;
    ProbeB.Order = Order;
    ProbeB.OrderCount = &OrderCount;
    ProbeC.Id = 3;
    ProbeC.Order = Order;
    ProbeC.OrderCount = &OrderCount;
    ProbeVariable.Id = 4;
    ProbeVariable.Order = Order;
    ProbeVariable.OrderCount = &OrderCount;
    StringView CDependencies[] = {"Graph.A"};
    Failures += Expect(World.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
            "Graph.C", ProbeC, ENativeGameplaySystemPhase::Fixed, CDependencies)) ==
            ENativeGameplaySystemRegisterResult::Success &&
        World.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
            "Graph.B", ProbeB, ENativeGameplaySystemPhase::Fixed)) ==
            ENativeGameplaySystemRegisterResult::Success &&
        World.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
            "Graph.A", ProbeA, ENativeGameplaySystemPhase::Fixed)) ==
            ENativeGameplaySystemRegisterResult::Success &&
        World.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
            "Graph.Variable", ProbeVariable, ENativeGameplaySystemPhase::Variable)) ==
            ENativeGameplaySystemRegisterResult::Success &&
        World.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
            "Graph.A", ProbeA, ENativeGameplaySystemPhase::Fixed)) ==
            ENativeGameplaySystemRegisterResult::DuplicateName &&
        World.CompileNativeGameplaySystemSchedule() == ENativeGameplayScheduleResult::Success,
        "native gameplay compiles forward dependencies and rejects duplicate identity");
    World.ConfigureNativeGameplayFixedStep({0.1, 4});
    FWorldTickContext Context{1, 0.1, 0.1};
    FNativeGameplayUpdateReport Report;
    Failures += Expect(World.BeginTick(Context) == EWorldTickResult::Success &&
        World.UpdateNativeGameplaySystems(&Report) == ENativeGameplayUpdateResult::Success &&
        OrderCount == 4 && Order[0] == 1 && Order[1] == 2 && Order[2] == 3 &&
        Order[3] == 4 &&
        Report.FixedStepsAttempted == 1 && Report.FixedStepsCompleted == 1 &&
        Report.FixedCallbacksAttempted == 3 &&
        ProbeA.LastContext.Phase == ENativeGameplaySystemPhase::Fixed &&
        ProbeA.LastContext.FrameIndex == 1 && ProbeA.LastContext.FixedTickIndex == 0 &&
        ProbeA.LastContext.FixedSubstepIndex == 0 &&
        std::fabs(ProbeA.LastContext.StepDeltaSeconds - 0.1) < 1.0e-12 &&
        std::fabs(ProbeA.LastContext.StepElapsedSeconds - 0.1) < 1.0e-12 &&
        ProbeVariable.CallCount == 1 &&
        ProbeVariable.LastContext.Phase == ENativeGameplaySystemPhase::Variable &&
        ProbeVariable.LastContext.FrameIndex == 1 &&
        std::fabs(ProbeVariable.LastContext.FrameDeltaSeconds - 0.1) < 1.0e-12 &&
        std::fabs(ProbeVariable.LastContext.FrameElapsedSeconds - 0.1) < 1.0e-12 &&
        std::fabs(ProbeVariable.LastContext.StepDeltaSeconds - 0.1) < 1.0e-12 &&
        std::fabs(ProbeVariable.LastContext.StepElapsedSeconds - 0.1) < 1.0e-12 &&
        World.EndTick() == EWorldTickResult::Success,
        "native gameplay topo, fixed-before-variable barrier, and callback contexts are deterministic");
    World.BeginTick({2, 0.0, 0.1});
    FNativeGameplayUpdateReport OnceReport;
    const ENativeGameplayUpdateResult OnceResult =
        World.UpdateNativeGameplaySystems(&OnceReport);
    const ENativeGameplayUpdateResult TwiceResult = World.UpdateNativeGameplaySystems();
    World.EndTick();
    Failures += Expect(OnceResult == ENativeGameplayUpdateResult::Success &&
        TwiceResult == ENativeGameplayUpdateResult::InvalidState &&
        ProbeVariable.CallCount == 2 && OnceReport.VariableCallbacksAttempted == 1,
        "Variable phase runs exactly once per accepted gameplay update");

    FWorld MissingWorld;
    StringView MissingDependency[] = {"Missing.B"};
    MissingWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Missing.A", ProbeA, ENativeGameplaySystemPhase::Fixed, MissingDependency));
    Failures += Expect(MissingWorld.CompileNativeGameplaySystemSchedule() ==
            ENativeGameplayScheduleResult::MissingDependency &&
        MissingWorld.GetNativeGameplayDiagnosticSystemName() == StringView("Missing.A") &&
        MissingWorld.GetNativeGameplayDiagnosticDependencyName() == StringView("Missing.B"),
        "native gameplay reports a deterministic missing dependency");

    FWorld CrossWorld;
    StringView CrossDependency[] = {"Cross.Variable"};
    CrossWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Cross.Fixed", ProbeA, ENativeGameplaySystemPhase::Fixed, CrossDependency));
    CrossWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Cross.Variable", ProbeB, ENativeGameplaySystemPhase::Variable));
    Failures += Expect(CrossWorld.CompileNativeGameplaySystemSchedule() ==
            ENativeGameplayScheduleResult::CrossPhaseDependency,
        "native gameplay rejects cross-phase dependencies");

    FWorld ReverseCrossWorld;
    StringView ReverseCrossDependency[] = {"Reverse.Fixed"};
    ReverseCrossWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Reverse.Fixed", ProbeA, ENativeGameplaySystemPhase::Fixed));
    ReverseCrossWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Reverse.Variable", ProbeB, ENativeGameplaySystemPhase::Variable,
        ReverseCrossDependency));
    Failures += Expect(ReverseCrossWorld.CompileNativeGameplaySystemSchedule() ==
            ENativeGameplayScheduleResult::CrossPhaseDependency,
        "native gameplay also rejects Variable-to-Fixed dependencies");

    FWorld CycleWorld;
    StringView DependsOnB[] = {"Cycle.B"};
    StringView DependsOnA[] = {"Cycle.A"};
    CycleWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Cycle.A", ProbeA, ENativeGameplaySystemPhase::Variable, DependsOnB));
    CycleWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Cycle.B", ProbeB, ENativeGameplaySystemPhase::Variable, DependsOnA));
    Failures += Expect(CycleWorld.CompileNativeGameplaySystemSchedule() ==
            ENativeGameplayScheduleResult::DependencyCycle &&
        CycleWorld.GetNativeGameplayDiagnosticSystemName() == StringView("Cycle.A"),
        "native gameplay reports the lexical member of a dependency cycle");

    FWorld SelfCycleWorld;
    StringView SelfDependency[] = {"Cycle.Self"};
    SelfCycleWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Cycle.Self", ProbeA, ENativeGameplaySystemPhase::Fixed, SelfDependency));
    Failures += Expect(SelfCycleWorld.CompileNativeGameplaySystemSchedule() ==
            ENativeGameplayScheduleResult::DependencyCycle,
        "native gameplay treats a self dependency as a cycle");

    FWorld CopyWorld;
    FNativeGameplayProbe CopyDependencyProbe;
    FNativeGameplayProbe CopyDependentProbe;
    char MutableName[] = "Copy.Dependent";
    char MutableDependency[] = "Copy.Dependency";
    CopyWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Copy.Dependency", CopyDependencyProbe, ENativeGameplaySystemPhase::Variable));
    StringView MutableDependencies[] = {StringView(MutableDependency)};
    CopyWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        StringView(MutableName), CopyDependentProbe, ENativeGameplaySystemPhase::Variable,
        MutableDependencies));
    MutableName[0] = 'X';
    MutableDependency[0] = 'X';
    Failures += Expect(CopyWorld.CompileNativeGameplaySystemSchedule() ==
            ENativeGameplayScheduleResult::Success,
        "native gameplay deep-copies mutable descriptor names and dependencies");
    CopyWorld.BeginTick({0, 0.0, 0.0});
    CopyWorld.UpdateNativeGameplaySystems();
    CopyWorld.EndTick();
    Failures += Expect(CopyDependencyProbe.CallCount == 1 && CopyDependentProbe.CallCount == 1,
        "deep-copied native gameplay descriptors remain executable after source mutation");
    return Failures;
}

int RunNativeGameplayTimingTests()
{
    int Failures = 0;
    FWorld World;
    Failures += Expect(World.ConfigureNativeGameplayFixedStep({0.1, 0}) ==
            ENativeGameplayFixedStepConfigResult::InvalidConfig &&
        World.ConfigureNativeGameplayFixedStep(
            {0.1, MaxNativeGameplayFixedSubsteps + 1}) ==
            ENativeGameplayFixedStepConfigResult::InvalidConfig &&
        World.ConfigureNativeGameplayFixedStep(
            {(std::numeric_limits<double>::max)(), 2}) ==
            ENativeGameplayFixedStepConfigResult::InvalidConfig &&
        World.ConfigureNativeGameplayFixedStep({0.1, 3}) ==
            ENativeGameplayFixedStepConfigResult::Success &&
        World.CompileNativeGameplaySystemSchedule() == ENativeGameplayScheduleResult::Success,
        "native gameplay validates the public fixed-step bound and finite cap");

    FNativeGameplayUpdateReport Reports[5];
    const double Deltas[] = {0.0, 0.1, 0.15, 0.05, 1.0};
    double Elapsed = 0.0;
    for (uint64 Frame = 0; Frame < 5; ++Frame)
    {
        Elapsed += Deltas[Frame];
        FWorldTickContext Context{Frame, Deltas[Frame], Elapsed};
        World.BeginTick(Context);
        World.UpdateNativeGameplaySystems(&Reports[Frame]);
        World.EndTick();
    }
    Failures += Expect(Reports[0].FixedStepsAttempted == 0 &&
        Reports[1].FixedStepsAttempted == 1 &&
        Reports[2].FixedStepsAttempted == 1 &&
        std::fabs(Reports[2].FixedAccumulatorSeconds - 0.05) < 1.0e-12 &&
        Reports[3].FixedStepsAttempted == 1 &&
        Reports[4].FixedStepsAttempted == 3 && Reports[4].FixedStepsCompleted == 3 &&
        std::fabs(Reports[4].DroppedSeconds - 0.7) < 1.0e-12 &&
        Reports[4].NextFixedTickIndex == 6 &&
        std::fabs(Reports[4].FixedSimulationSeconds - 0.6) < 1.0e-12,
        "native gameplay carries fractions, consumes empty fixed phases, and drops spiral backlog");

    FWorld OverflowWorld;
    OverflowWorld.ConfigureNativeGameplayFixedStep({0.1, 3});
    OverflowWorld.CompileNativeGameplaySystemSchedule();
    FWorldTickContext OverflowContext{0, (std::numeric_limits<double>::max)(), 0.0};
    FNativeGameplayUpdateReport OverflowReport;
    OverflowWorld.BeginTick(OverflowContext);
    OverflowWorld.UpdateNativeGameplaySystems(&OverflowReport);
    OverflowWorld.EndTick();
    Failures += Expect(std::isfinite(OverflowReport.DroppedSeconds) &&
        OverflowReport.FixedStepsAttempted == 3,
        "native gameplay saturates finite accumulator addition and keeps dropped time finite");

    FWorld DisabledWorld;
    FNativeGameplayProbe DisabledProbe;
    DisabledWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Disabled.Fixed", DisabledProbe, ENativeGameplaySystemPhase::Fixed));
    DisabledWorld.SetNativeGameplaySystemEnabled("Disabled.Fixed", false);
    DisabledWorld.ConfigureNativeGameplayFixedStep({0.1, 2});
    DisabledWorld.CompileNativeGameplaySystemSchedule();
    FNativeGameplayUpdateReport DisabledReport;
    DisabledWorld.BeginTick({0, 0.1, 0.1});
    DisabledWorld.UpdateNativeGameplaySystems(&DisabledReport);
    DisabledWorld.EndTick();
    Failures += Expect(DisabledProbe.CallCount == 0 &&
        DisabledReport.FixedStepsAttempted == 1 && DisabledReport.FixedStepsCompleted == 1 &&
        DisabledReport.FixedCallbacksAttempted == 0 &&
        DisabledReport.NextFixedTickIndex == 1,
        "disabled fixed systems skip callbacks without stopping the fixed clock");

    FWorld DisabledDependencyWorld;
    FNativeGameplayProbe DisabledDependency;
    FNativeGameplayProbe EnabledDependent;
    StringView DisabledDependencyName[] = {"Disabled.Dependency"};
    DisabledDependencyWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Disabled.Dependency", DisabledDependency, ENativeGameplaySystemPhase::Variable));
    DisabledDependencyWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Disabled.Dependent", EnabledDependent, ENativeGameplaySystemPhase::Variable,
        DisabledDependencyName));
    DisabledDependencyWorld.SetNativeGameplaySystemEnabled("Disabled.Dependency", false);
    DisabledDependencyWorld.CompileNativeGameplaySystemSchedule();
    DisabledDependencyWorld.BeginTick({0, 0.0, 0.0});
    FNativeGameplayUpdateReport DisabledDependencyReport;
    DisabledDependencyWorld.UpdateNativeGameplaySystems(&DisabledDependencyReport);
    DisabledDependencyWorld.EndTick();
    Failures += Expect(DisabledDependency.CallCount == 0 &&
        EnabledDependent.CallCount == 1 &&
        DisabledDependencyReport.VariableCallbacksAttempted == 1,
        "disabled dependency remains an order edge and does not disable its dependent");

    FWorld ExhaustedWorld;
    ExhaustedWorld.ConfigureNativeGameplayFixedStep({0.1, 2, 0});
    ExhaustedWorld.CompileNativeGameplaySystemSchedule();
    FNativeGameplayUpdateReport LastIndexReport;
    ExhaustedWorld.BeginTick({0, 0.1, 0.1});
    const ENativeGameplayUpdateResult LastIndexResult =
        ExhaustedWorld.UpdateNativeGameplaySystems(&LastIndexReport);
    ExhaustedWorld.EndTick();
    FNativeGameplayUpdateReport ExhaustedReport;
    FNativeGameplayUpdateReport ExhaustedPreserved;
    ExhaustedPreserved.FixedStepsAttempted = 55;
    ExhaustedWorld.BeginTick({1, 0.1, 0.2});
    const ENativeGameplayUpdateResult ExhaustedResult =
        ExhaustedWorld.UpdateNativeGameplaySystems(&ExhaustedReport);
    const ENativeGameplayUpdateResult RepeatedExhaustedResult =
        ExhaustedWorld.UpdateNativeGameplaySystems(&ExhaustedPreserved);
    ExhaustedWorld.EndTick();
    Failures += Expect(LastIndexResult == ENativeGameplayUpdateResult::Success &&
        LastIndexReport.FixedStepsAttempted == 1 &&
        LastIndexReport.bFixedTimelineExhausted &&
        ExhaustedResult == ENativeGameplayUpdateResult::TimelineExhausted &&
        ExhaustedReport.FixedStepsAttempted == 0 &&
        std::fabs(ExhaustedReport.FixedAccumulatorSeconds - 0.1) < 1.0e-12 &&
        RepeatedExhaustedResult == ENativeGameplayUpdateResult::InvalidState &&
        ExhaustedPreserved.FixedStepsAttempted == 55,
        "fixed timeline issues its final unique index once and then reports accepted exhaustion");
    return Failures;
}

int RunNativeGameplayFailureOomAndResetTests()
{
    int Failures = 0;
    FWorldFailingAllocator InvalidAllocator(true);
    FWorld InvalidWorld(InvalidAllocator);
    FNativeGameplayUpdateReport InvalidPreserved;
    InvalidPreserved.FixedStepsAttempted = 123;
    Failures += Expect(InvalidWorld.UpdateNativeGameplaySystems(&InvalidPreserved) ==
            ENativeGameplayUpdateResult::InvalidWorld &&
        InvalidPreserved.FixedStepsAttempted == 123,
        "invalid World preserves native gameplay update output");

    FWorld World;
    FNativeGameplayUpdateReport IdlePreserved;
    IdlePreserved.FixedStepsAttempted = 321;
    Failures += Expect(World.UpdateNativeGameplaySystems(&IdlePreserved) ==
            ENativeGameplayUpdateResult::InvalidState &&
        IdlePreserved.FixedStepsAttempted == 321,
        "Idle native gameplay update preserves its output");
    FEntity Entity;
    World.TryCreateEntity(Entity);
    World.TryAddTransform(Entity);
    int Order[8] = {};
    std::size_t OrderCount = 0;
    FNativeGameplayProbe Writer;
    Writer.Id = 1;
    Writer.Order = Order;
    Writer.OrderCount = &OrderCount;
    Writer.Entity = Entity;
    Writer.TransformToRecord.Translation = Math::Vector3f(7.0f, 0.0f, 0.0f);
    Writer.bRecordTransform = true;
    Writer.bTryReentry = true;
    FNativeGameplayProbe Failure;
    Failure.Id = 2;
    Failure.Order = Order;
    Failure.OrderCount = &OrderCount;
    Failure.Result = ENativeGameplaySystemResult::Failure;
    StringView FailureDependency[] = {"Failure.Writer"};
    World.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Failure.Writer", Writer, ENativeGameplaySystemPhase::Variable));
    World.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Failure.Stop", Failure, ENativeGameplaySystemPhase::Variable, FailureDependency));
    World.CompileNativeGameplaySystemSchedule();

    FNativeGameplayUpdateReport Report;
    FNativeGameplayUpdateReport Preserved;
    Preserved.FixedStepsAttempted = 777;
    FTransform BeforeEnd;
    FTransform AfterEnd;
    World.BeginTick({0, 0.0, 0.0});
    const ENativeGameplayUpdateResult FirstUpdate = World.UpdateNativeGameplaySystems(&Report);
    const ENativeGameplayUpdateResult SecondUpdate = World.UpdateNativeGameplaySystems(&Preserved);
    World.TryGetLocalTransform(Entity, BeforeEnd);
    const EWorldTickResult EndResult = World.EndTick();
    World.TryGetLocalTransform(Entity, AfterEnd);
    Failures += Expect(FirstUpdate == ENativeGameplayUpdateResult::SystemFailed &&
        SecondUpdate == ENativeGameplayUpdateResult::InvalidState &&
        Preserved.FixedStepsAttempted == 777 && Report.VariableCallbacksAttempted == 2 &&
        Report.FailedSystemName == StringView("Failure.Stop") &&
        Writer.bObservedOldTransform && BeforeEnd.Translation[0] == 0.0f &&
        EndResult == EWorldTickResult::Success && AfterEnd.Translation[0] == 7.0f &&
        Writer.ReentryRegister == ENativeGameplaySystemRegisterResult::InvalidState &&
        Writer.ReentryConfig == ENativeGameplayFixedStepConfigResult::InvalidState &&
        Writer.ReentryCompile == ENativeGameplayScheduleResult::InvalidState &&
        Writer.ReentryUpdate == ENativeGameplayUpdateResult::InvalidState &&
        !Writer.bImmediateDestroy && World.IsAlive(Entity),
        "native gameplay failure stops once, preserves invalid output, and End commits prior commands");

    Failure.Result = ENativeGameplaySystemResult::Success;
    Failures += Expect(World.Reset() && World.GetEntityCount() == 0,
        "World Reset preserves the native system registry while clearing simulation state");
    World.BeginTick({0, 0.0, 0.0});
    FNativeGameplayUpdateReport ResetReport;
    Failures += Expect(World.UpdateNativeGameplaySystems(&ResetReport) ==
            ENativeGameplayUpdateResult::Success && ResetReport.NextFixedTickIndex == 0 &&
        Failure.CallCount == 2 && World.EndTick() == EWorldTickResult::Success,
        "native gameplay compiled schedule survives Reset with reset timing");

    FWorld DirtyWorld;
    FNativeGameplayProbe DirtyProbe;
    DirtyWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Dirty.System", DirtyProbe, ENativeGameplaySystemPhase::Variable));
    DirtyWorld.BeginTick({0, 0.0, 0.0});
    FNativeGameplayUpdateReport DirtyPreserved;
    DirtyPreserved.FixedCallbacksAttempted = 456;
    Failures += Expect(DirtyWorld.UpdateNativeGameplaySystems(&DirtyPreserved) ==
            ENativeGameplayUpdateResult::DirtySchedule &&
        DirtyPreserved.FixedCallbacksAttempted == 456 &&
        DirtyWorld.EndTick() == EWorldTickResult::Success,
        "dirty native schedule rejects execution without modifying the report");

    FWorld FixedFailureWorld;
    FNativeGameplayProbe FixedFailure;
    FNativeGameplayProbe NeverVariable;
    FixedFailure.Result = ENativeGameplaySystemResult::Failure;
    FixedFailureWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Fixed.Failure", FixedFailure, ENativeGameplaySystemPhase::Fixed));
    FixedFailureWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
        "Variable.Never", NeverVariable, ENativeGameplaySystemPhase::Variable));
    FixedFailureWorld.ConfigureNativeGameplayFixedStep({0.1, 3});
    FixedFailureWorld.CompileNativeGameplaySystemSchedule();
    FixedFailureWorld.BeginTick({0, 0.3, 0.3});
    FNativeGameplayUpdateReport FixedFailureReport;
    const ENativeGameplayUpdateResult FixedFailureResult =
        FixedFailureWorld.UpdateNativeGameplaySystems(&FixedFailureReport);
    FixedFailureWorld.EndTick();
    Failures += Expect(FixedFailureResult == ENativeGameplayUpdateResult::SystemFailed &&
        FixedFailureReport.FixedStepsAttempted == 1 &&
        FixedFailureReport.FixedStepsCompleted == 0 &&
        FixedFailureReport.FixedCallbacksAttempted == 1 &&
        NeverVariable.CallCount == 0 &&
        std::fabs(FixedFailureReport.FixedAccumulatorSeconds - 0.2) < 1.0e-12 &&
        std::fabs(FixedFailureReport.FixedSimulationSeconds - 0.1) < 1.0e-12,
        "failed fixed callback consumes its attempted step and retains unattempted due steps");
    FixedFailure.Result = ENativeGameplaySystemResult::Success;
    FixedFailureWorld.BeginTick({1, 0.0, 0.3});
    FNativeGameplayUpdateReport FixedRetryReport;
    FixedFailureWorld.UpdateNativeGameplaySystems(&FixedRetryReport);
    FixedFailureWorld.EndTick();
    Failures += Expect(FixedRetryReport.FixedStepsAttempted == 2 &&
        FixedRetryReport.FixedStepsCompleted == 2 && NeverVariable.CallCount == 1,
        "next tick consumes retained fixed work before its one Variable pass");

    FWorldFailingAllocator Allocator;
    FWorld OomWorld(Allocator);
    FNativeGameplayProbe OomProbe;
    const std::size_t BeforeRegisterAttempts = Allocator.AllocationAttempts;
    Allocator.bFail = true;
    Failures += Expect(OomWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
            "Oom.System", OomProbe, ENativeGameplaySystemPhase::Variable)) ==
            ENativeGameplaySystemRegisterResult::OutOfMemory,
        "native gameplay registration OOM is reported without partial registration");
    Allocator.bFail = false;
    Failures += Expect(Allocator.AllocationAttempts > BeforeRegisterAttempts &&
        OomWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
            "Oom.System", OomProbe, ENativeGameplaySystemPhase::Variable)) ==
            ENativeGameplaySystemRegisterResult::Success &&
        OomWorld.CompileNativeGameplaySystemSchedule() ==
            ENativeGameplayScheduleResult::Success,
        "native gameplay registration retries after transactional OOM");
    FNativeGameplayProbe OomSecondProbe;
    Failures += Expect(OomWorld.RegisterNativeGameplaySystem(MakeNativeGameplayDescriptor(
            "Oom.Second", OomSecondProbe, ENativeGameplaySystemPhase::Variable)) ==
            ENativeGameplaySystemRegisterResult::Success,
        "native gameplay graph mutation marks a previously compiled plan dirty");
    Allocator.bFail = true;
    Failures += Expect(OomWorld.CompileNativeGameplaySystemSchedule() ==
            ENativeGameplayScheduleResult::OutOfMemory,
        "native gameplay schedule compilation reports allocator failure");
    FNativeGameplayUpdateReport OomPreserved;
    OomPreserved.VariableCallbacksAttempted = 91;
    OomWorld.BeginTick({0, 0.0, 0.0});
    const ENativeGameplayUpdateResult OomDirtyResult =
        OomWorld.UpdateNativeGameplaySystems(&OomPreserved);
    OomWorld.EndTick();
    Failures += Expect(OomDirtyResult == ENativeGameplayUpdateResult::DirtySchedule &&
        OomPreserved.VariableCallbacksAttempted == 91 && OomProbe.CallCount == 0 &&
        OomSecondProbe.CallCount == 0,
        "failed schedule compile never exposes its preserved stale plan");
    Allocator.bFail = false;
    Failures += Expect(OomWorld.CompileNativeGameplaySystemSchedule() ==
            ENativeGameplayScheduleResult::Success,
        "native gameplay schedule compilation retries transactionally after OOM");
    OomWorld.BeginTick({1, 0.0, 0.0});
    FNativeGameplayUpdateReport OomReport;
    OomWorld.UpdateNativeGameplaySystems(&OomReport);
    OomWorld.EndTick();
    Failures += Expect(OomProbe.CallCount == 1 && OomSecondProbe.CallCount == 1 &&
        OomReport.VariableCallbacksAttempted == 2,
        "retried native gameplay compile installs one complete deterministic plan");
    return Failures;
}

} // namespace

int RunWorldTests()
{
    int Failures = 0;
    Failures += RunEntityLifetimeTests();
    Failures += RunComponentStorageTests();
    Failures += RunComponentLifetimeAndAlignmentTests();
    Failures += RunWorldFailureAndDetachTests();
    Failures += RunTransformValueAndHierarchyTests();
    Failures += RunTransformValidationAndDestroyTests();
    Failures += RunTransformDepthOomAndExtractionTests();
    Failures += RunWorldTickBoundaryTests();
    Failures += RunDeferredCommandFifoAndTokenTests();
    Failures += RunDeferredCommandOomAndReentryTests();
    Failures += RunDeferredCommandCancellationAndHierarchyTests();
    Failures += RunNativeGameplayGraphTests();
    Failures += RunNativeGameplayTimingTests();
    Failures += RunNativeGameplayFailureOomAndResetTests();
    return Failures;
}

} // namespace LE
