#include "Application/ApplicationMinimal.h"
#include "Containers/Array.h"
#include "Containers/HashMap.h"
#include "Containers/HashSet.h"
#include "Containers/Span.h"
#include "Containers/StaticArray.h"
#include "Containers/String.h"
#include "Memory/Relocation.h"
#include "Memory/SharedPtr.h"
#include "Memory/UniquePtr.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"
#include "Math/Vector.h"
#include "Platform/Platform.h"
#include "RAL/RALBuffer.h"
#include "RAL/RALCommandAllocator.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALDevice.h"
#include "RFGMinimal.h"
#include "Renderer/RenderFrameScheduler.h"
#include "Templates/Function.h"
#include "Types/EngineTypes.h"
#include "Types/RuntimeHandle.h"
#include "Types/Uuid.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace LE
{
int RunReflectionTests();
int RunReflectionMacroTests();
int RunPropertySerializationTests();
}

namespace
{

struct FStableIdTestDomain;
struct FOtherStableIdTestDomain;
struct FRuntimeHandleTestDomain;
struct FOtherRuntimeHandleTestDomain;

class FFakeRALCommandList final : public LE::FRALCommandList
{
public:
    ~FFakeRALCommandList() override
    {
        if (DestroyCount != nullptr) ++*DestroyCount;
        if (Events != nullptr) Events->PushBack(91);
    }

    void Begin() override { ++BeginCount; }
    void End() override { ++EndCount; }
    void ResourceBarriers(const LE::FRALBarrierBatch&) override {}
    void BeginRenderPass(const LE::FRALRenderPassDesc&) override {}
    void EndRenderPass() override {}
    void SetGraphicsPipeline(LE::FRALPipeline_Graphics*) override {}
    void SetViewport(const LE::FRALViewport&) override {}
    void SetScissorRect(const LE::FRALScissorRect&) override {}
    void SetVertexBuffer(LE::uint32, LE::FRALBuffer*, LE::uint64) override {}
    void SetIndexBuffer(LE::FRALBuffer*, LE::uint64, LE::EPixelFormat) override {}
    void SetBindGroup(LE::uint32, LE::FRALBindGroup*) override {}
    void SetPushConstants(LE::EShaderStage, const void*, LE::uint32) override {}
    void Draw(LE::uint32, LE::uint32, LE::uint32) override {}
    void DrawIndexed(LE::uint32, LE::uint32, LE::uint32, LE::int32, LE::uint32) override {}

    LE::Array<int>* Events = nullptr;
    int* DestroyCount = nullptr;
    int BeginCount = 0;
    int EndCount = 0;
};

class FFakeRALCommandAllocator final : public LE::FRALCommandAllocator
{
public:
    ~FFakeRALCommandAllocator() override
    {
        if (DestroyCount != nullptr) ++*DestroyCount;
        if (Events != nullptr) Events->PushBack(92);
    }

    bool Reset() override
    {
        ++ResetCount;
        if (Events != nullptr) Events->PushBack(2);
        return bResetSucceeds;
    }

    LE::Array<int>* Events = nullptr;
    int* DestroyCount = nullptr;
    int ResetCount = 0;
    bool bResetSucceeds = true;
};

class FFakeRALSemaphore final : public LE::FRALSemaphore
{
public:
    ~FFakeRALSemaphore() override { if (DestroyCount != nullptr) ++*DestroyCount; }
    int* DestroyCount = nullptr;
};

class FFakeRALFence final : public LE::FRALFence
{
public:
    ~FFakeRALFence() override { if (DestroyCount != nullptr) ++*DestroyCount; }

    void Reset() override
    {
        ++ResetCount;
        bSignaled = false;
        if (Events != nullptr)
        {
            Events->PushBack(5);
        }
    }

    void Wait(LE::uint64) override
    {
        ++WaitCount;
        if (Events != nullptr)
        {
            Events->PushBack(1);
        }
    }

    bool IsSignaled() override { return bSignaled; }

    LE::Array<int>* Events = nullptr;
    int* DestroyCount = nullptr;
    int WaitCount = 0;
    int ResetCount = 0;
    bool bSignaled = true;
};

class FFakeRALTexture final : public LE::FRALTexture
{
public:
    ~FFakeRALTexture() override
    {
        if (DestroyCount != nullptr) ++*DestroyCount;
        if (Events != nullptr) Events->PushBack(8);
    }
    const LE::FRALTextureDesc& GetDesc() const override { return Desc; }
    LE::FRALTextureDesc Desc;
    LE::Array<int>* Events = nullptr;
    int* DestroyCount = nullptr;
};

class FFakeRALBuffer final : public LE::FRALBuffer
{
public:
    ~FFakeRALBuffer() override
    {
        if (DestroyCount != nullptr) ++*DestroyCount;
        if (Events != nullptr) Events->PushBack(9);
    }

    void* Map(LE::uint64, LE::uint64) override { return nullptr; }
    void Unmap() override {}
    const LE::FRALBufferDesc& GetDesc() const override { return Desc; }

    LE::FRALBufferDesc Desc;
    LE::Array<int>* Events = nullptr;
    int* DestroyCount = nullptr;
};

class FFakeRALTextureView final : public LE::FRALTextureView
{
public:
    explicit FFakeRALTextureView(LE::FRALTexture* const InTexture)
        : Texture(InTexture)
    {
        Desc.Texture = InTexture;
    }

    LE::FRALTexture* GetTexture() const override { return Texture; }
    const LE::FRALTextureViewDesc& GetDesc() const override { return Desc; }

    LE::FRALTexture* Texture = nullptr;
    LE::FRALTextureViewDesc Desc;
};

class FFakeRALQueue final : public LE::FRALQueue
{
public:
    LE::ERALQueueSubmitResult Submit(const LE::FRALSubmitInfo& SubmitInfo) override
    {
        ++SubmitCount;
        CapturedSubmitInfo = SubmitInfo;
        if (Events != nullptr)
        {
            Events->PushBack(6);
        }
        return Result;
    }

    void WaitIdle() override { ++WaitIdleCount; }
    LE::EQueueType GetType() const override { return LE::EQueueType::Graphics; }

    LE::Array<int>* Events = nullptr;
    LE::ERALQueueSubmitResult Result = LE::ERALQueueSubmitResult::Success;
    LE::FRALSubmitInfo CapturedSubmitInfo;
    int SubmitCount = 0;
    int WaitIdleCount = 0;
};

class FFakeRALSwapchain final : public LE::FRALSwapchain
{
public:
    explicit FFakeRALSwapchain(LE::FRALTextureView* const InView)
        : View(InView)
    {
    }

    LE::FRALAcquireResult AcquireNextImage(LE::FRALSemaphore* SignalSemaphore, LE::uint64) override
    {
        ++AcquireCount;
        CapturedAcquireSemaphore = SignalSemaphore;
        if (Events != nullptr)
        {
            Events->PushBack(3);
        }
        bImageAcquired = AcquireStatus == LE::ERALSwapchainStatus::Success ||
            AcquireStatus == LE::ERALSwapchainStatus::Suboptimal;
        return { AcquireStatus, bImageAcquired ? View : nullptr };
    }

    LE::FRALTextureView* GetCurrentBackBufferView() const override
    {
        return bImageAcquired ? View : nullptr;
    }

    LE::ERALSwapchainStatus Present(LE::FRALSemaphore* WaitSemaphore) override
    {
        ++PresentCount;
        CapturedPresentSemaphore = WaitSemaphore;
        bImageAcquired = false;
        if (Events != nullptr)
        {
            Events->PushBack(7);
        }
        return PresentStatus;
    }

    bool Resize(LE::uint32, LE::uint32) override
    {
        ++ResizeCount;
        bImageAcquired = false;
        return bResizeSucceeds;
    }

    LE::Array<int>* Events = nullptr;
    LE::FRALTextureView* View = nullptr;
    LE::FRALSemaphore* CapturedAcquireSemaphore = nullptr;
    LE::FRALSemaphore* CapturedPresentSemaphore = nullptr;
    LE::ERALSwapchainStatus AcquireStatus = LE::ERALSwapchainStatus::Success;
    LE::ERALSwapchainStatus PresentStatus = LE::ERALSwapchainStatus::Success;
    int AcquireCount = 0;
    int PresentCount = 0;
    int ResizeCount = 0;
    bool bImageAcquired = false;
    bool bResizeSucceeds = true;
};

class FFakeRALDevice final : public LE::FRALDevice
{
public:
    LE::FRALQueue* GetGraphicsQueue() const override { return const_cast<FFakeRALQueue*>(&Queue); }
    LE::FRALBuffer* CreateBuffer(const LE::FRALBufferDesc& Desc) override
    {
        if (!bCreateOwnedBuffers) return nullptr;
        auto* Buffer = new FFakeRALBuffer();
        Buffer->Desc = Desc;
        Buffer->Events = Events;
        Buffer->DestroyCount = &DestroyedBuffers;
        ++CreatedBuffers;
        return Buffer;
    }
    LE::FRALTexture* CreateTexture(const LE::FRALTextureDesc& Desc) override
    {
        if (!bCreateOwnedTextures) return nullptr;
        auto* Texture = new FFakeRALTexture();
        Texture->Desc = Desc;
        Texture->Events = Events;
        Texture->DestroyCount = &DestroyedTextures;
        ++CreatedTextures;
        return Texture;
    }
    LE::FRALTextureView* CreateTextureView(const LE::FRALTextureViewDesc&) override { return nullptr; }
    LE::FRALShader* CreateShaderFromFile(LE::EShaderStage, const void*, LE::uint64, const LE::String&) override { return nullptr; }
    LE::FRALPipeline_Graphics* CreateGraphicsPipeline(const LE::FRALPipelineDesc_Graphics&) override { return nullptr; }
    LE::FRALCommandAllocator* CreateCommandAllocator(LE::EQueueType) override
    {
        if (ShouldFailCreation()) return nullptr;
        auto* Allocator = new FFakeRALCommandAllocator();
        Allocator->Events = Events;
        Allocator->DestroyCount = &DestroyedAllocators;
        Allocators.PushBack(Allocator);
        return Allocator;
    }
    LE::FRALCommandList* CreateCommandList(LE::FRALCommandAllocator*) override
    {
        if (ShouldFailCreation()) return nullptr;
        ++CreatedCommandLists;
        auto* CommandList = new FFakeRALCommandList();
        CommandList->Events = Events;
        CommandList->DestroyCount = &DestroyedCommandLists;
        return CommandList;
    }
    LE::FRALSemaphore* CreateBinarySemaphore() override
    {
        if (ShouldFailCreation()) return nullptr;
        auto* Semaphore = new FFakeRALSemaphore();
        Semaphore->DestroyCount = &DestroyedSemaphores;
        Semaphores.PushBack(Semaphore);
        return Semaphore;
    }
    LE::FRALFence* CreateFence(bool bInitiallySignaled) override
    {
        LastFenceInitiallySignaled = bInitiallySignaled;
        if (ShouldFailCreation()) return nullptr;
        auto* Fence = new FFakeRALFence();
        Fence->bSignaled = bInitiallySignaled;
        Fence->Events = Events;
        Fence->DestroyCount = &DestroyedFences;
        Fences.PushBack(Fence);
        return Fence;
    }
    LE::FRALSwapchain* CreateSwapchain(const LE::FRALSwapchainDesc&) override { return nullptr; }
    LE::FRALBindGroup* CreateBindGroup(const LE::FRALBindGroupDesc&) override { return nullptr; }
    LE::FRALBindGroupLayout* CreateBindGroupLayout(const LE::FRALBindGroupLayoutDesc&) override { return nullptr; }
    LE::FRALSampler* CreateSampler(const LE::FRALSamplerDesc&) override { return nullptr; }
    void* GetBindlessHeapGPUDescriptor() const override { return nullptr; }
    LE::uint32 AllocateBindlessIndex(LE::FRALResource*) override { return 0; }

private:
    bool ShouldFailCreation()
    {
        ++CreationCalls;
        return FailCreationCall != 0 && CreationCalls == FailCreationCall;
    }

public:
    FFakeRALQueue Queue;
    LE::Array<FFakeRALCommandAllocator*> Allocators;
    LE::Array<FFakeRALSemaphore*> Semaphores;
    LE::Array<FFakeRALFence*> Fences;
    LE::Array<int>* Events = nullptr;
    int FailCreationCall = 0;
    int CreationCalls = 0;
    int CreatedCommandLists = 0;
    int DestroyedCommandLists = 0;
    int DestroyedAllocators = 0;
    int DestroyedSemaphores = 0;
    int DestroyedFences = 0;
    int CreatedTextures = 0;
    int DestroyedTextures = 0;
    int CreatedBuffers = 0;
    int DestroyedBuffers = 0;
    bool LastFenceInitiallySignaled = false;
    bool bCreateOwnedTextures = false;
    bool bCreateOwnedBuffers = false;
};

struct FRenderFrameSchedulerFixture
{
    FRenderFrameSchedulerFixture()
        : View(&Texture)
        , Swapchain(&View)
    {
        Device.Events = &Events;
        Device.Queue.Events = &Events;
        Swapchain.Events = &Events;
    }

    bool Initialize() { return Scheduler.Initialize(&Device); }

    LE::FRenderFrameResult Execute()
    {
        LE::FRenderFrameCallback Callback = [this](LE::FRenderFrameScope& Frame) noexcept
        {
            ++RecordCount;
            CapturedBackBufferView = Frame.GetBackBufferView();
            CapturedFrameIndex = Frame.GetFrameIndex();
            CapturedSlotIndex = Frame.GetSlotIndex();
            CapturedCommandList = Frame.GetCommandList();
            Events.PushBack(4);
            if (DeferredResource != nullptr)
            {
                bDeferredAccepted = Frame.DeferRelease(DeferredResource);
                if (bDeferredAccepted) DeferredResource = nullptr;
            }
            return RecordResult;
        };
        return Scheduler.ExecuteFrame(&Swapchain, Callback);
    }

    LE::Array<int> Events;
    FFakeRALTexture Texture;
    FFakeRALTextureView View;
    FFakeRALDevice Device;
    FFakeRALSwapchain Swapchain;
    LE::FRenderFrameScheduler Scheduler;
    LE::ERenderFrameRecordResult RecordResult = LE::ERenderFrameRecordResult::Success;
    LE::FRALResource* DeferredResource = nullptr;
    LE::FRALTextureView* CapturedBackBufferView = nullptr;
    LE::FRALCommandList* CapturedCommandList = nullptr;
    LE::uint64 CapturedFrameIndex = 0;
    LE::uint32 CapturedSlotIndex = 0;
    int RecordCount = 0;
    bool bDeferredAccepted = false;
};

class FTrackedRALResource final : public LE::FRALResource
{
public:
    explicit FTrackedRALResource(int* const InDestroyCount) : DestroyCount(InDestroyCount) {}
    ~FTrackedRALResource() override { if (DestroyCount != nullptr) ++*DestroyCount; }

private:
    int* DestroyCount = nullptr;
};

class FFakeRFGDeferredReleaseSink final : public LE::IRFGDeferredReleaseSink
{
public:
    bool DeferRelease(LE::FRALResource* const Resource) noexcept override
    {
        ++CallCount;
        if (Resource == nullptr || (RejectCall != 0 && CallCount == RejectCall))
        {
            return false;
        }
        Resources.PushBack(Resource);
        return true;
    }

    void DestroyAccepted() noexcept
    {
        for (LE::FRALResource* const Resource : Resources)
        {
            LE::RAL::DestroyResource(Resource);
        }
        Resources.Clear();
    }

    LE::Array<LE::FRALResource*> Resources;
    int RejectCall = 0;
    int CallCount = 0;
};

class FFrameScopeRFGDeferredReleaseSink final : public LE::IRFGDeferredReleaseSink
{
public:
    explicit FFrameScopeRFGDeferredReleaseSink(LE::FRenderFrameScope& InFrameScope) noexcept
        : FrameScope(InFrameScope)
    {
    }

    bool DeferRelease(LE::FRALResource* const Resource) noexcept override
    {
        return FrameScope.DeferRelease(Resource);
    }

private:
    LE::FRenderFrameScope& FrameScope;
};

struct BitwiseOwned
{
    explicit BitwiseOwned(const int Value = 0) : Pointer(new int(Value)) { ++LiveCount; }
    BitwiseOwned(const BitwiseOwned&) = delete;
    BitwiseOwned& operator=(const BitwiseOwned&) = delete;
    BitwiseOwned(BitwiseOwned&& Other) noexcept : Pointer(Other.Pointer)
    {
        Other.Pointer = nullptr;
    }
    ~BitwiseOwned()
    {
        delete Pointer;
        if (Pointer != nullptr)
        {
            --LiveCount;
            ++DestructionCount;
        }
    }
    int* Pointer = nullptr;
    static int LiveCount;
    static int DestructionCount;
};

int BitwiseOwned::LiveCount = 0;
int BitwiseOwned::DestructionCount = 0;

} // namespace

namespace LE
{
template <>
struct IsBitwiseRelocatable<BitwiseOwned> : std::true_type
{
};
}

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

template <typename T>
bool NearlyEqual(const T Left, const T Right, const T Tolerance)
{
    return std::abs(Left - Right) <= Tolerance;
}

class FailingAllocator final : public LE::IAllocator
{
public:
    explicit FailingAllocator(const bool bInFailAllocations = true)
        : bFailAllocations(bInFailAllocations)
    {
    }

    void* Allocate(const std::size_t Size, const std::size_t Alignment) noexcept override
    {
        ++AllocationAttempts;
        LastAlignment = Alignment;
        if (Alignment > MaximumAlignment)
        {
            MaximumAlignment = Alignment;
        }
        if (bFailAllocations)
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
        LE::GetDefaultAllocator().Deallocate(Address, Size, Alignment);
    }

    bool bFailAllocations = true;
    std::size_t AllocationAttempts = 0;
    std::size_t DeallocationCount = 0;
    std::size_t LiveAllocations = 0;
    std::size_t LastAlignment = 0;
    std::size_t MaximumAlignment = 0;
};

struct Counted
{
    explicit Counted(const int InValue = 0)
        : Value(InValue)
    {
        ++ConstructionCount;
        ++LiveCount;
    }

    Counted(const Counted& Other)
        : Value(Other.Value)
    {
        ++ConstructionCount;
        ++CopyCount;
        ++LiveCount;
    }

    Counted(Counted&& Other) noexcept
        : Value(Other.Value)
    {
        Other.Value = -1;
        ++ConstructionCount;
        ++MoveCount;
        ++LiveCount;
    }

    Counted& operator=(const Counted& Other)
    {
        Value = Other.Value;
        ++CopyCount;
        return *this;
    }

    Counted& operator=(Counted&& Other) noexcept
    {
        Value = Other.Value;
        Other.Value = -1;
        ++MoveCount;
        return *this;
    }

    ~Counted()
    {
        ++DestructionCount;
        --LiveCount;
    }

    static void Reset()
    {
        ConstructionCount = 0;
        DestructionCount = 0;
        CopyCount = 0;
        MoveCount = 0;
        LiveCount = 0;
    }

    int Value = 0;
    static int ConstructionCount;
    static int DestructionCount;
    static int CopyCount;
    static int MoveCount;
    static int LiveCount;
};

int Counted::ConstructionCount = 0;
int Counted::DestructionCount = 0;
int Counted::CopyCount = 0;
int Counted::MoveCount = 0;
int Counted::LiveCount = 0;

struct MoveOnly
{
    explicit MoveOnly(const int InValue = 0) : Value(InValue) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& Other) noexcept : Value(Other.Value) { Other.Value = -1; }
    MoveOnly& operator=(MoveOnly&& Other) noexcept
    {
        Value = Other.Value;
        Other.Value = -1;
        return *this;
    }
    int Value = 0;
};

struct MoveOnlyKey
{
    explicit MoveOnlyKey(const int InValue = 0) : Value(InValue) {}
    MoveOnlyKey(const MoveOnlyKey&) = delete;
    MoveOnlyKey& operator=(const MoveOnlyKey&) = delete;
    MoveOnlyKey(MoveOnlyKey&& Other) noexcept : Value(Other.Value) { Other.Value = -1; }
    MoveOnlyKey& operator=(MoveOnlyKey&& Other) noexcept
    {
        Value = Other.Value;
        Other.Value = -1;
        return *this;
    }
    int Value = 0;
};

struct MoveOnlyKeyHash
{
    std::size_t operator()(const MoveOnlyKey& Key) const noexcept
    {
        return LE::DefaultHash<int>{}(Key.Value);
    }
};

struct MoveOnlyKeyEqual
{
    bool operator()(const MoveOnlyKey& Left, const MoveOnlyKey& Right) const noexcept
    {
        return Left.Value == Right.Value;
    }
};

struct ConstantIntHash
{
    std::size_t operator()(const int) const noexcept { return 0; }
};

struct ModuloHash
{
    int Modulus = 1;
    std::size_t operator()(const int Value) const noexcept
    {
        return LE::DefaultHash<int>{}(Value % Modulus);
    }
};

struct ModuloEqual
{
    int Modulus = 1;
    bool operator()(const int Left, const int Right) const noexcept
    {
        return Left % Modulus == Right % Modulus;
    }
};

struct ThrowingDefaultHash
{
    ThrowingDefaultHash() noexcept(false) {}
    ThrowingDefaultHash(ThrowingDefaultHash&&) noexcept = default;
    ThrowingDefaultHash& operator=(ThrowingDefaultHash&&) noexcept = default;
    std::size_t operator()(const int Value) const noexcept
    {
        return LE::DefaultHash<int>{}(Value);
    }
};

class ThrowingCopyAllocatorPolicy
{
public:
    ThrowingCopyAllocatorPolicy() noexcept : Instance(&LE::GetDefaultAllocator()) {}
    explicit ThrowingCopyAllocatorPolicy(LE::IAllocator& Allocator) noexcept : Instance(&Allocator) {}
    ThrowingCopyAllocatorPolicy(const ThrowingCopyAllocatorPolicy& Other) noexcept(false)
        : Instance(Other.Instance)
    {
    }
    ThrowingCopyAllocatorPolicy(ThrowingCopyAllocatorPolicy&&) noexcept = default;
    ThrowingCopyAllocatorPolicy& operator=(ThrowingCopyAllocatorPolicy&&) noexcept = default;
    LE::IAllocator& Get() const noexcept { return *Instance; }
    friend void swap(
        ThrowingCopyAllocatorPolicy& Left,
        ThrowingCopyAllocatorPolicy& Right) noexcept
    {
        using std::swap;
        swap(Left.Instance, Right.Instance);
    }

private:
    LE::IAllocator* Instance;
};

using ThrowingDefaultHashMap =
    LE::HashMap<int, int, ThrowingDefaultHash, LE::DefaultEqual<int>>;
using ThrowingDefaultHashSet =
    LE::HashSet<int, ThrowingDefaultHash, LE::DefaultEqual<int>>;
using ThrowingAllocatorHashMap = LE::HashMap<
    int,
    int,
    LE::DefaultHash<int>,
    LE::DefaultEqual<int>,
    ThrowingCopyAllocatorPolicy>;

static_assert(std::is_nothrow_default_constructible<LE::HashMap<int, int>>::value,
    "default HashMap constructor should reflect non-throwing default policies");
static_assert(std::is_nothrow_default_constructible<LE::HashSet<int>>::value,
    "default HashSet constructor should reflect non-throwing map storage");
static_assert(!std::is_nothrow_default_constructible<ThrowingDefaultHashMap>::value,
    "HashMap default noexcept must reflect a potentially throwing Hash constructor");
static_assert(!std::is_nothrow_default_constructible<ThrowingDefaultHashSet>::value,
    "HashSet default noexcept must reflect a potentially throwing Hash constructor");
static_assert(!std::is_nothrow_constructible<
        ThrowingAllocatorHashMap, const ThrowingCopyAllocatorPolicy&>::value,
    "HashMap allocator constructor noexcept must reflect allocator copying");

struct LookupView
{
    int Value;
};

struct LookupKey
{
    explicit LookupKey(const int InValue = 0) : Value(InValue) {}
    LookupKey(const LookupView&) = delete;
    int Value = 0;
};

struct TransparentLookupHash
{
    using is_transparent = void;
    std::size_t operator()(const LookupKey& Key) const noexcept { return LE::DefaultHash<int>{}(Key.Value); }
    std::size_t operator()(const LookupView& Key) const noexcept { return LE::DefaultHash<int>{}(Key.Value); }
};

struct TransparentLookupEqual
{
    using is_transparent = void;
    bool operator()(const LookupKey& Left, const LookupKey& Right) const noexcept { return Left.Value == Right.Value; }
    bool operator()(const LookupView& Left, const LookupKey& Right) const noexcept { return Left.Value == Right.Value; }
    bool operator()(const LookupKey& Left, const LookupView& Right) const noexcept { return Left.Value == Right.Value; }
};

struct alignas(64) OverAligned
{
    std::uint64_t Words[8]{};
};

struct OwnedValue
{
    explicit OwnedValue(const int InValue = 0) noexcept : Value(InValue) { ++LiveCount; }
    ~OwnedValue() noexcept { --LiveCount; ++DestroyedCount; }
    int Value = 0;
    static int LiveCount;
    static int DestroyedCount;
};

int OwnedValue::LiveCount = 0;
int OwnedValue::DestroyedCount = 0;

struct PrefixBase
{
    virtual ~PrefixBase() noexcept = default;
    int Prefix = 3;
};

struct ObservedBase
{
    virtual ~ObservedBase() noexcept = default;
    int Value = 0;
};

struct SharedDerived final : PrefixBase, ObservedBase
{
    explicit SharedDerived(const int InValue = 0) noexcept
    {
        Value = InValue;
        ++LiveCount;
    }
    ~SharedDerived() noexcept override { --LiveCount; ++DestroyedCount; }
    static int LiveCount;
    static int DestroyedCount;
};

int SharedDerived::LiveCount = 0;
int SharedDerived::DestroyedCount = 0;

struct StatefulOwnedDeleter
{
    int* Calls = nullptr;
    void operator()(OwnedValue* const Value) const noexcept
    {
        ++*Calls;
        delete Value;
    }
};

void DestroyAdoptedOwned(void* const Context, OwnedValue* const Value) noexcept
{
    ++*static_cast<int*>(Context);
    delete Value;
}

struct AddCallable
{
    int Offset = 0;
    int operator()(const int Value) noexcept { return Value + Offset; }
};

struct ThrowingDestruction
{
    ~ThrowingDestruction() noexcept(false) {}
};

static_assert(std::is_nothrow_destructible<OwnedValue>::value,
    "ownership factory test value satisfies the non-throwing destructor contract");
static_assert(!std::is_nothrow_destructible<ThrowingDestruction>::value,
    "throwing destructors are detectable and excluded by ownership factories");
static_assert(std::is_nothrow_constructible<StatefulOwnedDeleter, StatefulOwnedDeleter&>::value,
    "stateful lvalue deleter is safe to store in the external control block");

[[noreturn]] void ExitOnOom(const std::size_t, const std::size_t)
{
    std::_Exit(91);
}

[[noreturn]] void ExitOnContract(const char*, const char*, const int)
{
    std::_Exit(92);
}

int RunAllocationTests()
{
    int Failures = 0;
    std::size_t Product = 0;
    Failures += Expect(LE::TryMultiplySize(7, 9, Product) && Product == 63,
        "checked allocation multiplication succeeds");
    Failures += Expect(!LE::TryMultiplySize((std::size_t(-1)), 2, Product),
        "checked allocation multiplication rejects overflow");
    Failures += Expect(LE::IsValidAlignment(1) && LE::IsValidAlignment(64)
        && !LE::IsValidAlignment(0) && !LE::IsValidAlignment(3),
        "alignment validation accepts only powers of two");

    FailingAllocator Allocator(false);
    Failures += Expect(LE::TryAllocateBytes(Allocator, 0, 16) == nullptr
        && Allocator.AllocationAttempts == 0,
        "zero-byte allocation has no allocator call or address identity");
    void* const Block = LE::TryAllocateBytes(Allocator, 32, 16);
    Failures += Expect(Block != nullptr && Allocator.LiveAllocations == 1,
        "allocation helper uses injected allocator");
    Allocator.Deallocate(Block, 32, 16);
    Failures += Expect(Allocator.LiveAllocations == 0 && Allocator.DeallocationCount >= 1,
        "deallocation returns through creating allocator");
    return Failures;
}

int RunArrayLifetimeTests()
{
    int Failures = 0;
    Counted::Reset();
    {
        LE::Array<Counted> Values;
        Values.EmplaceBack(10);
        Values.EmplaceBack(20);
        Values.Insert(1, Counted(15));
        Failures += Expect(Values.Size() == 3 && Values[0].Value == 10
            && Values[1].Value == 15 && Values[2].Value == 20,
            "Array insert preserves order");
        Values.Erase(1);
        Failures += Expect(Values.Size() == 2 && Values[1].Value == 20,
            "Array erase compacts non-trivial elements");

        LE::Array<Counted> Copy(Values);
        Failures += Expect(Copy.Size() == 2 && Copy[0].Value == 10 && Counted::CopyCount >= 2,
            "Array copy constructs elements");
        auto CopyIterator = Copy.begin();
        ++CopyIterator;
        Failures += Expect(CopyIterator->Value == 20, "copied Array supports iteration");
#if !defined(NDEBUG)
        Failures += Expect(CopyIterator.IsValid(),
            "copied Array initializes iterator diagnostics");
#endif
        LE::Array<Counted> Moved(std::move(Copy));
        Failures += Expect(Copy.IsEmpty() && Moved.Size() == 2,
            "Array move transfers storage ownership");
        Values.Clear();
        Failures += Expect(Values.IsEmpty(), "Array clear destroys all elements");
    }
    Failures += Expect(Counted::LiveCount == 0
        && Counted::ConstructionCount == Counted::DestructionCount,
        "Array balances every construction and destruction");

    LE::Array<MoveOnly> MoveOnlyValues;
    for (int Index = 0; Index < 12; ++Index)
    {
        MoveOnlyValues.EmplaceBack(Index);
    }
    MoveOnlyValues.Insert(3, MoveOnly(99));
    MoveOnlyValues.Erase(4, 2);
    Failures += Expect(MoveOnlyValues[3].Value == 99 && MoveOnlyValues.Size() == 11,
        "Array supports move-only growth insert and erase");

    LE::Array<int> SelfAliasing;
    SelfAliasing.Reserve(4);
    SelfAliasing.PushBack(10);
    SelfAliasing.PushBack(20);
    SelfAliasing.PushBack(30);
    SelfAliasing.PushBack(40);
    SelfAliasing.PushBack(SelfAliasing[0]);
    Failures += Expect(SelfAliasing.Size() == 5 && SelfAliasing[0] == 10
        && SelfAliasing[4] == 10,
        "self-aliased PushBack survives reallocation");
    SelfAliasing.Insert(1, SelfAliasing[3]);
    Failures += Expect(SelfAliasing[1] == 40,
        "self-aliased Insert survives shifting storage");

    LE::Array<MoveOnly> SelfMove;
    SelfMove.Reserve(4);
    for (int Index = 0; Index < 4; ++Index)
    {
        SelfMove.EmplaceBack(Index + 1);
    }
    SelfMove.PushBack(std::move(SelfMove[0]));
    Failures += Expect(SelfMove[0].Value == -1 && SelfMove[4].Value == 1,
        "self-aliased move PushBack rebinds after growth");

    BitwiseOwned::LiveCount = 0;
    BitwiseOwned::DestructionCount = 0;
    {
        LE::Array<BitwiseOwned> Relocated;
        Relocated.EmplaceBack(1);
        Relocated.EmplaceBack(2);
        Relocated.Erase(0);
        Failures += Expect(BitwiseOwned::LiveCount == 1
            && BitwiseOwned::DestructionCount == 1,
            "erase destroys removed explicitly bitwise-relocatable element");
    }
    Failures += Expect(BitwiseOwned::LiveCount == 0 && BitwiseOwned::DestructionCount == 2,
        "bitwise relocation transfers ownership without leaking destruction");
    return Failures;
}

int RunArrayStorageTests()
{
    int Failures = 0;
    LE::Array<int> Values;
    std::size_t PreviousCapacity = 0;
    for (int Index = 0; Index < 128; ++Index)
    {
        Values.PushBack(Index);
        Failures += Expect(Values.Capacity() >= Values.Size()
            && Values.Capacity() >= PreviousCapacity,
            "Array growth is monotonic and covers size");
        PreviousCapacity = Values.Capacity();
    }
    for (int Index = 0; Index < 128; ++Index)
    {
        Failures += Expect(Values[static_cast<std::size_t>(Index)] == Index,
            "Array growth preserves values");
    }

    Values.Reserve(256);
    int* const StableAddress = &Values[0];
    auto StableIterator = Values.begin();
#if !defined(NDEBUG)
    auto OldEnd = Values.end();
#endif
    Values.PushBack(128);
    Failures += Expect(&Values[0] == StableAddress && *StableIterator == 0,
        "append without growth preserves earlier references");
#if !defined(NDEBUG)
    Failures += Expect(StableIterator.IsValid(),
        "Debug iterator preserves positions before appended element");
    Failures += Expect(!OldEnd.IsValid(), "Debug iterator detects invalidated old end");
#endif

    auto BeforeInsert = Values.begin();
    auto AtInsert = Values.begin();
    ++AtInsert;
    Values.Insert(1, -1);
    Failures += Expect(*BeforeInsert == 0, "insert preserves iterator before insertion point");
#if !defined(NDEBUG)
    Failures += Expect(BeforeInsert.IsValid(),
        "Debug iterator preserves position before insertion point");
    Failures += Expect(!AtInsert.IsValid(),
        "Debug iterator detects insertion-point invalidation");
#endif
    auto BeforeErase = Values.begin();
    auto AtErase = Values.begin();
    ++AtErase;
    ++AtErase;
    Values.Erase(2);
    Failures += Expect(*BeforeErase == 0, "erase preserves iterator before erased position");
#if !defined(NDEBUG)
    Failures += Expect(BeforeErase.IsValid(),
        "Debug iterator preserves position before erased element");
    Failures += Expect(!AtErase.IsValid(),
        "Debug iterator detects erased-position invalidation");
#endif
    Values.PushBack(777);
    Values.PushBack(778);
#if !defined(NDEBUG)
    Failures += Expect(!AtInsert.IsValid() && !AtErase.IsValid(),
        "later mutations never revive previously invalidated iterators");
#endif

#if !defined(NDEBUG)
    auto BeforeReserve = Values.begin();
#endif
    Values.Reserve(512);
#if !defined(NDEBUG)
    Failures += Expect(!BeforeReserve.IsValid(),
        "Reserve reallocation invalidates every iterator");
#endif
    Values.Shrink();
    Failures += Expect(Values.Capacity() == Values.Size(),
        "Shrink reduces capacity to size");
#if !defined(NDEBUG)
    auto BeforeClear = Values.begin();
#endif
    Values.Clear();
    Failures += Expect(Values.Size() == 0, "Clear removes every element");
#if !defined(NDEBUG)
    Failures += Expect(!BeforeClear.IsValid(),
        "Debug iterator detects Clear invalidation");
#endif
    Values.Shrink();
    Failures += Expect(Values.Data() == nullptr && Values.Capacity() == 0,
        "empty Shrink returns to zero-allocation state");

    LE::Array<int> Resized;
    Resized.Resize(3);
    Failures += Expect(Resized.Size() == 3 && Resized[0] == 0 && Resized[2] == 0,
        "Resize value-initializes newly grown elements");
    Resized.Resize(6, 7);
    Failures += Expect(Resized.Size() == 6 && Resized[2] == 0 && Resized[5] == 7,
        "Resize fill preserves the prefix and copies the fill value");
    Resized.Resize(2);
    Failures += Expect(Resized.Size() == 2 && Resized[1] == 0,
        "Resize shrink destroys the suffix and preserves the prefix");

    LE::Array<int> AliasedResize{11};
    AliasedResize.Resize(16, AliasedResize[0]);
    Failures += Expect(AliasedResize.Size() == 16 && AliasedResize[15] == 11,
        "Resize fill survives self-aliased reallocation");

    LE::Array<int> Iterated{1, 2, 3, 4};
    int Sum = 0;
    for (const int Value : Iterated)
    {
        Sum += Value;
    }
    auto Iterator = Iterated.begin();
    ++Iterator;
    const LE::Array<int>& ConstIterated = Iterated;
    auto ConstIterator = ConstIterated.begin();
    ++ConstIterator;
    auto EndIterator = Iterated.end();
    --EndIterator;
    Failures += Expect(Sum == 10 && *Iterator == 2 && Iterator == ConstIterator
        && ConstIterator == Iterator && *EndIterator == 4,
        "Array bidirectional iterators support range-for and const comparison");

    LE::Array<int> MiddleGrowth;
    MiddleGrowth.Reserve(4);
    MiddleGrowth.PushBack(1);
    MiddleGrowth.PushBack(2);
    MiddleGrowth.PushBack(3);
    MiddleGrowth.PushBack(4);
    const std::size_t CapacityBeforeMiddleInsert = MiddleGrowth.Capacity();
    MiddleGrowth.Insert(2, 99);
    int MiddleGrowthSum = 0;
    std::size_t MiddleGrowthCount = 0;
    for (auto Current = MiddleGrowth.begin(); Current != MiddleGrowth.end(); ++Current)
    {
#if !defined(NDEBUG)
        Failures += Expect(Current.IsValid(),
            "middle reallocation initializes every live iterator token");
#endif
        MiddleGrowthSum += *Current;
        ++MiddleGrowthCount;
    }
    Failures += Expect(MiddleGrowth.Capacity() > CapacityBeforeMiddleInsert
        && MiddleGrowthCount == 5 && MiddleGrowthSum == 109
        && MiddleGrowth[0] == 1 && MiddleGrowth[1] == 2
        && MiddleGrowth[2] == 99 && MiddleGrowth[3] == 3 && MiddleGrowth[4] == 4,
        "middle insert with reallocation preserves order and full iteration");
#if !defined(NDEBUG)
    Failures += Expect(MiddleGrowth.end().IsValid(),
        "Debug iterator exposes a valid current end position");
#endif
    return Failures;
}

int RunArrayFailureAndAlignmentTests()
{
    int Failures = 0;
    FailingAllocator Allocator(false);
    {
        LE::Array<int> Values(Allocator);
        Failures += Expect(Values.TryReserve(4), "TryReserve succeeds when allocator permits it");
        Values.PushBack(1);
        Values.PushBack(2);
        Values.PushBack(3);
        Values.PushBack(4);
        Allocator.bFailAllocations = true;
        int* const OriginalData = Values.Data();
        const std::size_t OriginalSize = Values.Size();
        const std::size_t OriginalCapacity = Values.Capacity();
        Failures += Expect(!Values.TryPushBack(5)
            && Values.Data() == OriginalData
            && Values.Size() == OriginalSize
            && Values.Capacity() == OriginalCapacity
            && Values[0] == 1 && Values[3] == 4,
            "TryPushBack preserves Array after allocation failure");
        Failures += Expect(!Values.TryInsert(2, Values[0])
            && Values.Data() == OriginalData && Values.Size() == OriginalSize
            && Values[0] == 1 && Values[1] == 2 && Values[2] == 3 && Values[3] == 4,
            "self-aliased TryInsert preserves Array after allocation failure");
        Failures += Expect(!Values.TryReserve(8)
            && Values.Data() == OriginalData && Values.Capacity() == OriginalCapacity,
            "TryReserve preserves Array after allocation failure");
        Failures += Expect(!Values.TryResize(8)
            && Values.Data() == OriginalData && Values.Size() == OriginalSize
            && Values.Capacity() == OriginalCapacity && Values[3] == 4,
            "TryResize preserves Array after allocation failure");
        Failures += Expect(!Values.TryResize(8, Values[0])
            && Values.Data() == OriginalData && Values.Size() == OriginalSize
            && Values[0] == 1 && Values[3] == 4,
            "self-aliased fill TryResize preserves Array after allocation failure");
        Values.Erase(3);
        Failures += Expect(!Values.TryShrink() && Values.Data() == OriginalData
            && Values.Size() == 3 && Values.Capacity() == OriginalCapacity,
            "TryShrink preserves Array after allocation failure");
        const std::size_t AttemptsBeforeOverflow = Allocator.AllocationAttempts;
        Failures += Expect(!Values.TryReserve(LE::Array<int>::MaxSize() + 1)
            && Allocator.AllocationAttempts == AttemptsBeforeOverflow,
            "TryReserve rejects capacity overflow before allocator call");
    }
    Failures += Expect(Allocator.LiveAllocations == 0 && Allocator.DeallocationCount >= 1,
        "Array destructor deallocates through original injected allocator");

    FailingAllocator AlignedAllocator(false);
    {
        LE::Array<OverAligned> Values(AlignedAllocator);
        Values.EmplaceBack();
        Failures += Expect(reinterpret_cast<std::uintptr_t>(Values.Data()) % alignof(OverAligned) == 0
            && AlignedAllocator.MaximumAlignment >= alignof(OverAligned),
            "Array preserves over-aligned element allocation");
    }
    Failures += Expect(AlignedAllocator.LiveAllocations == 0,
        "over-aligned storage returns through creating allocator");
    return Failures;
}

int RunStaticArrayAndSpanTests()
{
    int Failures = 0;
    LE::StaticArray<int, 3> Values;
    Values[0] = 2;
    Values[1] = 4;
    Values[2] = 6;
    const LE::StaticArray<int, 3>& ConstValues = Values;
    Failures += Expect(ConstValues.Size() == 3 && ConstValues[1] == 4,
        "StaticArray provides const fixed-size access");

    LE::Span<int> View = Values.AsSpan();
    LE::Span<const int> ConstView = View;
    View[1] = 5;
    Failures += Expect(Values[1] == 5 && ConstView[1] == 5,
        "Span is a non-owning mutable/const view");
    const LE::Span<const int> Tail = ConstView.Subspan(1, 2);
    Failures += Expect(Tail.Size() == 2 && Tail[0] == 5 && Tail[1] == 6,
        "Span subspan preserves the borrowed range");
    std::size_t SpanBytes = 0;
    Failures += Expect(ConstView.TrySizeBytes(SpanBytes) && SpanBytes == 3 * sizeof(int),
        "Span reports checked byte size");
    const LE::Span<int> OverflowView(
        reinterpret_cast<int*>(static_cast<std::uintptr_t>(1)),
        (std::numeric_limits<std::size_t>::max)());
    Failures += Expect(!OverflowView.TrySizeBytes(SpanBytes),
        "Span byte size rejects overflow");
    const LE::Span<int> NormalizedEmpty(Values.Data(), 0);
    Failures += Expect(NormalizedEmpty.Data() == nullptr && NormalizedEmpty.IsEmpty(),
        "zero-length Span has canonical null storage");

    LE::StaticArray<int, 0> EmptyArray;
    LE::Span<int> EmptySpan = EmptyArray.AsSpan();
    Failures += Expect(EmptyArray.IsEmpty() && EmptyArray.Data() == nullptr
        && EmptySpan.IsEmpty() && EmptySpan.Data() == nullptr
        && EmptySpan.begin() == EmptySpan.end(),
        "StaticArray<0> and empty Span have no backing allocation");

    static_assert(std::is_constructible<LE::Span<const int>, LE::Span<int>>::value,
        "mutable Span must convert to const Span");
    static_assert(!std::is_constructible<LE::Span<int>, LE::Span<const int>>::value,
        "const Span must not convert to mutable Span");
    static_assert(!std::is_constructible<LE::Span<int>, LE::StaticArray<int, 3>&&>::value,
        "Span must not silently bind to a temporary owner");
    static_assert(LE::IsBitwiseRelocatableV<int>, "trivial values are bitwise relocatable");
    static_assert(!LE::IsBitwiseRelocatableV<Counted>,
        "non-trivial values use move construction and destruction");
    return Failures;
}

int RunHashContainerSmokeTests()
{
    int Failures = 0;
    LE::HashMap<int, int> Values;
    Failures += Expect(Values.Insert(1, 10) && Values.Contains(1)
        && Values.Find(1) != nullptr && *Values.Find(1) == 10,
        "HashMap inserts and finds a basic key");
    Failures += Expect(!Values.Insert(1, 99) && *Values.Find(1) == 10,
        "HashMap duplicate insert preserves the existing value");
    Failures += Expect(!Values.InsertOrAssign(1, 20) && *Values.Find(1) == 20,
        "HashMap InsertOrAssign updates an existing value");

    LE::HashMap<LookupKey, int, TransparentLookupHash, TransparentLookupEqual> Heterogeneous;
    Heterogeneous.Insert(LookupKey(7), 70);
    const LookupView Seven{7};
    Failures += Expect(Heterogeneous.Contains(Seven) && *Heterogeneous.Find(Seven) == 70,
        "transparent HashMap lookup does not construct a temporary key");

    LE::HashSet<int> Set;
    Failures += Expect(Set.Insert(3) && Set.Contains(3) && Set.Erase(3) && Set.IsEmpty(),
        "HashSet basic operations match HashMap semantics");
    return Failures;
}

int RunHashMapStorageTests()
{
    int Failures = 0;
    LE::HashMap<int, int> Values;
    Values.Reserve(100);
    const std::size_t ReservedBuckets = Values.BucketCount();
    Failures += Expect(ReservedBuckets >= 100 && Values.LoadFactor() <= Values.MaxLoadFactor(),
        "HashMap Reserve provides capacity under the maximum load factor");
    for (int Index = 0; Index < 100; ++Index)
    {
        Values.Insert(Index, Index * 3);
    }

    LE::HashMap<int, int> SelfAliased;
    for (int Index = 0; Index < 5; ++Index) { SelfAliased.Insert(Index, Index + 100); }
    const int& AliasedValue = *SelfAliased.Find(0);
    SelfAliased.Insert(99, AliasedValue);
    Failures += Expect(SelfAliased.Contains(99) && *SelfAliased.Find(99) == 100,
        "HashMap stages a self-aliased mapped value before forced growth");
    Failures += Expect(Values.BucketCount() == ReservedBuckets
        && Values.LoadFactor() <= Values.MaxLoadFactor(),
        "HashMap Reserve prevents growth through the requested live count");

    Values.Rehash(512);
    Failures += Expect(Values.BucketCount() >= 512 && Values.Size() == 100,
        "HashMap Rehash changes bucket storage while preserving size");
    for (int Index = 0; Index < 100; ++Index)
    {
        Failures += Expect(Values.Find(Index) != nullptr && *Values.Find(Index) == Index * 3,
            "HashMap Rehash preserves every key and value");
    }

    bool Seen[100]{};
    std::size_t IterationCount = 0;
    for (const auto& Item : Values)
    {
        const int Key = Item.GetKey();
        Failures += Expect(Key >= 0 && Key < 100 && !Seen[Key],
            "HashMap iteration visits no key twice");
        if (Key >= 0 && Key < 100) { Seen[Key] = true; }
        ++IterationCount;
    }
    Failures += Expect(IterationCount == Values.Size(),
        "HashMap iteration visits every live entry without an order assumption");

    LE::HashMap<int, int> Copy(Values);
    LE::HashMap<int, int> CopyAssigned;
    CopyAssigned = Copy;
    LE::HashMap<int, int> Moved(std::move(Copy));
    LE::HashMap<int, int> MoveAssigned;
    MoveAssigned = std::move(CopyAssigned);
    Failures += Expect(Copy.IsEmpty() && CopyAssigned.IsEmpty()
        && Moved.Size() == 100 && MoveAssigned.Size() == 100
        && *Moved.Find(42) == 126 && *MoveAssigned.Find(99) == 297,
        "HashMap copy and move preserve values and transfer ownership");

    LE::HashMap<MoveOnlyKey, MoveOnly, MoveOnlyKeyHash, MoveOnlyKeyEqual> MoveOnlyValues;
    MoveOnlyValues.Insert(MoveOnlyKey(4), MoveOnly(40));
    MoveOnlyValues.Insert(MoveOnlyKey(5), MoveOnly(50));
    MoveOnlyKey DuplicateKey(4);
    MoveOnly DuplicateValue(400);
    Failures += Expect(!MoveOnlyValues.Insert(std::move(DuplicateKey), std::move(DuplicateValue))
        && DuplicateKey.Value == 4 && DuplicateValue.Value == 400,
        "duplicate HashMap insertion does not consume rvalue arguments");
    MoveOnlyValues.Reserve(32);
    MoveOnlyKey Four(4);
    Failures += Expect(MoveOnlyValues.Find(Four) != nullptr
        && MoveOnlyValues.Find(Four)->Value == 40,
        "HashMap supports move-only keys and values across rehash");
    return Failures;
}

int RunHashMapCollisionTests()
{
    int Failures = 0;
    LE::HashMap<int, int, ConstantIntHash> Collisions;
    for (int Index = 0; Index < 200; ++Index) { Collisions.Insert(Index, Index + 1); }
    for (int Index = 0; Index < 200; ++Index)
    {
        Failures += Expect(Collisions.Find(Index) != nullptr && *Collisions.Find(Index) == Index + 1,
            "constant-hash probing finds every colliding key");
    }
    const std::size_t SizeBeforeDuplicate = Collisions.Size();
    Failures += Expect(!Collisions.Insert(50, -1) && Collisions.Size() == SizeBeforeDuplicate
        && *Collisions.Find(50) == 51,
        "duplicate colliding key neither grows nor replaces the entry");

    for (int Index = 0; Index < 120; ++Index) { Collisions.Erase(Index); }
    const std::size_t TombstonesBeforeReuse = Collisions.Tombstones();
    for (int Index = 200; Index < 280; ++Index) { Collisions.Insert(Index, Index + 1); }
    Failures += Expect(Collisions.Tombstones() < TombstonesBeforeReuse,
        "linear probing reuses tombstones before empty buckets");
    for (int Index = 120; Index < 280; ++Index)
    {
        Failures += Expect(Collisions.Contains(Index),
            "collision chain remains searchable across tombstone reuse");
    }

    LE::HashMap<int, int, ConstantIntHash> AllTombstones;
    AllTombstones.Reserve(5);
    for (int Index = 0; Index < 5; ++Index) { AllTombstones.Insert(Index, Index); }
    for (int Index = 0; Index < 5; ++Index) { AllTombstones.Erase(Index); }
    Failures += Expect(AllTombstones.Tombstones() == 5
        && AllTombstones.Find(999) == nullptr
        && AllTombstones.Insert(999, 1) && AllTombstones.Contains(999),
        "all-tombstone and adversarial probes terminate and reuse storage");
    AllTombstones.Clear();
    Failures += Expect(AllTombstones.IsEmpty() && AllTombstones.Tombstones() == 0,
        "HashMap Clear resets live entries and tombstones");
    return Failures;
}

int RunHashMapPolicyAndFailureTests()
{
    int Failures = 0;
    LE::HashMap<LookupKey, int, TransparentLookupHash, TransparentLookupEqual> Heterogeneous;
    Heterogeneous.Insert(LookupKey(8), 80);
    const LookupView Eight{8};
    Failures += Expect(Heterogeneous.Contains(Eight) && *Heterogeneous.Find(Eight) == 80
        && Heterogeneous.Erase(Eight) && Heterogeneous.IsEmpty(),
        "transparent hash/equality support Find Contains and Erase without key conversion");

    LE::HashMap<int, int, ModuloHash, ModuloEqual> Stateful(
        ModuloHash{10}, ModuloEqual{10});
    Failures += Expect(Stateful.Insert(1, 10) && !Stateful.Insert(11, 110)
        && Stateful.Contains(21) && *Stateful.Find(31) == 10,
        "HashMap stores and applies stateful hash and equality policies");

    FailingAllocator StatefulAllocator(false);
    {
        LE::HashMap<int, int, ModuloHash, ModuloEqual> StatefulWithAllocator(
            ModuloHash{7}, ModuloEqual{7}, LE::AllocatorRef(StatefulAllocator));
        StatefulWithAllocator.Insert(2, 20);
        Failures += Expect(&StatefulWithAllocator.GetAllocator() == &StatefulAllocator
            && StatefulWithAllocator.Contains(9),
            "stateful policies and exact allocator identity can be supplied together");
    }
    Failures += Expect(StatefulAllocator.LiveAllocations == 0,
        "stateful-policy HashMap frees through its supplied allocator");

    FailingAllocator Allocator(false);
    {
        LE::HashMap<int, int> Values(Allocator);
        for (int Index = 0; Index < 5; ++Index) { Values.Insert(Index, Index + 10); }
        const std::size_t OriginalBuckets = Values.BucketCount();
        Allocator.bFailAllocations = true;
        Failures += Expect(!Values.TryReserve(1000)
            && Values.Size() == 5 && Values.BucketCount() == OriginalBuckets
            && *Values.Find(2) == 12,
            "HashMap TryReserve allocation failure preserves the original map");
        Failures += Expect(Values.TryInsert(99, 99) == LE::EHashInsertResult::AllocationFailed
            && Values.Size() == 5 && !Values.Contains(99) && *Values.Find(4) == 14,
            "HashMap TryInsert growth failure preserves the original map");
        const std::size_t AttemptsBeforeOverflow = Allocator.AllocationAttempts;
        Failures += Expect(!Values.TryReserve((std::numeric_limits<std::size_t>::max)())
            && Allocator.AllocationAttempts == AttemptsBeforeOverflow,
            "HashMap rejects reserve overflow before allocator invocation");
    }
    Failures += Expect(Allocator.LiveAllocations == 0 && Allocator.DeallocationCount >= 1,
        "HashMap deallocates bucket storage through its original allocator identity");

    FailingAllocator RvalueAliasAllocator(false);
    {
        LE::HashMap<int, MoveOnly> Values(RvalueAliasAllocator);
        for (int Index = 0; Index < 5; ++Index) { Values.Insert(Index, MoveOnly(Index + 30)); }
        MoveOnly* const AliasedValue = Values.Find(0);
        RvalueAliasAllocator.bFailAllocations = true;
        Failures += Expect(Values.TryInsert(99, std::move(*AliasedValue))
                == LE::EHashInsertResult::AllocationFailed
            && Values.Size() == 5 && Values.Find(0)->Value == 30 && !Values.Contains(99),
            "failed HashMap growth does not move-from an internally aliased rvalue");
    }
    Failures += Expect(RvalueAliasAllocator.LiveAllocations == 0,
        "rvalue-alias failure test retains the allocator ownership route");
    return Failures;
}

int RunHashSetParityTests()
{
    int Failures = 0;
    LE::HashSet<int, ConstantIntHash> Values;
    Values.Reserve(64);
    for (int Index = 0; Index < 40; ++Index) { Values.Insert(Index); }
    Failures += Expect(!Values.Insert(10) && Values.Size() == 40,
        "HashSet rejects a duplicate key under extreme collision");
    for (int Index = 0; Index < 20; ++Index) { Values.Erase(Index); }
    for (int Index = 40; Index < 60; ++Index) { Values.Insert(Index); }
    bool Seen[60]{};
    std::size_t Iterated = 0;
    for (const int Key : Values)
    {
        Failures += Expect(Key >= 20 && Key < 60 && !Seen[Key],
            "HashSet iteration visits unique live keys without order assumptions");
        if (Key >= 0 && Key < 60) { Seen[Key] = true; }
        ++Iterated;
    }
    Failures += Expect(Iterated == 40 && Values.LoadFactor() <= Values.MaxLoadFactor(),
        "HashSet matches HashMap size load and iteration contracts");
    Values.Rehash(256);
    for (int Index = 20; Index < 60; ++Index)
    {
        Failures += Expect(Values.Contains(Index), "HashSet Rehash preserves every live key");
    }
    Values.Clear();
    Failures += Expect(Values.IsEmpty() && Values.Tombstones() == 0,
        "HashSet Clear resets storage state");
    return Failures;
}

int RunStringTests()
{
    int Failures = 0;

    LE::String Empty;
    const LE::StringView NullView;
    const LE::StringView EmptyPointerView("", 0);
    Failures += Expect(Empty.IsEmpty() && Empty.Data() != nullptr && Empty.Data()[0] == '\0'
        && NullView == EmptyPointerView && Empty == NullView,
        "String and null/empty StringView have consistent empty semantics");

    const char EmbeddedBytes[] = {'a', '\0', 'b'};
    LE::String Embedded(EmbeddedBytes, sizeof(EmbeddedBytes));
    Failures += Expect(Embedded.Size() == 3 && Embedded[1] == '\0' && Embedded.Data()[3] == '\0',
        "String preserves embedded NUL bytes and adds a trailing terminator");

    LE::String SelfAppend("0123456789abcdef");
    const LE::StringView AliasedSuffix = SelfAppend.View().Substr(2, 10);
    SelfAppend.Append(AliasedSuffix);
    Failures += Expect(SelfAppend == LE::StringView("0123456789abcdef23456789ab"),
        "String stages self-aliased append across growth");

    LE::String Overlap("abcdef");
    Overlap.Reserve(32);
    Overlap.Append(Overlap.View().Substr(1, 4));
    Failures += Expect(Overlap == LE::StringView("abcdefbcde"),
        "String uses overlap-safe append without growth");
    Overlap.Assign(Overlap.View().Substr(2, 5));
    Failures += Expect(Overlap == LE::StringView("cdefb"),
        "String uses overlap-safe assign");

    const LE::String EqualEmbedded(EmbeddedBytes, sizeof(EmbeddedBytes));
    Failures += Expect(LE::StringHash{}(Embedded) == LE::StringHash{}(EqualEmbedded.View())
        && LE::StringEqual{}(Embedded, EqualEmbedded.View()),
        "String and StringView content hash/equality agree for embedded NUL");

    const char LowerEmbeddedBytes[] = {'a', '\0', 'a'};
    const LE::String LowerEmbedded(LowerEmbeddedBytes, sizeof(LowerEmbeddedBytes));
    Failures += Expect(LowerEmbedded < Embedded
        && LE::StringView("a") < LowerEmbedded.View()
        && Embedded.View().Compare(EqualEmbedded.View()) == 0,
        "String byte ordering is embedded-NUL safe and shorter-prefix first");

    LE::HashMap<LE::String, int> Names;
    Names.Insert(LE::String("alpha"), 7);
    Failures += Expect(Names.Contains(LE::StringView("alpha")) && Names.Contains("alpha")
        && *Names.Find(LE::StringView("alpha")) == 7,
        "String default hash/equality support allocation-free heterogeneous lookup");

    FailingAllocator Allocator(false);
    {
        LE::String Preserved(Allocator);
        Preserved.Assign("stable");
        Allocator.bFailAllocations = true;
        Failures += Expect(!Preserved.TryAppend(LE::StringView("-a-suffix-that-forces-growth"))
            && Preserved == LE::StringView("stable"),
            "String TryAppend allocation failure preserves the original value");
        const std::size_t AttemptsBeforeOverflow = Allocator.AllocationAttempts;
        const LE::StringView Impossible("x", (std::numeric_limits<std::size_t>::max)());
        Failures += Expect(!Preserved.TryAssign(Impossible)
            && Preserved == LE::StringView("stable")
            && Allocator.AllocationAttempts == AttemptsBeforeOverflow,
            "String rejects SIZE_MAX assignment before allocation or source access");
    }
    Failures += Expect(Allocator.LiveAllocations == 0,
        "String frees storage through its original allocator identity");

    return Failures;
}

int RunUuidTests()
{
    int Failures = 0;
    using FStableId = LE::TStableId<FStableIdTestDomain>;
    using FOtherStableId = LE::TStableId<FOtherStableIdTestDomain>;

    static_assert(sizeof(LE::FUuid) == LE::FUuid::ByteCount, "UUID storage must be exactly 16 bytes");
    static_assert(std::is_standard_layout<LE::FUuid>::value, "UUID must have standard layout");
    static_assert(std::is_trivially_copyable<LE::FUuid>::value, "UUID must be trivially copyable");
    static_assert(!std::is_constructible<FOtherStableId, FStableId>::value,
        "stable ID domains must not implicitly cross");

    const LE::StringView Canonical("00112233-4455-4677-8899-aabbccddeeff");
    LE::FUuid Parsed;
    Failures += Expect(LE::FUuid::TryParse(Canonical, Parsed)
        && Parsed.IsValid()
        && Parsed.GetVersion() == 4
        && Parsed.HasRfcVariant()
        && Parsed.ToString() == Canonical,
        "FUuid parses and formats the strict canonical RFC byte order");

    LE::FUuid Copied = Parsed;
    Failures += Expect(Copied == Parsed && !(Copied < Parsed) && !(Parsed < Copied)
        && LE::FUuidHash{}(Copied) == LE::DefaultHash<LE::FUuid>{}(Parsed),
        "FUuid copy comparison ordering and hashing are value-based");

    LE::HashMap<LE::FUuid, int> UuidValues;
    UuidValues.Insert(Parsed, 42);
    Failures += Expect(UuidValues.Contains(Copied) && *UuidValues.Find(Copied) == 42,
        "FUuid is a default-hash key across the public Core API");

    LE::FUuid Nil;
    Failures += Expect(LE::FUuid::TryParse("00000000-0000-0000-0000-000000000000", Nil)
        && !Nil.IsValid()
        && Nil.ToString() == LE::StringView("00000000-0000-0000-0000-000000000000"),
        "FUuid accepts nil syntax while preserving nil as the invalid sentinel");

    const LE::StringView InvalidSpellings[] = {
        "00112233-4455-4677-8899-aabbccddeef",
        "00112233-4455-4677-8899-aabbccddeeff0",
        "00112233_4455-4677-8899-aabbccddeeff",
        "00112233-4455-4677-8899-Aabbccddeeff",
        "00112233-4455-4677-8899-aabbccddegff",
    };
    for (const LE::StringView Invalid : InvalidSpellings)
    {
        LE::FUuid Preserved = Parsed;
        Failures += Expect(!LE::FUuid::TryParse(Invalid, Preserved) && Preserved == Parsed,
            "FUuid rejects non-canonical input without changing the output value");
    }

    LE::HashSet<LE::FUuid> GeneratedValues;
    bool bGeneratedContractHolds = true;
    for (int Index = 0; Index < 64; ++Index)
    {
        LE::FUuid Generated = Parsed;
        if (!LE::FUuid::TryGenerate(Generated)
            || !Generated.IsValid()
            || Generated.GetVersion() != 4
            || !Generated.HasRfcVariant()
            || !GeneratedValues.Insert(Generated))
        {
            bGeneratedContractHolds = false;
            break;
        }
        LE::FUuid RoundTripped;
        if (!LE::FUuid::TryParse(Generated.ToString(), RoundTripped) || RoundTripped != Generated)
        {
            bGeneratedContractHolds = false;
            break;
        }
    }
    Failures += Expect(bGeneratedContractHolds && GeneratedValues.Size() == 64,
        "OS-generated UUIDs are unique in the sample and satisfy RFC v4 and variant bits");

    FStableId Stable;
    Failures += Expect(FStableId::TryParse(Canonical, Stable)
        && Stable.IsValid()
        && Stable.ToString() == Canonical,
        "TStableId preserves UUID value semantics for one compile-time domain");
    FStableId PreservedStable = Stable;
    Failures += Expect(!FStableId::TryParse("00000000-0000-0000-0000-000000000000", PreservedStable)
        && PreservedStable == Stable,
        "TStableId rejects nil without changing the output ID");
    FStableId GeneratedStable;
    Failures += Expect(FStableId::TryGenerate(GeneratedStable) && GeneratedStable.IsValid(),
        "TStableId generates a valid UUID-backed domain value");
    LE::HashMap<FStableId, int> StableValues;
    StableValues.Insert(Stable, 7);
    Failures += Expect(StableValues.Contains(FStableId::FromUuid(Parsed))
        && *StableValues.Find(FStableId::FromUuid(Parsed)) == 7,
        "TStableId supplies domain-specific default hashing");

    return Failures;
}

int RunRuntimeHandleTests()
{
    int Failures = 0;
    using FHandle = LE::TRuntimeHandle<FRuntimeHandleTestDomain>;
    using FOtherHandle = LE::TRuntimeHandle<FOtherRuntimeHandleTestDomain>;
    using FPool = LE::TRuntimeHandlePool<FRuntimeHandleTestDomain>;

    static_assert(sizeof(FHandle) == sizeof(LE::uint32) * 2,
        "default runtime handles must be a fixed 64-bit pair");
    static_assert(std::is_trivially_copyable<FHandle>::value,
        "runtime handles must be trivially copyable values");
    static_assert(!std::is_constructible<FOtherHandle, FHandle>::value,
        "runtime handle domains must not implicitly cross");
    static_assert(!std::is_copy_constructible<FPool>::value && !std::is_move_constructible<FPool>::value,
        "runtime handle pools must preserve one issuance domain");

    FHandle Invalid;
    Failures += Expect(!Invalid.IsValid(), "default runtime handle is invalid");

    FPool Pool;
    FHandle First;
    FHandle Second;
    Failures += Expect(Pool.TryAllocate(First) && Pool.TryAllocate(Second)
        && First.IsValid() && Second.IsValid()
        && First.Index == 0 && First.Generation == 1
        && Second.Index == 1 && Second.Generation == 1
        && Pool.IsAlive(First) && Pool.IsAlive(Second)
        && Pool.GetSlotCount() == 2 && Pool.GetAliveCount() == 2,
        "runtime handle pool issues live index-generation pairs");

    const FHandle ForgedGeneration{ First.Index, static_cast<LE::uint32>(First.Generation + 1) };
    const FHandle OutOfRange{ 99, 1 };
    Failures += Expect(!Pool.IsAlive(ForgedGeneration)
        && !Pool.IsAlive(OutOfRange)
        && !Pool.Release(Invalid)
        && !Pool.Release(ForgedGeneration),
        "runtime handle validation rejects invalid forged and out-of-range values");

    Failures += Expect(Pool.Release(First) && !Pool.IsAlive(First) && !Pool.Release(First),
        "runtime handle release invalidates stale and double-release values");
    FHandle Reused;
    Failures += Expect(Pool.TryAllocate(Reused)
        && Reused.Index == First.Index
        && Reused.Generation == First.Generation + 1
        && Pool.IsAlive(Reused)
        && !Pool.IsAlive(First),
        "runtime handle slot reuse increments generation and keeps the prior handle stale");

    LE::HashMap<FHandle, int> HandleValues;
    HandleValues.Insert(Reused, 23);
    Failures += Expect(HandleValues.Contains(Reused) && *HandleValues.Find(Reused) == 23
        && LE::DefaultHash<FHandle>{}(Reused) != LE::DefaultHash<FHandle>{}(First),
        "runtime handle hashing includes both index and generation");

    using FSmallGenerationHandle = LE::TRuntimeHandle<FRuntimeHandleTestDomain, LE::uint32, LE::uint8>;
    LE::TRuntimeHandlePool<FRuntimeHandleTestDomain, LE::uint32, LE::uint8> ExhaustionPool;
    FSmallGenerationHandle Earliest;
    bool bGenerationSequenceValid = ExhaustionPool.TryAllocate(Earliest)
        && Earliest.Index == 0 && Earliest.Generation == 1
        && ExhaustionPool.Release(Earliest);
    for (unsigned int Expected = 2; Expected <= 255 && bGenerationSequenceValid; ++Expected)
    {
        FSmallGenerationHandle Current;
        bGenerationSequenceValid = ExhaustionPool.TryAllocate(Current)
            && Current.Index == 0
            && Current.Generation == static_cast<LE::uint8>(Expected)
            && ExhaustionPool.Release(Current);
    }
    FSmallGenerationHandle AfterRetirement;
    bGenerationSequenceValid = bGenerationSequenceValid
        && ExhaustionPool.TryAllocate(AfterRetirement)
        && AfterRetirement.Index == 1
        && AfterRetirement.Generation == 1
        && !ExhaustionPool.IsAlive(Earliest)
        && ExhaustionPool.GetSlotCount() == 2;
    Failures += Expect(bGenerationSequenceValid,
        "generation exhaustion permanently retires the slot instead of wrapping into ABA");

    using FSmallIndexHandle = LE::TRuntimeHandle<FRuntimeHandleTestDomain, LE::uint8, LE::uint8>;
    LE::TRuntimeHandlePool<FRuntimeHandleTestDomain, LE::uint8, LE::uint8> IndexPool;
    LE::Array<FSmallIndexHandle> Issued;
    bool bIndexSequenceValid = true;
    for (unsigned int Index = 0; Index < 255; ++Index)
    {
        FSmallIndexHandle Handle;
        if (!IndexPool.TryAllocate(Handle)
            || Handle.Index != static_cast<LE::uint8>(Index)
            || Handle.Generation != 1)
        {
            bIndexSequenceValid = false;
            break;
        }
        Issued.PushBack(Handle);
    }
    FSmallIndexHandle Preserved{ 7, 9 };
    bIndexSequenceValid = bIndexSequenceValid
        && !IndexPool.TryAllocate(Preserved)
        && Preserved == FSmallIndexHandle{ 7, 9 }
        && IndexPool.GetSlotCount() == 255
        && IndexPool.GetAliveCount() == 255;
    Failures += Expect(bIndexSequenceValid,
        "runtime handle index sentinel is never issued and exhaustion preserves the output value");

    return Failures;
}

int RunOwnershipTests()
{
    int Failures = 0;
    FailingAllocator Allocator(false);
    OwnedValue::LiveCount = 0;
    OwnedValue::DestroyedCount = 0;
    {
        LE::UniquePtr<OwnedValue> Owner = LE::MakeUniqueWithAllocator<OwnedValue>(Allocator, 17);
        LE::UniquePtr<OwnedValue> Moved(std::move(Owner));
        Failures += Expect(!Owner && Moved && Moved->Value == 17 && OwnedValue::LiveCount == 1,
            "UniquePtr move transfers its creator-side destruction route");
    }
    Failures += Expect(OwnedValue::LiveCount == 0 && OwnedValue::DestroyedCount == 1
        && Allocator.LiveAllocations == 0,
        "UniquePtr destroys once and deallocates through the creator allocator");

    int UniqueAdoptCalls = 0;
    {
        LE::UniquePtr<OwnedValue> Adopted = LE::UniquePtr<OwnedValue>::Adopt(
            new OwnedValue(23), &UniqueAdoptCalls, &DestroyAdoptedOwned);
        Failures += Expect(Adopted && Adopted->Value == 23,
            "UniquePtr adopts an explicit creator-side custom destroy route");
    }
    Failures += Expect(UniqueAdoptCalls == 1,
        "UniquePtr invokes its adopted destroy route exactly once");

    SharedDerived::LiveCount = 0;
    SharedDerived::DestroyedCount = 0;
    LE::WeakPtr<ObservedBase> Observer;
    {
        LE::SharedPtr<SharedDerived> Derived = LE::MakeSharedWithAllocator<SharedDerived>(Allocator, 41);
        LE::SharedPtr<const ObservedBase> ConstBase = Derived;
        LE::SharedPtr<ObservedBase> Base = Derived;
        Observer = LE::WeakPtr<ObservedBase>(Base);
        LE::SharedPtr<ObservedBase> Locked = Observer.Lock();
        Failures += Expect(Derived.UseCount() == 4 && Locked && Locked->Value == 41
            && static_cast<const void*>(Base.Get()) != static_cast<const void*>(Derived.Get()),
            "SharedPtr conversion keeps an adjusted base pointer and shared control block");
    }
    Failures += Expect(Observer.IsExpired() && !Observer.Lock()
        && SharedDerived::LiveCount == 0 && SharedDerived::DestroyedCount == 1,
        "WeakPtr fails to lock after the last strong owner and object destruction occurs once");
    Observer.Reset();
    Failures += Expect(Allocator.LiveAllocations == 0,
        "SharedPtr control block remains until weak release then returns through creator allocator");

    int DeleteCalls = 0;
    OwnedValue::DestroyedCount = 0;
    {
        StatefulOwnedDeleter Deleter{&DeleteCalls};
        LE::SharedPtr<OwnedValue> Adopted = LE::AdoptShared(
            new OwnedValue(9), Allocator, Deleter);
        Failures += Expect(Adopted && Adopted->Value == 9,
            "SharedPtr accepts a stateful lvalue custom deleter");
    }
    Failures += Expect(DeleteCalls == 1 && OwnedValue::DestroyedCount == 1
        && Allocator.LiveAllocations == 0,
        "SharedPtr invokes its creator-side custom deleter exactly once");

    FailingAllocator FailureAllocator;
    LE::UniquePtr<OwnedValue> FailedUnique = LE::TryMakeUniqueWithAllocator<OwnedValue>(FailureAllocator, 1);
    LE::SharedPtr<OwnedValue> FailedShared = LE::TryMakeSharedWithAllocator<OwnedValue>(FailureAllocator, 1);
    Failures += Expect(!FailedUnique && !FailedShared && FailureAllocator.LiveAllocations == 0,
        "TryMake ownership factories report allocation failure without a partial owner");

    OwnedValue* const StillOwnedByCaller = new OwnedValue(5);
    StatefulOwnedDeleter FailureDeleter{&DeleteCalls};
    LE::SharedPtr<OwnedValue> FailedAdoption = LE::TryAdoptShared(
        StillOwnedByCaller, FailureAllocator, FailureDeleter);
    Failures += Expect(!FailedAdoption && OwnedValue::LiveCount == 1,
        "TryAdoptShared failure leaves the raw object owned by the caller");
    delete StillOwnedByCaller;

    return Failures;
}

int RunFunctionTests()
{
    int Failures = 0;
    FailingAllocator Allocator(false);
    {
        LE::Function<int(int)> Add(Allocator, AddCallable{5});
        LE::Function<int(int)> Copy(Add);
        LE::Function<int(int)> Moved(std::move(Copy));
        Failures += Expect(Add(2) == 7 && Moved(3) == 8 && !Copy,
            "Function copies and moves allocator-backed callable state");

        Allocator.bFailAllocations = true;
        Failures += Expect(!Add.TryAssign(AddCallable{100}) && Add(1) == 6,
            "Function TryAssign allocation failure preserves the original callable");
    }
    Failures += Expect(Allocator.LiveAllocations == 0,
        "Function destroys callable storage through its original allocator route");
    return Failures;
}

int RunMathVectorTests()
{
    int Failures = 0;
    using LE::Math::Vector2f;
    using LE::Math::Vector3d;
    using LE::Math::Vector3f;

    Vector3f Value(1.0f, 2.0f, 3.0f);
    Failures += Expect(Value[0] == 1.0f && Value[1] == 2.0f && Value[2] == 3.0f
        && Value.Data()[0] == 1.0f,
        "Vector construction indexing and first component are valid");

    const Vector3f Other(4.0f, -2.0f, 0.5f);
    const Vector3f Sum = Value + Other;
    const Vector3f Difference = Value - Other;
    const Vector3f Product = Value * Other;
    Failures += Expect(Sum == Vector3f(5.0f, 0.0f, 3.5f)
        && Difference == Vector3f(-3.0f, 4.0f, 2.5f)
        && Product == Vector3f(4.0f, -4.0f, 1.5f)
        && (Value * 2.0f) == Vector3f(2.0f, 4.0f, 6.0f),
        "Vector arithmetic scalar and component-wise products are explicit");

    const Vector3f BasisX(1.0f, 0.0f, 0.0f);
    const Vector3f BasisY(0.0f, 1.0f, 0.0f);
    const Vector3f BasisZ = Vector3f::Cross(BasisX, BasisY);
    Failures += Expect(BasisZ == Vector3f(0.0f, 0.0f, 1.0f)
        && Vector3f::Dot(BasisX, BasisY) == 0.0f,
        "Vector cross and dot lock the right-handed basis convention");

    const Vector3d Normalized = Vector3d(3.0, 4.0, 0.0).Normalized();
    Vector3d Zero;
    Failures += Expect(NearlyEqual(Normalized[0], 0.6, 1.0e-12)
        && NearlyEqual(Normalized[1], 0.8, 1.0e-12)
        && NearlyEqual(Normalized.Length(), 1.0, 1.0e-12)
        && !Zero.Normalize() && Zero == Vector3d(),
        "Vector normalization is precise and maps zero to zero");

    const Vector2f Direction(2.0f, 3.0f);
    Failures += Expect(Direction.Perp() == Vector2f(3.0f, -2.0f)
        && Direction.PerpCCW() == Vector2f(-3.0f, 2.0f),
        "Vector clockwise and counter-clockwise perpendiculars differ correctly");
    return Failures;
}

int RunMathMatrixTests()
{
    int Failures = 0;
    using Matrix23f = LE::Math::Matrix<float, 2, 3>;
    using Matrix32f = LE::Math::Matrix<float, 3, 2>;
    using Matrix22f = LE::Math::Matrix<float, 2, 2>;
    using LE::Math::Matrix3f;
    using LE::Math::Vector3f;

    const Matrix23f Left(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f);
    const Matrix32f Right(7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f);
    const Matrix22f RectangularProduct = Left * Right;
    Failures += Expect(RectangularProduct(0, 0) == 58.0f
        && RectangularProduct(0, 1) == 64.0f
        && RectangularProduct(1, 0) == 139.0f
        && RectangularProduct(1, 1) == 154.0f,
        "Rectangular matrix multiplication uses row by column products");

    const auto Transposed = Left.Transposed();
    Failures += Expect(Transposed(0, 0) == 1.0f && Transposed(0, 1) == 4.0f
        && Transposed(2, 0) == 3.0f && Transposed(2, 1) == 6.0f,
        "Rectangular matrix transpose exchanges rows and columns");

    const LE::Math::Vector2f Transformed = Left * Vector3f(1.0f, 2.0f, 3.0f);
    Failures += Expect(Transformed[0] == 14.0f && Transformed[1] == 32.0f,
        "Matrix multiplies a column vector on its right");

    Matrix3f Scale = Matrix3f::Identity();
    Scale(0, 0) = 2.0f;
    Scale(1, 1) = 3.0f;
    Matrix3f Translation = Matrix3f::Identity();
    Translation(0, 2) = 5.0f;
    Translation(1, 2) = 7.0f;
    const Vector3f Point(1.0f, 1.0f, 1.0f);
    const Vector3f Composed = (Translation * Scale) * Point;
    Failures += Expect(Composed == Vector3f(7.0f, 10.0f, 1.0f)
        && Matrix3f::Identity() * Point == Point,
        "Matrix A times B applies B first then A and identity is neutral");

    Failures += Expect(Left.Data()[0] == 1.0f && Left.Data()[1] == 2.0f
        && Left.Data()[3] == 4.0f,
        "Matrix storage is contiguous row-major");
    return Failures;
}

int RunMathQuaternionTests()
{
    int Failures = 0;
    using LE::Math::Quaterniond;
    using LE::Math::Vector3d;
    constexpr double HalfPi = 1.57079632679489661923;
    constexpr double Tolerance = 1.0e-12;

    const Quaterniond Identity;
    Failures += Expect(Identity[0] == 0.0 && Identity[1] == 0.0
        && Identity[2] == 0.0 && Identity[3] == 1.0,
        "Quaternion default and indexed order are x y z w identity");

    const Quaterniond RotateZ = Quaterniond::FromAxisAngle(Vector3d(0.0, 0.0, 2.0), HalfPi);
    const Vector3d RotatedX = RotateZ.RotateVector(Vector3d(1.0, 0.0, 0.0));
    Failures += Expect(NearlyEqual(RotatedX[0], 0.0, Tolerance)
        && NearlyEqual(RotatedX[1], 1.0, Tolerance)
        && NearlyEqual(RotatedX[2], 0.0, Tolerance),
        "Quaternion radians and positive right-handed rotation map X to Y around positive Z");

    const Quaterniond RotateX = Quaterniond::FromAxisAngle(Vector3d(1.0, 0.0, 0.0), HalfPi);
    const Quaterniond Composed = RotateZ * RotateX;
    const Vector3d Source(0.0, 1.0, 0.0);
    const Vector3d Sequential = RotateZ.RotateVector(RotateX.RotateVector(Source));
    const Vector3d Combined = Composed.RotateVector(Source);
    Failures += Expect(NearlyEqual(Combined[0], Sequential[0], Tolerance)
        && NearlyEqual(Combined[1], Sequential[1], Tolerance)
        && NearlyEqual(Combined[2], Sequential[2], Tolerance),
        "Hamilton quaternion product applies the right operand first");

    const Quaterniond NonUnit(
        RotateZ.x * 2.0, RotateZ.y * 2.0, RotateZ.z * 2.0, RotateZ.w * 2.0);
    const Quaterniond InverseProduct = NonUnit * NonUnit.Inverse();
    Failures += Expect(NearlyEqual(InverseProduct.x, 0.0, Tolerance)
        && NearlyEqual(InverseProduct.y, 0.0, Tolerance)
        && NearlyEqual(InverseProduct.z, 0.0, Tolerance)
        && NearlyEqual(InverseProduct.w, 1.0, Tolerance),
        "Quaternion inverse divides conjugate by norm squared");

    Quaterniond Zero(0.0, 0.0, 0.0, 0.0);
    Failures += Expect(!Zero.Normalize() && Zero == Quaterniond::Identity()
        && Quaterniond::FromAxisAngle(Vector3d(), HalfPi) == Quaterniond::Identity(),
        "Quaternion normalization and zero axis-angle use identity fallback");
    return Failures;
}

int RunMathLayoutTests()
{
    static_assert(std::is_standard_layout<LE::Math::Vector3f>::value,
        "Vector must be standard-layout");
    static_assert(std::is_trivially_copyable<LE::Math::Vector3f>::value,
        "Vector must be trivially copyable");
    static_assert(std::is_standard_layout<LE::Math::Matrix4f>::value,
        "Matrix must be standard-layout");
    static_assert(std::is_trivially_copyable<LE::Math::Matrix4f>::value,
        "Matrix must be trivially copyable");
    static_assert(std::is_standard_layout<LE::Math::Quaternionf>::value,
        "Quaternion must be standard-layout");
    static_assert(std::is_trivially_copyable<LE::Math::Quaternionf>::value,
        "Quaternion must be trivially copyable");
    static_assert(sizeof(LE::Math::Vector3f) == sizeof(float) * 3,
        "Vector has no implicit SIMD padding");
    static_assert(sizeof(LE::Math::Matrix4f) == sizeof(float) * 16,
        "Matrix has no implicit SIMD padding");
    static_assert(sizeof(LE::Math::Quaternionf) == sizeof(float) * 4,
        "Quaternion has no implicit SIMD padding");
    static_assert(alignof(LE::Math::Vector3f) == alignof(float),
        "Vector uses natural scalar alignment");
    static_assert(alignof(LE::Math::Matrix4d) == alignof(double),
        "Matrix uses natural scalar alignment");
    static_assert(alignof(LE::Math::Quaterniond) == alignof(double),
        "Quaternion uses natural scalar alignment");

    const LE::Math::Vector<float, 3> CanonicalVector(1.0f, 2.0f, 3.0f);
    const LE::Math::Matrix3f CanonicalMatrix = LE::Math::Matrix3f::Identity();
    const LE::Math::Quaternionf CanonicalQuaternion = LE::Math::Quaternionf::Identity();
    return Expect(CanonicalVector[0] == 1.0f && CanonicalMatrix[1][1] == 1.0f
        && CanonicalQuaternion.w == 1.0f,
        "Canonical LE::Math types are directly consumable");
}

bool ExplicitFrameEventsEqual(
    const LE::Array<int>& Events,
    const int* const Expected,
    const std::size_t Count)
{
    if (Events.Size() != Count)
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        if (Events[Index] != Expected[Index])
        {
            return false;
        }
    }
    return true;
}

int RunExplicitFrameLifecycleTests()
{
    int Failures = 0;

    {
        FFakeRALTexture Texture;
        FFakeRALTextureView View(&Texture);
        Failures += Expect(
            LE::FRALAcquireResult{ LE::ERALSwapchainStatus::Success, &View }.HasImage() &&
                LE::FRALAcquireResult{ LE::ERALSwapchainStatus::Suboptimal, &View }.HasImage() &&
                !LE::FRALAcquireResult{ LE::ERALSwapchainStatus::OutOfDate, nullptr }.HasImage() &&
                !LE::FRALAcquireResult{ LE::ERALSwapchainStatus::Error, nullptr }.HasImage() &&
                !LE::FRALAcquireResult{ LE::ERALSwapchainStatus::Success, nullptr }.HasImage(),
            "Acquire result exposes images only for successful and suboptimal acquisitions");
    }

    {
        FFakeRALCommandList CommandList;
        FFakeRALSemaphore Semaphore;
        FFakeRALFence Fence;
        LE::FRALSubmitInfo EmptySubmitInfo;
        LE::FRALSubmitInfo NullWaitSubmitInfo;
        NullWaitSubmitInfo.CmdList = &CommandList;
        NullWaitSubmitInfo.WaitSemaphores.PushBack(nullptr);
        LE::FRALSubmitInfo ValidSubmitInfo;
        ValidSubmitInfo.CmdList = &CommandList;
        ValidSubmitInfo.WaitSemaphores.PushBack(&Semaphore);
        ValidSubmitInfo.SignalSemaphores.PushBack(&Semaphore);
        ValidSubmitInfo.FenceToSignal = &Fence;
        Failures += Expect(
            !EmptySubmitInfo.IsStructurallyValid() &&
                !NullWaitSubmitInfo.IsStructurallyValid() &&
                ValidSubmitInfo.IsStructurallyValid(),
            "Submit info rejects empty and null synchronization entries without silently filtering");
    }

    {
        FRenderFrameSchedulerFixture Fixture;
        const bool bInitialized = Fixture.Initialize();
        const LE::FRenderFrameResult Result = Fixture.Execute();
        const int ExpectedEvents[] = { 1, 2, 3, 4, 5, 6, 7 };
        Failures += Expect(
            bInitialized && Result.Action == LE::ERenderFrameAction::Continue &&
                Result.RecordResult == LE::ERenderFrameRecordResult::Success &&
                Result.AcquireStatus == LE::ERALSwapchainStatus::Success &&
                Result.SubmitResult == LE::ERALQueueSubmitResult::Success &&
                Result.PresentStatus == LE::ERALSwapchainStatus::Success &&
                Result.FrameIndex == 0 && Result.SlotIndex == 0 &&
                ExplicitFrameEventsEqual(Fixture.Events, ExpectedEvents, 7) &&
                Fixture.Device.Fences[0]->WaitCount == 1 && Fixture.Device.Fences[0]->ResetCount == 1 &&
                Fixture.Device.Allocators[0]->ResetCount == 1 && Fixture.RecordCount == 1 &&
                Fixture.Device.Queue.SubmitCount == 1 && Fixture.Device.Queue.WaitIdleCount == 0 &&
                Fixture.Swapchain.PresentCount == 1 &&
                Fixture.CapturedBackBufferView == &Fixture.View &&
                Fixture.Device.Queue.CapturedSubmitInfo.CmdList == Fixture.CapturedCommandList &&
                Fixture.Device.Queue.CapturedSubmitInfo.WaitSemaphores.Size() == 1 &&
                Fixture.Device.Queue.CapturedSubmitInfo.WaitSemaphores[0] == Fixture.Device.Semaphores[0] &&
                Fixture.Device.Queue.CapturedSubmitInfo.SignalSemaphores.Size() == 1 &&
                Fixture.Device.Queue.CapturedSubmitInfo.SignalSemaphores[0] == Fixture.Device.Semaphores[1] &&
                Fixture.Device.Queue.CapturedSubmitInfo.FenceToSignal == Fixture.Device.Fences[0] &&
                Fixture.Swapchain.CapturedAcquireSemaphore == Fixture.Device.Semaphores[0] &&
                Fixture.Swapchain.CapturedPresentSemaphore == Fixture.Device.Semaphores[1] &&
                Fixture.Swapchain.GetCurrentBackBufferView() == nullptr,
            "Scheduler owns exact wait reset acquire record submit present synchronization");
    }

    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Initialize();
        Fixture.Swapchain.AcquireStatus = LE::ERALSwapchainStatus::Suboptimal;
        const LE::FRenderFrameResult Result = Fixture.Execute();
        Failures += Expect(
            Result.Action == LE::ERenderFrameAction::RecreateSwapchain &&
                Fixture.RecordCount == 1 && Fixture.Device.Queue.SubmitCount == 1 &&
                Fixture.Swapchain.PresentCount == 1,
            "Suboptimal acquire renders and presents the valid image before requesting recreation");
    }

    for (const LE::ERALSwapchainStatus AcquireStatus : {
        LE::ERALSwapchainStatus::OutOfDate,
        LE::ERALSwapchainStatus::Error })
    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Initialize();
        Fixture.Swapchain.AcquireStatus = AcquireStatus;
        const LE::FRenderFrameResult Result = Fixture.Execute();
        const int AcquireCountBeforeIgnoredRetry = Fixture.Swapchain.AcquireCount;
        const LE::FRenderFrameResult IgnoredRetry = Fixture.Execute();
        Failures += Expect(
            Result.Action == (AcquireStatus == LE::ERALSwapchainStatus::OutOfDate
                ? LE::ERenderFrameAction::RecreateSwapchain
                : LE::ERenderFrameAction::Exit) &&
                Fixture.Device.Fences[0]->ResetCount == 0 &&
                Fixture.RecordCount == 0 && Fixture.Device.Queue.SubmitCount == 0 &&
                Fixture.Swapchain.PresentCount == 0 &&
                Fixture.Swapchain.GetCurrentBackBufferView() == nullptr &&
                (AcquireStatus == LE::ERALSwapchainStatus::OutOfDate ||
                    (IgnoredRetry.Action == LE::ERenderFrameAction::Exit &&
                     Fixture.Swapchain.AcquireCount == AcquireCountBeforeIgnoredRetry)),
            "Acquire without an image skips record submit and present");
    }

    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Initialize();
        Fixture.Swapchain.AcquireStatus = LE::ERALSwapchainStatus::Success;
        Fixture.Swapchain.View = nullptr;
        const LE::FRenderFrameResult Result = Fixture.Execute();
        const int AcquireCount = Fixture.Swapchain.AcquireCount;
        const LE::FRenderFrameResult IgnoredRetry = Fixture.Execute();
        Failures += Expect(
            Result.Action == LE::ERenderFrameAction::Exit &&
                IgnoredRetry.Action == LE::ERenderFrameAction::Exit &&
                Fixture.Swapchain.AcquireCount == AcquireCount &&
                Fixture.RecordCount == 0 && Fixture.Device.Queue.SubmitCount == 0,
            "Successful status without an image is terminal and ignored retries perform no work");
    }

    for (const LE::ERALQueueSubmitResult SubmitResult : {
        LE::ERALQueueSubmitResult::InvalidArguments,
        LE::ERALQueueSubmitResult::Error })
    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Initialize();
        Fixture.Device.Queue.Result = SubmitResult;
        const LE::FRenderFrameResult Result = Fixture.Execute();
        const int WaitCount = Fixture.Device.Fences[0]->WaitCount;
        const int AcquireCount = Fixture.Swapchain.AcquireCount;
        const LE::FRenderFrameResult IgnoredRetry = Fixture.Execute();
        Failures += Expect(
            Result.Action == LE::ERenderFrameAction::Exit && IgnoredRetry.Action == LE::ERenderFrameAction::Exit &&
                Fixture.RecordCount == 1 && Fixture.Device.Fences[0]->ResetCount == 1 &&
                Fixture.Device.Fences[0]->WaitCount == WaitCount &&
                Fixture.Device.Queue.SubmitCount == 1 && Fixture.Swapchain.PresentCount == 0 &&
                Fixture.Swapchain.AcquireCount == AcquireCount,
            "Submit failure is terminal without present, reacquire, or another unsignaled fence wait");
        Fixture.Scheduler.Shutdown();
        Failures += Expect(Fixture.Device.Queue.WaitIdleCount == 1,
            "Terminal submit failure uses exceptional queue idle instead of waiting its reset fence");
    }

    for (const LE::ERenderFrameRecordResult RecordResult : {
        LE::ERenderFrameRecordResult::InvalidArguments,
        LE::ERenderFrameRecordResult::Error })
    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Initialize();
        Fixture.RecordResult = RecordResult;
        const LE::FRenderFrameResult Result = Fixture.Execute();
        const int AcquireCount = Fixture.Swapchain.AcquireCount;
        const LE::FRenderFrameResult IgnoredRetry = Fixture.Execute();
        Failures += Expect(
            Result.Action == LE::ERenderFrameAction::Exit && IgnoredRetry.Action == LE::ERenderFrameAction::Exit &&
                Fixture.RecordCount == 1 && Fixture.Device.Fences[0]->ResetCount == 0 &&
                Fixture.Device.Queue.SubmitCount == 0 && Fixture.Swapchain.PresentCount == 0 &&
                Fixture.Swapchain.AcquireCount == AcquireCount,
            "Record failure is terminal before fence reset and submission");
    }

    for (const LE::ERALSwapchainStatus PresentStatus : {
        LE::ERALSwapchainStatus::Suboptimal,
        LE::ERALSwapchainStatus::OutOfDate,
        LE::ERALSwapchainStatus::Error })
    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Initialize();
        Fixture.Swapchain.PresentStatus = PresentStatus;
        const LE::FRenderFrameResult Result = Fixture.Execute();
        Failures += Expect(
            Result.Action == (PresentStatus == LE::ERALSwapchainStatus::Error
                ? LE::ERenderFrameAction::Exit
                : LE::ERenderFrameAction::RecreateSwapchain) &&
                Fixture.RecordCount == 1 && Fixture.Device.Queue.SubmitCount == 1 &&
                Fixture.Swapchain.PresentCount == 1 &&
                Fixture.Swapchain.GetCurrentBackBufferView() == nullptr,
            "Present status is observable and every present attempt consumes the acquired image");
    }

    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Initialize();
        Fixture.Swapchain.AcquireStatus = LE::ERALSwapchainStatus::OutOfDate;
        const LE::FRenderFrameResult FirstResult = Fixture.Execute();
        const bool bResized = Fixture.Swapchain.Resize(1280, 720);
        Fixture.Swapchain.AcquireStatus = LE::ERALSwapchainStatus::Success;
        const LE::FRenderFrameResult RetryResult = Fixture.Execute();
        Failures += Expect(
            FirstResult.Action == LE::ERenderFrameAction::RecreateSwapchain &&
                bResized && Fixture.Swapchain.ResizeCount == 1 &&
                RetryResult.Action == LE::ERenderFrameAction::Continue && RetryResult.SlotIndex == 0 &&
                Fixture.RecordCount == 1 && Fixture.Device.Queue.SubmitCount == 1 &&
                Fixture.Swapchain.PresentCount == 1,
            "Out-of-date acquisition supports explicit resize then retry without using a stale image");
    }

    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Initialize();
        const LE::FRenderFrameResult First = Fixture.Execute();
        const LE::FRenderFrameResult Second = Fixture.Execute();
        const LE::FRenderFrameResult Third = Fixture.Execute();
        Failures += Expect(
            First.SlotIndex == 0 && First.FrameIndex == 0 &&
                Second.SlotIndex == 1 && Second.FrameIndex == 1 &&
                Third.SlotIndex == 0 && Third.FrameIndex == 2 &&
                Fixture.Device.Fences[0]->WaitCount == 2 &&
                Fixture.Device.Fences[1]->WaitCount == 1 &&
                Fixture.Device.Allocators[0]->ResetCount == 2 &&
                Fixture.Device.Allocators[1]->ResetCount == 1 &&
                Fixture.Device.Queue.SubmitCount == 3 && Fixture.Device.Queue.WaitIdleCount == 0,
            "Two frame slots rotate 0 1 0 and reset only after their completion fence wait");
    }

    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Initialize();
        int DeferredDestroyCount = 0;
        Fixture.DeferredResource = new FTrackedRALResource(&DeferredDestroyCount);
        const LE::FRenderFrameResult First = Fixture.Execute();
        const bool bAccepted = Fixture.bDeferredAccepted;
        const LE::FRenderFrameResult Second = Fixture.Execute();
        const bool bStillAliveBeforeReuse = DeferredDestroyCount == 0;
        const LE::FRenderFrameResult Third = Fixture.Execute();
        Failures += Expect(
            First.Action == LE::ERenderFrameAction::Continue &&
                Second.Action == LE::ERenderFrameAction::Continue &&
                Third.Action == LE::ERenderFrameAction::Continue && bAccepted &&
                bStillAliveBeforeReuse && DeferredDestroyCount == 1,
            "Deferred ownership is reclaimed exactly when its frame slot fence completes");
    }

    {
        LE::Array<int> DestructionEvents;
        FFakeRALDevice Device;
        Device.Events = &DestructionEvents;
        Device.FailCreationCall = 7;
        LE::FRenderFrameScheduler Scheduler;
        const bool bInitialized = Scheduler.Initialize(&Device);
        bool bListBeforeAllocator = false;
        for (std::size_t Index = 1; Index < DestructionEvents.Size(); ++Index)
        {
            bListBeforeAllocator |= DestructionEvents[Index - 1] == 91 && DestructionEvents[Index] == 92;
        }
        Failures += Expect(
            !bInitialized && !Scheduler.IsInitialized() &&
                Device.DestroyedCommandLists == Device.CreatedCommandLists &&
                Device.DestroyedAllocators == static_cast<int>(Device.Allocators.Size()) &&
                Device.DestroyedSemaphores == static_cast<int>(Device.Semaphores.Size()) &&
                Device.DestroyedFences == static_cast<int>(Device.Fences.Size()) &&
                bListBeforeAllocator,
            "Partial scheduler initialization rolls back every resource list before allocator");
    }

    return Failures;
}

int RunRFGTransientSlotLifetimeTests()
{
    int Failures = 0;

    auto ExecuteGraph = [](
        const bool bImported,
        FFakeRALDevice& Device,
        FFakeRALCommandList& CommandList,
        int& RecordCount,
        LE::IRFGDeferredReleaseSink* const DeferredReleaseSink)
    {
        LE::FRFGBuilder Builder;
        LE::FRFGTextureDesc TextureDesc;
        TextureDesc.Width = 16;
        TextureDesc.Height = 16;
        TextureDesc.Format = LE::EPixelFormat::B8G8R8A8_SRGB;
        TextureDesc.UsageMask = 1u << 1;

        FFakeRALTexture ImportedTexture;
        ImportedTexture.Desc.Width = 16;
        ImportedTexture.Desc.Height = 16;
        ImportedTexture.Desc.Format = LE::EPixelFormat::B8G8R8A8_SRGB;
        const LE::FRFGResourceHandle Texture = bImported
            ? Builder.ImportTexture("Imported", &ImportedTexture, LE::ERALResourceState::Undefined)
            : Builder.CreateTexture("Transient", TextureDesc);

        LE::FRFGBufferDesc BufferDesc;
        BufferDesc.Size = 256;
        BufferDesc.Usage = LE::EResourceUsage::Local;
        BufferDesc.UsageMask = 1u;
        FFakeRALBuffer ImportedBuffer;
        ImportedBuffer.Desc.Size = BufferDesc.Size;
        const LE::FRFGResourceHandle Buffer = bImported
            ? Builder.ImportBuffer("ImportedBuffer", &ImportedBuffer, LE::ERALResourceState::Undefined)
            : Builder.CreateBuffer("TransientBuffer", BufferDesc);

        const LE::FRFGPassHandle Pass = Builder.AddPass(
            "TransientLifetime", "TransientLifetime", {}, LE::ERFGPassFlags::NeverCull);
        LE::FRFGAccessDesc TextureWrite;
        TextureWrite.Access = LE::ERFGAccessType::Write;
        TextureWrite.State = LE::ERALResourceState::RenderTarget;
        Builder.Write(Pass, Texture, TextureWrite);
        LE::FRFGAccessDesc BufferWrite;
        BufferWrite.Access = LE::ERFGAccessType::Write;
        BufferWrite.State = LE::ERALResourceState::CopyDestination;
        Builder.Write(Pass, Buffer, BufferWrite);
        Builder.MarkOutput(Texture);
        Builder.MarkOutput(Buffer);
        Builder.SetPassCallback(Pass, [&RecordCount](LE::FRFGPassContext&) noexcept { ++RecordCount; });

        LE::FRFGCompiler Compiler;
        const LE::FRFGCompileResult CompileResult = Compiler.Compile(
            Builder.GetRecordedGraph(), Builder.BuildSignature());
        if (!CompileResult.Plan)
        {
            return LE::ERALQueueSubmitResult::Error;
        }

        LE::FRFGExecutionContext Context;
        Context.Device = &Device;
        Context.GraphicsQueue = Device.GetGraphicsQueue();
        Context.CommandList = &CommandList;
        LE::FRFGExecuteOptions Options;
        Options.bSubmitImmediately = false;
        Options.bWaitForCompletion = false;
        Options.DeferredReleaseSink = DeferredReleaseSink;
        LE::FRFGExecutor Executor;
        const LE::ERALQueueSubmitResult Result =
            Executor.Execute(*CompileResult.Plan, Builder.GetRecordedGraph(), Context, Options);
        return Context.TextureResources.IsEmpty() && Context.BufferResources.IsEmpty() &&
            Context.OwnedTextures.IsEmpty() && Context.OwnedBuffers.IsEmpty()
            ? Result
            : LE::ERALQueueSubmitResult::Error;
    };

    {
        FFakeRALDevice Device;
        Device.bCreateOwnedTextures = true;
        Device.bCreateOwnedBuffers = true;
        FFakeRALCommandList CommandList;
        int RecordCount = 0;
        const LE::ERALQueueSubmitResult Result = ExecuteGraph(false, Device, CommandList, RecordCount, nullptr);
        Failures += Expect(
            Result == LE::ERALQueueSubmitResult::InvalidArguments &&
                Device.CreatedTextures == 1 && Device.DestroyedTextures == 1 &&
                Device.CreatedBuffers == 1 && Device.DestroyedBuffers == 1 &&
                CommandList.BeginCount == 0 && CommandList.EndCount == 0 &&
                RecordCount == 0 && Device.Queue.SubmitCount == 0 && Device.Queue.WaitIdleCount == 0,
            "Owned asynchronous RFG without a sink rejects before recording and releases every transient");
    }

    {
        FFakeRALDevice Device;
        FFakeRALCommandList CommandList;
        int RecordCount = 0;
        const LE::ERALQueueSubmitResult Result = ExecuteGraph(true, Device, CommandList, RecordCount, nullptr);
        Failures += Expect(
            Result == LE::ERALQueueSubmitResult::Success &&
                Device.CreatedTextures == 0 && Device.DestroyedTextures == 0 &&
                Device.CreatedBuffers == 0 && Device.DestroyedBuffers == 0 &&
                CommandList.BeginCount == 1 && CommandList.EndCount == 1 &&
                RecordCount == 1 && Device.Queue.SubmitCount == 0 && Device.Queue.WaitIdleCount == 0,
            "Imported asynchronous RFG records without ownership transfer submit or WaitIdle");
    }

    {
        FFakeRALDevice Device;
        Device.bCreateOwnedTextures = true;
        Device.bCreateOwnedBuffers = true;
        FFakeRALCommandList CommandList;
        FFakeRFGDeferredReleaseSink Sink;
        int RecordCount = 0;
        const LE::ERALQueueSubmitResult Result = ExecuteGraph(false, Device, CommandList, RecordCount, &Sink);
        const bool bTransferredWithoutDestroy =
            Result == LE::ERALQueueSubmitResult::Success && Sink.Resources.Size() == 2 &&
            Device.DestroyedTextures == 0 && Device.DestroyedBuffers == 0;
        Sink.DestroyAccepted();
        Failures += Expect(
            bTransferredWithoutDestroy && Device.DestroyedTextures == 1 && Device.DestroyedBuffers == 1 &&
                RecordCount == 1 && Device.Queue.SubmitCount == 0 && Device.Queue.WaitIdleCount == 0,
            "Accepting sink receives exact texture and buffer ownership without early destruction");
    }

    {
        FFakeRALDevice Device;
        Device.bCreateOwnedTextures = true;
        Device.bCreateOwnedBuffers = true;
        FFakeRALCommandList CommandList;
        FFakeRFGDeferredReleaseSink Sink;
        Sink.RejectCall = 2;
        int RecordCount = 0;
        const LE::ERALQueueSubmitResult Result = ExecuteGraph(false, Device, CommandList, RecordCount, &Sink);
        const bool bRejectedRemainderDestroyed =
            Result == LE::ERALQueueSubmitResult::InvalidArguments && Sink.Resources.Size() == 1 &&
            Device.DestroyedTextures == 0 && Device.DestroyedBuffers == 1;
        Sink.DestroyAccepted();
        Failures += Expect(
            bRejectedRemainderDestroyed && Device.DestroyedTextures == 1 && Device.DestroyedBuffers == 1 &&
                CommandList.BeginCount == 1 && CommandList.EndCount == 1 &&
                RecordCount == 1 && Device.Queue.SubmitCount == 0,
            "Partial sink rejection transfers accepted ownership and destroys only the refused remainder");
    }

    auto ExecuteScheduledGraph = [&ExecuteGraph](
        FRenderFrameSchedulerFixture& Fixture,
        const bool bRejectSecondTransfer)
    {
        LE::FRenderFrameCallback Callback = [&ExecuteGraph, &Fixture, bRejectSecondTransfer](
            LE::FRenderFrameScope& Frame) noexcept
        {
            int RecordCount = 0;
            if (bRejectSecondTransfer)
            {
                class FRejectSecondScopeSink final : public LE::IRFGDeferredReleaseSink
                {
                public:
                    explicit FRejectSecondScopeSink(LE::FRenderFrameScope& InFrame) noexcept : Frame(InFrame) {}
                    bool DeferRelease(LE::FRALResource* const Resource) noexcept override
                    {
                        ++CallCount;
                        return CallCount != 2 && Frame.DeferRelease(Resource);
                    }
                    LE::FRenderFrameScope& Frame;
                    int CallCount = 0;
                } Sink(Frame);
                const LE::ERALQueueSubmitResult Result = ExecuteGraph(
                    false, Fixture.Device, *static_cast<FFakeRALCommandList*>(Frame.GetCommandList()),
                    RecordCount, &Sink);
                return Result == LE::ERALQueueSubmitResult::Success
                    ? LE::ERenderFrameRecordResult::Success
                    : LE::ERenderFrameRecordResult::InvalidArguments;
            }

            FFrameScopeRFGDeferredReleaseSink Sink(Frame);
            const LE::ERALQueueSubmitResult Result = ExecuteGraph(
                false, Fixture.Device, *static_cast<FFakeRALCommandList*>(Frame.GetCommandList()),
                RecordCount, &Sink);
            return Result == LE::ERALQueueSubmitResult::Success
                ? LE::ERenderFrameRecordResult::Success
                : LE::ERenderFrameRecordResult::InvalidArguments;
        };
        return Fixture.Scheduler.ExecuteFrame(&Fixture.Swapchain, Callback);
    };

    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Device.bCreateOwnedTextures = true;
        Fixture.Device.bCreateOwnedBuffers = true;
        Fixture.Initialize();
        const LE::FRenderFrameResult First = ExecuteScheduledGraph(Fixture, false);
        const bool bAliveAfterSlot0 = Fixture.Device.DestroyedTextures == 0 && Fixture.Device.DestroyedBuffers == 0;
        const LE::FRenderFrameResult Second = ExecuteScheduledGraph(Fixture, false);
        const bool bAliveAfterSlot1 = Fixture.Device.DestroyedTextures == 0 && Fixture.Device.DestroyedBuffers == 0;
        Fixture.Events.Clear();
        const LE::FRenderFrameResult Third = ExecuteScheduledGraph(Fixture, false);
        const int ExpectedReusePrefix[] = { 1, 8, 9, 2 };
        bool bReuseOrder = Fixture.Events.Size() >= 4;
        for (std::size_t Index = 0; bReuseOrder && Index < 4; ++Index)
        {
            bReuseOrder = Fixture.Events[Index] == ExpectedReusePrefix[Index];
        }
        const bool bReclaimedSlot0ExactlyOnce =
            Fixture.Device.CreatedTextures == 3 && Fixture.Device.DestroyedTextures == 1 &&
            Fixture.Device.CreatedBuffers == 3 && Fixture.Device.DestroyedBuffers == 1;
        Failures += Expect(
            First.Action == LE::ERenderFrameAction::Continue &&
                Second.Action == LE::ERenderFrameAction::Continue &&
                Third.Action == LE::ERenderFrameAction::Continue &&
                bAliveAfterSlot0 && bAliveAfterSlot1 && bReuseOrder && bReclaimedSlot0ExactlyOnce &&
                Fixture.Device.Queue.WaitIdleCount == 0,
            "RFG transients survive slots 0 and 1 then reclaim exactly after slot 0 fence wait before allocator reset");
        Fixture.Scheduler.Shutdown();
        Failures += Expect(
            Fixture.Device.DestroyedTextures == 3 && Fixture.Device.DestroyedBuffers == 3 &&
                Fixture.Device.Queue.WaitIdleCount == 0,
            "Successful frame-slot transients are reclaimed exactly once at reuse or shutdown without WaitIdle");
    }

    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Device.bCreateOwnedTextures = true;
        Fixture.Device.bCreateOwnedBuffers = true;
        Fixture.Device.Queue.Result = LE::ERALQueueSubmitResult::Error;
        Fixture.Initialize();
        const LE::FRenderFrameResult Result = ExecuteScheduledGraph(Fixture, false);
        const bool bHeldAfterSubmitFailure =
            Result.SubmitResult == LE::ERALQueueSubmitResult::Error &&
            Fixture.Device.DestroyedTextures == 0 && Fixture.Device.DestroyedBuffers == 0 &&
            Fixture.Swapchain.PresentCount == 0;
        Fixture.Scheduler.Shutdown();
        Failures += Expect(
            bHeldAfterSubmitFailure && Fixture.Device.Queue.WaitIdleCount == 1 &&
                Fixture.Device.DestroyedTextures == 1 && Fixture.Device.DestroyedBuffers == 1,
            "Submit failure holds RFG transients until exceptional queue idle then reclaims exactly once");
    }

    {
        FRenderFrameSchedulerFixture Fixture;
        Fixture.Device.bCreateOwnedTextures = true;
        Fixture.Device.bCreateOwnedBuffers = true;
        Fixture.Initialize();
        const LE::FRenderFrameResult Result = ExecuteScheduledGraph(Fixture, true);
        const bool bNoSubmission =
            Result.RecordResult == LE::ERenderFrameRecordResult::InvalidArguments &&
            Fixture.Device.Queue.SubmitCount == 0 && Fixture.Swapchain.PresentCount == 0 &&
            Fixture.Device.DestroyedTextures == 0 && Fixture.Device.DestroyedBuffers == 1;
        Fixture.Scheduler.Shutdown();
        Failures += Expect(
            bNoSubmission && Fixture.Device.Queue.WaitIdleCount == 0 &&
                Fixture.Device.DestroyedTextures == 1 && Fixture.Device.DestroyedBuffers == 1,
            "Record rejection reclaims refused and partially transferred RFG resources without submit or leak");
    }

    return Failures;
}

int RunPlatformContractsTests()
{
    int Failures = 0;

    LE::FPlatformEventQueue Events;
    Failures += Expect(Events.IsEmpty(), "Platform event queue starts empty");

    LE::FPlatformEvent First;
    First.Type = LE::EPlatformEventType::Resized;
    First.WindowId = 17;
    First.Width = 1280;
    First.Height = 720;
    LE::FPlatformEvent Second;
    Second.Type = LE::EPlatformEventType::Minimized;
    Second.WindowId = 17;
    Events.Push(First);
    Events.Push(Second);
    Failures += Expect(Events.Size() == 2, "Platform event queue records count");

    LE::FPlatformEvent Polled;
    Failures += Expect(
        Events.Poll(Polled) &&
            Polled.Type == LE::EPlatformEventType::Resized &&
            Polled.WindowId == 17 &&
            Polled.Width == 1280 &&
            Polled.Height == 720,
        "Platform event queue preserves FIFO payload");
    Failures += Expect(
        Events.Poll(Polled) && Polled.Type == LE::EPlatformEventType::Minimized,
        "Platform event queue preserves FIFO order");
    Failures += Expect(!Events.Poll(Polled), "Platform event queue reports drain");

    Events.Push(First);
    Events.Clear();
    Failures += Expect(Events.IsEmpty(), "Platform event queue clears safely");

    LE::FPlatformSurface Surface;
    Failures += Expect(!Surface.IsValid(), "Unknown platform surface is invalid");
    Surface.Type = LE::EPlatformSurfaceType::Win32;
    Surface.WindowHandle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
    Failures += Expect(Surface.IsValid(), "Win32 platform surface requires window handle");
    Surface.Type = LE::EPlatformSurfaceType::MetalLayer;
    Failures += Expect(!Surface.IsValid(), "Metal platform surface requires layer handle");
    Surface.LayerHandle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(2));
    Failures += Expect(Surface.IsValid(), "Metal platform surface accepts opaque handles");

    const double BeforeSleep = LE::FPlatformTime::Seconds();
    LE::FPlatformTime::SleepForMilliseconds(2);
    const double AfterSleep = LE::FPlatformTime::Seconds();
    Failures += Expect(AfterSleep > BeforeSleep, "Platform monotonic time advances across sleep");
    Failures += Expect(
        LE::FPlatformTime::Seconds() >= AfterSleep,
        "Platform monotonic time never moves backwards");

    return Failures;
}

struct FRuntimeModuleTestContext
{
    LE::Array<int>* Events = nullptr;
    int StartEvent = 0;
    int StopEvent = 0;
    bool bFailStartup = false;
    bool bActive = false;
    int StartupCount = 0;
    int ShutdownCount = 0;
};

bool StartRuntimeModule(void* const Context) noexcept
{
    FRuntimeModuleTestContext& Module = *static_cast<FRuntimeModuleTestContext*>(Context);
    ++Module.StartupCount;
    Module.bActive = true;
    Module.Events->PushBack(Module.StartEvent);
    return !Module.bFailStartup;
}

void StopRuntimeModule(void* const Context) noexcept
{
    FRuntimeModuleTestContext& Module = *static_cast<FRuntimeModuleTestContext*>(Context);
    ++Module.ShutdownCount;
    Module.bActive = false;
    Module.Events->PushBack(Module.StopEvent);
}

bool StartStatelessRuntimeModule(void*) noexcept
{
    return true;
}

void StopStatelessRuntimeModule(void*) noexcept
{
}

LE::FRuntimeModuleDescriptor MakeRuntimeModuleDescriptor(
    const LE::StringView Name,
    FRuntimeModuleTestContext& Context,
    const LE::Span<const LE::StringView> Dependencies = {})
{
    LE::FRuntimeModuleDescriptor Descriptor;
    Descriptor.Name = Name;
    Descriptor.Dependencies = Dependencies;
    Descriptor.Context = &Context;
    Descriptor.Startup = &StartRuntimeModule;
    Descriptor.Shutdown = &StopRuntimeModule;
    return Descriptor;
}

bool EventsEqual(const LE::Array<int>& Events, const int* const Expected, const std::size_t Count)
{
    if (Events.Size() != Count)
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        if (Events[Index] != Expected[Index])
        {
            return false;
        }
    }
    return true;
}

int RunRuntimeModuleTests()
{
    int Failures = 0;

    {
        LE::Array<int> Events;
        FRuntimeModuleTestContext A{ &Events, 1, -1 };
        FRuntimeModuleTestContext B{ &Events, 2, -2 };
        FRuntimeModuleTestContext C{ &Events, 3, -3 };
        FRuntimeModuleTestContext D{ &Events, 4, -4 };
        FRuntimeModuleTestContext E{ &Events, 5, -5 };
        const LE::StringView CDependencies[] = { "A" };
        const LE::StringView DDependencies[] = { "A" };
        const LE::StringView EDependencies[] = { "D", "B", "C" };

        LE::FRuntimeModuleRegistry Registry;
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            "E", E, LE::Span<const LE::StringView>(EDependencies))));
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            "D", D, LE::Span<const LE::StringView>(DDependencies))));
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            "C", C, LE::Span<const LE::StringView>(CDependencies))));
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor("B", B)));
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor("A", A)));

        const int StartupOrder[] = { 1, 2, 3, 4, 5 };
        Failures += Expect(
            Registry.StartupAll() == LE::ERuntimeModuleStartupResult::Success &&
                Registry.GetState() == LE::ERuntimeModuleRegistryState::Running &&
                EventsEqual(Events, StartupOrder, 5),
            "Runtime modules use registration-independent lexical topological order");
        Failures += Expect(
            Registry.StartupAll() == LE::ERuntimeModuleStartupResult::InvalidState,
            "Runtime module registry rejects repeated startup");
        Failures += Expect(
            Registry.RegisterModule(MakeRuntimeModuleDescriptor("Later", A)) ==
                LE::ERuntimeModuleRegisterResult::InvalidState,
            "Runtime module registry rejects registration after startup");

        Registry.ShutdownAll();
        Registry.ShutdownAll();
        const int FullOrder[] = { 1, 2, 3, 4, 5, -5, -4, -3, -2, -1 };
        Failures += Expect(
            EventsEqual(Events, FullOrder, 10) &&
                Registry.GetState() == LE::ERuntimeModuleRegistryState::Shutdown,
            "Runtime modules shut down in strict reverse order exactly once");
    }

    {
        LE::FRuntimeModuleRegistry Registry;
        LE::FRuntimeModuleDescriptor Descriptor;
        Descriptor.Name = "7.Stateless_module-1";
        Descriptor.Startup = &StartStatelessRuntimeModule;
        Descriptor.Shutdown = &StopStatelessRuntimeModule;
        Failures += Expect(
            Registry.RegisterModule(Descriptor) == LE::ERuntimeModuleRegisterResult::Success &&
                Registry.StartupAll() == LE::ERuntimeModuleStartupResult::Success,
            "Runtime module identifier grammar accepts readable ASCII and null context");
    }

    {
        LE::Array<int> Events;
        FRuntimeModuleTestContext A{ &Events, 1, -1 };
        {
            LE::FRuntimeModuleRegistry Registry;
            static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor("A", A)));
            static_cast<void>(Registry.StartupAll());
        }
        const int DestructorOrder[] = { 1, -1 };
        Failures += Expect(
            EventsEqual(Events, DestructorOrder, 2) && A.ShutdownCount == 1,
            "Runtime module registry destructor safely shuts down active modules");
    }

    {
        LE::Array<int> Events;
        FRuntimeModuleTestContext A{ &Events, 1, -1 };
        FRuntimeModuleTestContext B{ &Events, 2, -2 };
        LE::String ModuleAName("Owned.A");
        LE::String ModuleBName("Owned.B");
        LE::String DependencyName("Owned.A");
        LE::StringView Dependencies[] = { DependencyName.View() };

        LE::FRuntimeModuleRegistry Registry;
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            ModuleAName.View(), A)));
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            ModuleBName.View(), B, LE::Span<const LE::StringView>(Dependencies))));

        ModuleAName.Clear();
        ModuleBName.Clear();
        DependencyName.Clear();
        Dependencies[0] = LE::StringView();

        const int StartupOrder[] = { 1, 2 };
        Failures += Expect(
            Registry.StartupAll() == LE::ERuntimeModuleStartupResult::Success &&
                EventsEqual(Events, StartupOrder, 2),
            "Runtime module registry owns copied names beyond borrowed descriptor lifetime");
        Registry.ShutdownAll();
    }

    {
        const LE::StringView InvalidNames[] = {
            LE::StringView(), ".Leading", "_Leading", "-Leading", "Has space",
            LE::StringView("A\0B", 3),
        };
        for (const LE::StringView InvalidName : InvalidNames)
        {
            LE::FRuntimeModuleRegistry Registry;
            LE::FRuntimeModuleDescriptor Descriptor;
            Descriptor.Name = InvalidName;
            Descriptor.Startup = &StartStatelessRuntimeModule;
            Descriptor.Shutdown = &StopStatelessRuntimeModule;
            Failures += Expect(
                Registry.RegisterModule(Descriptor) ==
                    LE::ERuntimeModuleRegisterResult::InvalidDescriptor &&
                    Registry.StartupAll() ==
                    LE::ERuntimeModuleStartupResult::InvalidRegistration,
                "Runtime module identifier grammar rejects empty, separator-leading, and unsafe names");
        }
    }

    {
        LE::Array<int> Events;
        FRuntimeModuleTestContext A{ &Events, 1, -1 };
        const LE::StringView EmptyDependency[] = { LE::StringView() };
        const LE::StringView DuplicateDependencies[] = { "A", "A" };

        LE::FRuntimeModuleRegistry EmptyDependencyRegistry;
        Failures += Expect(
            EmptyDependencyRegistry.RegisterModule(MakeRuntimeModuleDescriptor(
                "A", A, LE::Span<const LE::StringView>(EmptyDependency))) ==
                LE::ERuntimeModuleRegisterResult::InvalidDescriptor &&
                EmptyDependencyRegistry.StartupAll() ==
                LE::ERuntimeModuleStartupResult::InvalidRegistration && Events.IsEmpty(),
            "Runtime module registry rejects empty dependency names before startup");

        LE::FRuntimeModuleRegistry DuplicateDependencyRegistry;
        Failures += Expect(
            DuplicateDependencyRegistry.RegisterModule(MakeRuntimeModuleDescriptor(
                "B", A, LE::Span<const LE::StringView>(DuplicateDependencies))) ==
                LE::ERuntimeModuleRegisterResult::InvalidDescriptor &&
                DuplicateDependencyRegistry.StartupAll() ==
                LE::ERuntimeModuleStartupResult::InvalidRegistration && Events.IsEmpty(),
            "Runtime module registry rejects duplicate dependency names before startup");

        LE::FRuntimeModuleRegistry NullCallbackRegistry;
        LE::FRuntimeModuleDescriptor NullCallbackDescriptor;
        NullCallbackDescriptor.Name = "Null.Callback";
        NullCallbackDescriptor.Shutdown = &StopStatelessRuntimeModule;
        Failures += Expect(
            NullCallbackRegistry.RegisterModule(NullCallbackDescriptor) ==
                LE::ERuntimeModuleRegisterResult::InvalidDescriptor &&
                NullCallbackRegistry.StartupAll() ==
                LE::ERuntimeModuleStartupResult::InvalidRegistration,
            "Runtime module registry rejects null callbacks before startup");
    }

    {
        LE::Array<int> Events;
        FRuntimeModuleTestContext A{ &Events, 1, -1 };
        const LE::StringView MissingDependencies[] = { "Missing" };
        LE::FRuntimeModuleRegistry Registry;
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            "A", A, LE::Span<const LE::StringView>(MissingDependencies))));
        Failures += Expect(
            Registry.StartupAll() == LE::ERuntimeModuleStartupResult::MissingDependency &&
                Events.IsEmpty() && Registry.GetDiagnosticModuleName() == LE::StringView("A") &&
                Registry.GetDiagnosticDependencyName() == LE::StringView("Missing"),
            "Runtime module missing dependency fails before startup with owned diagnostics");
    }

    {
        LE::Array<int> Events;
        FRuntimeModuleTestContext A{ &Events, 1, -1 };
        FRuntimeModuleTestContext B{ &Events, 2, -2 };
        const LE::StringView ADependencies[] = { "B" };
        const LE::StringView BDependencies[] = { "A" };
        LE::FRuntimeModuleRegistry Registry;
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            "B", B, LE::Span<const LE::StringView>(BDependencies))));
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            "A", A, LE::Span<const LE::StringView>(ADependencies))));
        Failures += Expect(
            Registry.StartupAll() == LE::ERuntimeModuleStartupResult::DependencyCycle &&
                Events.IsEmpty() && Registry.GetDiagnosticModuleName() == LE::StringView("A"),
            "Runtime module cycles fail before startup with deterministic diagnostics");
    }

    {
        LE::Array<int> Events;
        FRuntimeModuleTestContext A{ &Events, 1, -1 };
        FRuntimeModuleTestContext B{ &Events, 2, -2 };
        FRuntimeModuleTestContext Failing{ &Events, 3, -3, true };
        const LE::StringView BDependencies[] = { "A" };
        const LE::StringView FailingDependencies[] = { "B" };
        LE::FRuntimeModuleRegistry Registry;
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor("A", A)));
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            "B", B, LE::Span<const LE::StringView>(BDependencies))));
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            "Failing", Failing, LE::Span<const LE::StringView>(FailingDependencies))));

        const int Expected[] = { 1, 2, 3, -3, -2, -1 };
        Failures += Expect(
            Registry.StartupAll() == LE::ERuntimeModuleStartupResult::ModuleStartupFailed &&
                EventsEqual(Events, Expected, 6) && !A.bActive && !B.bActive &&
                !Failing.bActive && Failing.ShutdownCount == 1 &&
                Registry.GetDiagnosticModuleName() == LE::StringView("Failing"),
            "Runtime module startup failure cleans partial state then rolls back exactly");
        Registry.ShutdownAll();
        Failures += Expect(
            EventsEqual(Events, Expected, 6),
            "Runtime module shutdown after rollback is idempotent");
    }

    {
        LE::Array<int> Events;
        FRuntimeModuleTestContext A{ &Events, 1, -1 };
        LE::FRuntimeModuleRegistry Registry;
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor("A", A)));
        const LE::ERuntimeModuleRegisterResult DuplicateResult =
            Registry.RegisterModule(MakeRuntimeModuleDescriptor("A", A));
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor("B", A)));
        Failures += Expect(
            DuplicateResult == LE::ERuntimeModuleRegisterResult::DuplicateName &&
                Registry.GetLastRegisterResult() == LE::ERuntimeModuleRegisterResult::Success &&
                Registry.GetRegistrationFailure() == LE::ERuntimeModuleRegisterResult::DuplicateName &&
                Registry.StartupAll() == LE::ERuntimeModuleStartupResult::InvalidRegistration &&
                Events.IsEmpty(),
            "Runtime module first registration failure remains latched before callbacks");
    }

    return Failures;
}

double EngineLoopClockSamples[8]{};
std::size_t EngineLoopClockSampleCount = 0;
std::size_t EngineLoopClockReadCount = 0;

void SetEngineLoopClockSamples(const double* Samples, const std::size_t Count)
{
    EngineLoopClockSampleCount = Count;
    EngineLoopClockReadCount = 0;
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        EngineLoopClockSamples[Index] = Samples[Index];
    }
}

double ReadEngineLoopClock()
{
    if (EngineLoopClockSampleCount == 0)
    {
        return 0.0;
    }
    const std::size_t Index = EngineLoopClockReadCount < EngineLoopClockSampleCount
        ? EngineLoopClockReadCount
        : EngineLoopClockSampleCount - 1;
    ++EngineLoopClockReadCount;
    return EngineLoopClockSamples[Index];
}

class FFakeApplication final : public LE::IApplication
{
public:
    enum class EModuleMode
    {
        Normal,
        InvalidRegistration,
        MissingDependency,
        StartupFailure,
    };

    FFakeApplication()
        : ModuleA{ &LifecycleEvents, 10, -10 }
        , ModuleB{ &LifecycleEvents, 20, -20 }
    {
    }

    void RegisterModules(LE::FRuntimeModuleRegistry& Registry) override
    {
        if (ModuleMode == EModuleMode::InvalidRegistration)
        {
            LE::FRuntimeModuleDescriptor InvalidDescriptor;
            InvalidDescriptor.Name = ".Invalid";
            InvalidDescriptor.Startup = &StartStatelessRuntimeModule;
            InvalidDescriptor.Shutdown = &StopStatelessRuntimeModule;
            static_cast<void>(Registry.RegisterModule(InvalidDescriptor));
            return;
        }

        if (ModuleMode == EModuleMode::MissingDependency)
        {
            const LE::StringView MissingDependencies[] = { "Missing" };
            static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
                "Test.A", ModuleA, LE::Span<const LE::StringView>(MissingDependencies))));
            return;
        }

        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor("Test.A", ModuleA)));
        const LE::StringView ModuleBDependencies[] = { "Test.A" };
        ModuleB.bFailStartup = ModuleMode == EModuleMode::StartupFailure;
        static_cast<void>(Registry.RegisterModule(MakeRuntimeModuleDescriptor(
            "Test.B", ModuleB, LE::Span<const LE::StringView>(ModuleBDependencies))));
    }

    bool Initialize() override
    {
        ++InitializeCount;
        LifecycleEvents.PushBack(30);
        return bInitializeResult;
    }

    LE::EApplicationTickResult Tick(const LE::FApplicationTickContext& Context) override
    {
        Contexts.PushBack(Context);
        if (bExitEnabled && Context.Phase == ExitPhase)
        {
            return LE::EApplicationTickResult::Exit;
        }
        return LE::EApplicationTickResult::Continue;
    }

    void Shutdown() noexcept override
    {
        ++ShutdownCount;
        LifecycleEvents.PushBack(-30);
    }

    EModuleMode ModuleMode = EModuleMode::Normal;
    bool bInitializeResult = true;
    bool bExitEnabled = false;
    LE::EApplicationFramePhase ExitPhase = LE::EApplicationFramePhase::Render;
    int InitializeCount = 0;
    int ShutdownCount = 0;
    LE::Array<LE::FApplicationTickContext> Contexts;
    LE::Array<int> LifecycleEvents;
    FRuntimeModuleTestContext ModuleA;
    FRuntimeModuleTestContext ModuleB;
};

int RunEngineLoopTests()
{
    int Failures = 0;

    LE::RAL::DestroyResource(nullptr);
    Failures += Expect(true, "RAL creator-side destruction accepts null resources");

    {
        const double Clock[] = { 10.0, 10.25 };
        SetEngineLoopClockSamples(Clock, 2);
        FFakeApplication Application;
        LE::FEngineLoop EngineLoop;
        LE::FEngineLoopConfig Config;
        Config.TimeSource = &ReadEngineLoopClock;

        Failures += Expect(
            EngineLoop.Initialize(Application, Config) == LE::EEngineLoopInitializeResult::Success &&
                EngineLoop.GetState() == LE::EEngineLoopState::Running &&
                Application.InitializeCount == 1 && EngineLoopClockReadCount == 0,
            "EngineLoop initializes without sampling the frame clock");
        Failures += Expect(
            EngineLoop.Initialize(Application, Config) == LE::EEngineLoopInitializeResult::InvalidState &&
                Application.InitializeCount == 1,
            "EngineLoop rejects reinitialization while running");
        Failures += Expect(
            EngineLoop.Tick() == LE::EEngineLoopTickResult::Continue &&
                Application.Contexts.Size() == 3 && EngineLoopClockReadCount == 1,
            "EngineLoop accepts one tick and samples the clock once");
        Failures += Expect(
            Application.Contexts[0].Phase == LE::EApplicationFramePhase::ProcessPlatformEvents &&
                Application.Contexts[1].Phase == LE::EApplicationFramePhase::Update &&
                Application.Contexts[2].Phase == LE::EApplicationFramePhase::Render,
            "EngineLoop executes explicit phases in canonical order");
        Failures += Expect(
            Application.Contexts[0].FrameIndex == 0 &&
                Application.Contexts[1].FrameIndex == 0 &&
                Application.Contexts[2].FrameIndex == 0 &&
                Application.Contexts[0].DeltaSeconds == 0.0 &&
                Application.Contexts[2].ElapsedSeconds == 0.0,
            "EngineLoop first frame shares index zero and zero time across phases");

        Failures += Expect(
            EngineLoop.Tick() == LE::EEngineLoopTickResult::Continue &&
                Application.Contexts.Size() == 6 && EngineLoop.GetNextFrameIndex() == 2 &&
                EngineLoopClockReadCount == 2,
            "EngineLoop advances one frame index per accepted top-level tick");
        Failures += Expect(
            Application.Contexts[3].FrameIndex == 1 &&
                Application.Contexts[3].DeltaSeconds == 0.25 &&
                Application.Contexts[3].ElapsedSeconds == 0.25 &&
                Application.Contexts[5].DeltaSeconds == Application.Contexts[3].DeltaSeconds &&
                Application.Contexts[5].ElapsedSeconds == Application.Contexts[3].ElapsedSeconds,
            "EngineLoop shares one elapsed-time context across every phase");

        EngineLoop.Shutdown();
        EngineLoop.Shutdown();
        const int LifecycleOrder[] = { 10, 20, 30, -30, -20, -10 };
        Failures += Expect(
            Application.ShutdownCount == 1 && EngineLoop.GetState() == LE::EEngineLoopState::Shutdown &&
                EventsEqual(Application.LifecycleEvents, LifecycleOrder, 6),
            "EngineLoop starts modules before app and shuts app before reverse modules exactly once");
    }

    {
        struct FExitCase
        {
            LE::EApplicationFramePhase Phase;
            std::size_t ExpectedCallbacks;
        };
        const FExitCase ExitCases[] = {
            { LE::EApplicationFramePhase::ProcessPlatformEvents, 1 },
            { LE::EApplicationFramePhase::Update, 2 },
            { LE::EApplicationFramePhase::Render, 3 },
        };

        for (const FExitCase& ExitCase : ExitCases)
        {
            const double Clock[] = { 5.0 };
            SetEngineLoopClockSamples(Clock, 1);
            FFakeApplication Application;
            Application.bExitEnabled = true;
            Application.ExitPhase = ExitCase.Phase;
            LE::FEngineLoop EngineLoop;
            LE::FEngineLoopConfig Config;
            Config.TimeSource = &ReadEngineLoopClock;
            static_cast<void>(EngineLoop.Initialize(Application, Config));

            Failures += Expect(
                EngineLoop.Tick() == LE::EEngineLoopTickResult::Exit &&
                    Application.Contexts.Size() == ExitCase.ExpectedCallbacks &&
                    Application.Contexts.Back().Phase == ExitCase.Phase &&
                    EngineLoop.GetNextFrameIndex() == 1,
                "EngineLoop every exit phase short-circuits later phases and consumes the tick index");
            const std::size_t ContextCount = Application.Contexts.Size();
            const std::size_t ClockReads = EngineLoopClockReadCount;
            Failures += Expect(
                EngineLoop.Tick() == LE::EEngineLoopTickResult::Exit &&
                    Application.Contexts.Size() == ContextCount &&
                    EngineLoopClockReadCount == ClockReads,
                "EngineLoop performs zero callbacks and zero clock reads after exit");
            EngineLoop.Shutdown();
            EngineLoop.Shutdown();
            const int ExitLifecycleOrder[] = { 10, 20, 30, -30, -20, -10 };
            Failures += Expect(
                Application.ShutdownCount == 1 &&
                    EventsEqual(Application.LifecycleEvents, ExitLifecycleOrder, 6),
                "EngineLoop phase exit shuts app before modules exactly once");
        }
    }

    {
        SetEngineLoopClockSamples(nullptr, 0);
        FFakeApplication Application;
        Application.bInitializeResult = false;
        LE::FEngineLoop EngineLoop;
        LE::FEngineLoopConfig Config;
        Config.TimeSource = &ReadEngineLoopClock;
        Failures += Expect(
            EngineLoop.Initialize(Application, Config) ==
                LE::EEngineLoopInitializeResult::ApplicationInitializeFailed &&
                Application.InitializeCount == 1 && Application.ShutdownCount == 1 &&
                EngineLoop.GetState() == LE::EEngineLoopState::ExitRequested,
            "EngineLoop reports initialize failure and immediately unwinds once");
        const int FailureLifecycleOrder[] = { 10, 20, 30, -30, -20, -10 };
        Failures += Expect(
            EventsEqual(Application.LifecycleEvents, FailureLifecycleOrder, 6),
            "EngineLoop application init failure shuts app before reverse module cleanup");
        Failures += Expect(
            EngineLoop.Initialize(Application, Config) == LE::EEngineLoopInitializeResult::InvalidState &&
                Application.InitializeCount == 1 && Application.ShutdownCount == 1,
            "EngineLoop rejects reinitialization after failed initialization");
        Failures += Expect(
            EngineLoop.Tick() == LE::EEngineLoopTickResult::Exit &&
                Application.Contexts.IsEmpty() && EngineLoopClockReadCount == 0,
            "EngineLoop failed initialization cannot tick or sample time");
        EngineLoop.Shutdown();
        EngineLoop.Shutdown();
        Failures += Expect(Application.ShutdownCount == 1,
            "EngineLoop explicit Shutdown does not repeat failed-init unwind");
    }

    {
        const double Clock[] = { 5.0, 4.0, 6.0 };
        SetEngineLoopClockSamples(Clock, 3);
        FFakeApplication Application;
        LE::FEngineLoop EngineLoop;
        LE::FEngineLoopConfig Config;
        Config.TimeSource = &ReadEngineLoopClock;
        static_cast<void>(EngineLoop.Initialize(Application, Config));
        static_cast<void>(EngineLoop.Tick());
        static_cast<void>(EngineLoop.Tick());
        static_cast<void>(EngineLoop.Tick());
        Failures += Expect(
            Application.Contexts[3].DeltaSeconds == 0.0 &&
                Application.Contexts[3].ElapsedSeconds == 0.0 &&
                Application.Contexts[6].DeltaSeconds == 1.0 &&
                Application.Contexts[6].ElapsedSeconds == 1.0,
            "EngineLoop clamps backwards clock movement without double-counting elapsed time");
        EngineLoop.Shutdown();
    }

    {
        SetEngineLoopClockSamples(nullptr, 0);
        FFakeApplication Application;
        Application.ModuleMode = FFakeApplication::EModuleMode::InvalidRegistration;
        LE::FEngineLoop EngineLoop;
        LE::FEngineLoopConfig Config;
        Config.TimeSource = &ReadEngineLoopClock;
        Failures += Expect(
            EngineLoop.Initialize(Application, Config) ==
                LE::EEngineLoopInitializeResult::ModuleRegistrationFailed &&
                Application.InitializeCount == 0 && Application.ShutdownCount == 0 &&
                Application.LifecycleEvents.IsEmpty() &&
                EngineLoop.Tick() == LE::EEngineLoopTickResult::Exit,
            "EngineLoop registration failure never enters application lifecycle or tick");
        EngineLoop.Shutdown();
        Failures += Expect(Application.ShutdownCount == 0,
            "EngineLoop does not shut down an application whose lifecycle never started");
    }

    {
        SetEngineLoopClockSamples(nullptr, 0);
        FFakeApplication Application;
        Application.ModuleMode = FFakeApplication::EModuleMode::MissingDependency;
        LE::FEngineLoop EngineLoop;
        LE::FEngineLoopConfig Config;
        Config.TimeSource = &ReadEngineLoopClock;
        Failures += Expect(
            EngineLoop.Initialize(Application, Config) ==
                LE::EEngineLoopInitializeResult::ModuleDependencyValidationFailed &&
                Application.InitializeCount == 0 && Application.ShutdownCount == 0 &&
                Application.LifecycleEvents.IsEmpty() &&
                EngineLoop.GetModuleRegistry().GetDiagnosticModuleName() == LE::StringView("Test.A") &&
                EngineLoop.GetModuleRegistry().GetDiagnosticDependencyName() == LE::StringView("Missing"),
            "EngineLoop exposes detailed dependency failure without module or app startup");
        EngineLoop.Shutdown();
    }

    {
        SetEngineLoopClockSamples(nullptr, 0);
        FFakeApplication Application;
        Application.ModuleMode = FFakeApplication::EModuleMode::StartupFailure;
        LE::FEngineLoop EngineLoop;
        LE::FEngineLoopConfig Config;
        Config.TimeSource = &ReadEngineLoopClock;
        const int RollbackOrder[] = { 10, 20, -20, -10 };
        Failures += Expect(
            EngineLoop.Initialize(Application, Config) ==
                LE::EEngineLoopInitializeResult::ModuleStartupFailed &&
                Application.InitializeCount == 0 && Application.ShutdownCount == 0 &&
                EventsEqual(Application.LifecycleEvents, RollbackOrder, 4) &&
                EngineLoop.GetModuleRegistry().GetDiagnosticModuleName() == LE::StringView("Test.B") &&
                EngineLoop.Tick() == LE::EEngineLoopTickResult::Exit,
            "EngineLoop module failure rolls back modules and never enters app lifecycle");
        EngineLoop.Shutdown();
        Failures += Expect(
            EventsEqual(Application.LifecycleEvents, RollbackOrder, 4) &&
                Application.ShutdownCount == 0,
            "EngineLoop shutdown after module rollback is exactly once");
    }

    return Failures;
}

int RunProbe(const char* const Argument)
{
    if (std::strcmp(Argument, "--oom-probe") == 0)
    {
        LE::SetOutOfMemoryHandler(&ExitOnOom);
        FailingAllocator Allocator;
        LE::Array<int> Values(Allocator);
        Values.PushBack(1);
    }
    if (std::strcmp(Argument, "--bounds-probe") == 0)
    {
        LE::SetContractViolationHandler(&ExitOnContract);
        LE::Array<int> Values;
        return Values[0];
    }
    if (std::strcmp(Argument, "--iterator-probe") == 0)
    {
        LE::SetContractViolationHandler(&ExitOnContract);
        LE::Array<int> Values;
        Values.PushBack(1);
        auto Iterator = Values.begin();
        Values.Reserve(64);
        return *Iterator;
    }
    if (std::strcmp(Argument, "--iterator-end-probe") == 0)
    {
        LE::SetContractViolationHandler(&ExitOnContract);
        LE::Array<int> Values;
        Values.PushBack(1);
        auto Iterator = Values.end();
        ++Iterator;
        return 0;
    }
    if (std::strcmp(Argument, "--alignment-probe") == 0)
    {
        LE::SetContractViolationHandler(&ExitOnContract);
        static_cast<void>(LE::TryAllocateBytes(LE::GetDefaultAllocator(), 16, 3));
        return 0;
    }
    if (std::strcmp(Argument, "--span-bounds-probe") == 0)
    {
        LE::SetContractViolationHandler(&ExitOnContract);
        LE::Span<int> Empty;
        return Empty[0];
    }
    if (std::strcmp(Argument, "--hash-oom-probe") == 0)
    {
        LE::SetOutOfMemoryHandler(&ExitOnOom);
        FailingAllocator Allocator;
        LE::HashMap<int, int> Values(Allocator);
        Values.Insert(1, 1);
    }
    if (std::strcmp(Argument, "--hash-iterator-probe") == 0)
    {
        LE::SetContractViolationHandler(&ExitOnContract);
        LE::HashMap<int, int> Values;
        Values.Insert(1, 1);
        auto Iterator = Values.begin();
        Values.Insert(2, 2);
        return Iterator->GetValue();
    }
    if (std::strcmp(Argument, "--string-oom-probe") == 0)
    {
        LE::SetOutOfMemoryHandler(&ExitOnOom);
        FailingAllocator Allocator;
        LE::String Value(Allocator);
        Value.Append("allocation");
    }
    if (std::strcmp(Argument, "--string-view-probe") == 0)
    {
        LE::SetContractViolationHandler(&ExitOnContract);
        return LE::StringView()[0];
    }
    if (std::strcmp(Argument, "--function-probe") == 0)
    {
        LE::SetContractViolationHandler(&ExitOnContract);
        LE::Function<void()> Empty;
        Empty();
    }
    if (std::strcmp(Argument, "--ownership-oom-probe") == 0)
    {
        LE::SetOutOfMemoryHandler(&ExitOnOom);
        FailingAllocator Allocator;
        static_cast<void>(LE::MakeUniqueWithAllocator<OwnedValue>(Allocator, 1));
    }
    if (std::strcmp(Argument, "--function-oom-probe") == 0)
    {
        LE::SetOutOfMemoryHandler(&ExitOnOom);
        FailingAllocator Allocator;
        LE::Function<int(int)> Callable(Allocator, AddCallable{1});
        return Callable(1);
    }
    if (std::strcmp(Argument, "--math-bounds-probe") == 0)
    {
        LE::SetContractViolationHandler(&ExitOnContract);
        LE::Math::Vector3f Value;
        return static_cast<int>(Value[3]);
    }
    return 0;
}

} // namespace

int main(const int ArgCount, char** Arguments)
{
    if (ArgCount > 1)
    {
        if (std::strcmp(Arguments[1], "--force-failure") == 0)
        {
            return Expect(false, "explicit failure path");
        }
        return RunProbe(Arguments[1]);
    }

    int FailureCount = 0;
    FailureCount += Expect(sizeof(void*) >= 4, "supported pointer width");
    FailureCount += RunAllocationTests();
    FailureCount += RunArrayLifetimeTests();
    FailureCount += RunArrayStorageTests();
    FailureCount += RunArrayFailureAndAlignmentTests();
    FailureCount += RunStaticArrayAndSpanTests();
    FailureCount += RunHashContainerSmokeTests();
    FailureCount += RunHashMapStorageTests();
    FailureCount += RunHashMapCollisionTests();
    FailureCount += RunHashMapPolicyAndFailureTests();
    FailureCount += RunHashSetParityTests();
    FailureCount += RunStringTests();
    FailureCount += RunUuidTests();
    FailureCount += RunRuntimeHandleTests();
    FailureCount += LE::RunReflectionTests();
    FailureCount += LE::RunReflectionMacroTests();
    FailureCount += LE::RunPropertySerializationTests();
    FailureCount += RunOwnershipTests();
    FailureCount += RunFunctionTests();
    FailureCount += RunMathVectorTests();
    FailureCount += RunMathMatrixTests();
    FailureCount += RunMathQuaternionTests();
    FailureCount += RunMathLayoutTests();
    FailureCount += RunExplicitFrameLifecycleTests();
    FailureCount += RunRFGTransientSlotLifetimeTests();
    FailureCount += RunPlatformContractsTests();
    FailureCount += RunRuntimeModuleTests();
    FailureCount += RunEngineLoopTests();

    if (FailureCount == 0)
    {
        std::puts("All Limitless C++ tests passed.");
    }
    return FailureCount == 0 ? 0 : 1;
}
