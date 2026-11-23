#pragma once

#include "CoreMinimal.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHISwapchain.h"
#include "RHI/RHIBindGroup.h"
#include "RHI/RHICommandAllocator.h"
#include "RHI/RHICommandList.h"
#include "RHI/RHISampler.h"
#include "RHI/RHIShader.h"

// Windows.h must be included before vulkan_win32.h to define HANDLE, HWND, etc.
// Include it AFTER all RHI headers to avoid macro pollution
#if PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#if PLATFORM_MAC
#define VK_USE_PLATFORM_METAL_EXT 1
#endif

#include <vulkan/vulkan.h>

#if PLATFORM_WINDOWS
#include <vulkan/vulkan_win32.h>
#endif
#if PLATFORM_MAC
#include <vulkan/vulkan_metal.h>
#endif

#if PLATFORM_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif

namespace LE
{

LE_DECLARE_LOG_CATEGORY_EXTERN(LogRHI);

namespace RHI
{
namespace Vulkan
{
static const char* const DeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
static const uint32 DeviceExtensionCount = 1;

static const char* const InstanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
#if PLATFORM_WINDOWS
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#if PLATFORM_MAC
    VK_EXT_METAL_SURFACE_EXTENSION_NAME, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
#if LE_RHI_ENABLE_VALIDATION
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
};
#if PLATFORM_WINDOWS
#if LE_RHI_ENABLE_VALIDATION
static const uint32 InstanceExtensionCount = 3;
#else
static const uint32 InstanceExtensionCount = 2;
#endif
#else
#if PLATFORM_MAC
#if LE_RHI_ENABLE_VALIDATION
static const uint32 InstanceExtensionCount = 4;
#else
static const uint32 InstanceExtensionCount = 3;
#endif
#else
#if LE_RHI_ENABLE_VALIDATION
static const uint32 InstanceExtensionCount = 2;
#else
static const uint32 InstanceExtensionCount = 1;
#endif
#endif
#endif

#if LE_RHI_ENABLE_VALIDATION
static const char* const InstanceLayers[] = {"VK_LAYER_KHRONOS_validation"};
static const uint32 InstanceLayerCount = 1;
#else
static const char* const InstanceLayers[] = {nullptr};
static const uint32 InstanceLayerCount = 0;
#endif

static const uint32 MaxBindlessDescriptorCount = 1000000;
}
}

class FVulkanRHITexture;
class FVulkanRHITextureView;
class FVulkanRHIDevice;
class FVulkanRHIPipeline_Graphics;

template <typename TDesc> class TVulkanResourceBase
{
protected:
    TVulkanResourceBase(FVulkanRHIDevice* InDevice, const TDesc& InDesc) : Device(InDevice), Desc(InDesc) {}

    FVulkanRHIDevice* Device = nullptr;
    TDesc Desc;
};

// ***********************************************************************************************
// **************************************** Device ***********************************************
// ***********************************************************************************************

struct FVulkanRHIContext
{
public:
    uint32 InstanceApiVersion = VK_API_VERSION_1_0;

    VkInstance Instance = VK_NULL_HANDLE;
    VkDevice LogicalDevice = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
#if LE_RHI_ENABLE_VALIDATION
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
#endif

public:
    uint32 GraphicsFamilyIndex = -1;

public:
    // ====== Dynamic Rendering
    bool bDynamicRenderingEnabled = false;
    bool bDynamicRenderingUsesKHR = false;
    PFN_vkCmdBeginRendering CmdBeginRendering = nullptr;
    PFN_vkCmdEndRendering CmdEndRendering = nullptr;

public:
    // ====== Bindless
    bool bBindlessSupported = false;
    VkDescriptorPool BindlessPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout BindlessLayout = VK_NULL_HANDLE;
    VkDescriptorSet BindlessDescriptorSet = VK_NULL_HANDLE;
};

class FVulkanRHIDevice : public FRHIDevice
{
public:
    FVulkanRHIDevice();
    virtual ~FVulkanRHIDevice();

public:
    FRHIQueue* GetGraphicsQueue() const override;

public:
    FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) override;
    FRHITexture* CreateTexture(const FRHITextureDesc& Desc) override;
    FRHITextureView* CreateTextureView(const FRHITextureViewDesc& Desc) override;
    FRHIShader* CreateShaderFromFile(EShaderStage Stage, const void* Data, uint64 Size, const LE::String& EntryPoint = "main") override;
    FRHIPipeline_Graphics* CreateGraphicsPipeline(const FRHIPipelineDesc_Graphics& Desc) override;
    FRHICommandAllocator* CreateCommandAllocator(EQueueType Type = EQueueType::Graphics) override;
    FRHICommandList* CreateCommandList(FRHICommandAllocator* Allocator) override;
    FRHISemaphore* CreateBinarySemaphore() override;
    FRHIFence* CreateFence(bool bInitiallySignaled = false) override;
    FRHISwapchain* CreateSwapchain(const FRHISwapchainDesc& Desc) override;
    FRHIBindGroup* CreateBindGroup(const FRHIBindGroupDesc& Desc) override;
    FRHIBindGroupLayout* CreateBindGroupLayout(const FRHIBindGroupLayoutDesc& Desc) override;
    FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) override;

public:
    FVulkanRHIContext VkContext;

    void InternalCreateInstance();
    void InternalSelectPhysicalDevice();
    void InternalCreateLogicalDevice();
    void InternalSetupValidationMessenger();
    void InternalDestroyValidationMessenger();

    FRHISwapchain* InternalCreateSwapchain(const FRHISwapchainDesc& InDesc);

    void InternalSetupBindlessHeap();

private:
    FRHIQueue* GraphicsQueue = nullptr;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRHISwapchain : public FRHISwapchain
{
public:
    FVulkanRHISwapchain(FVulkanRHIDevice* InDevice, const FRHISwapchainDesc& InDesc);
    ~FVulkanRHISwapchain() override;

public:
    FRHIAcquireResult AcquireNextImage(FRHISemaphore* SignalSemaphore, uint64 Timeout) override;
    FRHITextureView* GetCurrentBackBufferView() const override;
    ERHISwapchainStatus Present(FRHISemaphore* WaitSemaphore) override;
    bool Resize(uint32 Width, uint32 Height) override;

public:
    VkSwapchainKHR SwapchainHandle = VK_NULL_HANDLE;
    VkSurfaceKHR SurfaceHandle = VK_NULL_HANDLE;

    LE::Array<VkImage> Images;
    LE::Array<VkImageView> ImageViews;

    LE::Array<FVulkanRHITexture*> BackBufferTextures;
    LE::Array<FVulkanRHITextureView*> BackBufferViews;

    uint32 CurrentImageIndex = 0;

private:
    FVulkanRHIDevice* Device = nullptr;
    FRHISwapchainDesc Desc;

    bool bImageAcquired = false;

private:
    void InternalCreateSurface();
    void InternalCreateSwapchain();
    void InternalCreateImageViews();

protected:
    void InternalDestroySwapchainResources();
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRHISemaphore : public FRHISemaphore
{
public:
    FVulkanRHISemaphore(FVulkanRHIDevice* InDevice);
    ~FVulkanRHISemaphore() override;

public:
    VkSemaphore Handle = VK_NULL_HANDLE;

private:
    friend class FVulkanRHIQueue;
    FVulkanRHIDevice* Device;
};

class FVulkanRHIFence : public FRHIFence
{
public:
    FVulkanRHIFence(FVulkanRHIDevice* InDevice, bool bSignaled = false);
    ~FVulkanRHIFence() override;

public:
    void Reset() override;
    void Wait(uint64 Timeout) override;
    ERHIFenceWaitResult WaitChecked(uint64 Timeout) override;
    bool IsSignaled() override;

public:
    VkFence Handle = VK_NULL_HANDLE;

private:
    friend class FVulkanRHIQueue;
    FVulkanRHIDevice* Device;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRHIQueue : public FRHIQueue
{
public:
    FVulkanRHIQueue(FVulkanRHIDevice* InDevice, uint32 FamilyIndex, uint32 QueueIndex);

public:
    ERHIQueueSubmitResult Submit(const FRHISubmitInfo& SubmitInfo) override;
    void WaitIdle() override;
    ERHIFenceWaitResult WaitIdleChecked() override;
    EQueueType GetType() const override { return EQueueType::Graphics; }

public:
    VkQueue Handle = VK_NULL_HANDLE;

private:
    FVulkanRHIDevice* Device;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRHICommandAllocator : public FRHICommandAllocator
{
public:
    FVulkanRHICommandAllocator(FVulkanRHIDevice* InDevice, EQueueType InType);
    ~FVulkanRHICommandAllocator() override;

public:
    bool Reset() override;

public:
    FVulkanRHIDevice* Device = nullptr;
    EQueueType Type = EQueueType::Graphics;
    VkCommandPool Pool = VK_NULL_HANDLE;
};

class FVulkanRHICommandList : public FRHICommandList
{
public:
    explicit FVulkanRHICommandList(FVulkanRHICommandAllocator* InAllocator);
    ~FVulkanRHICommandList() override;

public:
    void Begin() override;
    void End() override;
    void ResourceBarriers(const FRHIBarrierBatch& Barriers) override;
    bool BeginChecked() override;
    bool EndChecked() override;
    bool TextureUploadBarrier(FRHITexture* Texture, ERHIResourceState Before, ERHIResourceState After) override;
    bool CopyBufferToTexture2D(FRHIBuffer* Source, FRHITexture* Destination, uint64 SourceOffset) override;

public:
    void BeginRenderPass(const FRHIRenderPassDesc& Desc) override;
    void EndRenderPass() override;

public:
    void SetViewport(const FRHIViewport& Viewport) override;
    void SetScissorRect(const FRHIScissorRect& Scissor) override;

public:
    void SetGraphicsPipeline(FRHIPipeline_Graphics* Pipeline) override;

public:
    void SetVertexBuffer(uint32 Slot, FRHIBuffer* Buffer, uint64 Offset) override;
    void SetIndexBuffer(FRHIBuffer* Buffer, uint64 Offset, EPixelFormat IndexFormat) override;
    void SetBindGroup(uint32 SetIndex, FRHIBindGroup* BindGroup) override;
    void SetPushConstants(EShaderStage Stage, const void* Data, uint32 Size) override;

public:
    void Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstInstance) override;
    void DrawIndexed(uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, int32 VertexOffset, uint32 FirstInstance) override;

public:
    VkCommandBuffer Handle = VK_NULL_HANDLE;

private:
    friend class FVulkanRHIQueue;
    FVulkanRHIDevice* Device = nullptr;
    FVulkanRHICommandAllocator* Allocator = nullptr;

private:
    FVulkanRHIPipeline_Graphics* CurrentPipeline = nullptr;
    bool bInsideRendering = false;
    bool bRecording = false;
    bool bExecutable = false;
    bool bUploadCopied = false;
    bool bUploadComplete = false;
    FVulkanRHITexture* UploadTexture = nullptr;
    FRHIRenderingAttachmentSignature ActiveRenderingSignature;
    LE::Array<VkImage> PendingPresentTransitionImages;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRHISampler : public FRHISampler
{
public:
    FVulkanRHISampler(FVulkanRHIDevice* InDevice, const FRHISamplerDesc& InDesc);
    ~FVulkanRHISampler() override;

public:
    const FRHISamplerDesc& GetDesc() const override { return this->Desc; }

public:
    VkSampler Handle = VK_NULL_HANDLE;

private:
    FVulkanRHIDevice* Device;
    FRHISamplerDesc Desc;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRHIBindGroupLayout : public FRHIBindGroupLayout, public TVulkanResourceBase<FRHIBindGroupLayoutDesc>
{
public:
    FVulkanRHIBindGroupLayout(FVulkanRHIDevice* InDevice, const FRHIBindGroupLayoutDesc& InDesc);
    ~FVulkanRHIBindGroupLayout() override;

public:
    const FRHIBindGroupLayoutDesc& GetDesc() const override { return this->Desc; }

public:
    VkDescriptorSetLayout Handle = VK_NULL_HANDLE;
};

class FVulkanRHIBindGroup : public FRHIBindGroup, public TVulkanResourceBase<FRHIBindGroupDesc>
{
public:
    FVulkanRHIBindGroup(FVulkanRHIDevice* InDevice, const FRHIBindGroupDesc& InDesc);
    ~FVulkanRHIBindGroup() override;

public:
    const FRHIBindGroupDesc& GetDesc() const override { return this->Desc; }

public:
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkDescriptorSet Set = VK_NULL_HANDLE;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRHIShader : public FRHIShader, public TVulkanResourceBase<FRHIShaderDesc>
{
public:
    FVulkanRHIShader(FVulkanRHIDevice* InDevice, const FRHIShaderDesc& InDesc);
    ~FVulkanRHIShader() override;

public:
    const FRHIShaderDesc& GetDesc() const override { return this->Desc; }

public:
    VkShaderModule Module = VK_NULL_HANDLE;
};

class FVulkanRHIPipeline_Graphics : public FRHIPipeline_Graphics, public TVulkanResourceBase<FRHIPipelineDesc_Graphics>
{
public:
    FVulkanRHIPipeline_Graphics(FVulkanRHIDevice* InDevice, const FRHIPipelineDesc_Graphics& InDesc);
    ~FVulkanRHIPipeline_Graphics() override;

public:
    const FRHIPipelineDesc_Graphics& GetDesc() const override { return this->Desc; }

public:
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRHIBuffer : public FRHIBuffer, public TVulkanResourceBase<FRHIBufferDesc>
{
public:
    FVulkanRHIBuffer(FVulkanRHIDevice* InDevice, const FRHIBufferDesc& InDesc);
    ~FVulkanRHIBuffer() override;

public:
    const FRHIBufferDesc& GetDesc() const override { return this->Desc; }

public:
    void* Map(uint64 Offset, uint64 Size) override;
    void Unmap() override;

public:
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;

private:
    friend class FVulkanRHICommandList;
    void* MappedPtr = nullptr;
};

class FVulkanRHITexture : public FRHITexture, public TVulkanResourceBase<FRHITextureDesc>
{
public:
    FVulkanRHITexture(FVulkanRHIDevice* InDevice, const FRHITextureDesc& InDesc);
    FVulkanRHITexture(FVulkanRHIDevice* InDevice, const FRHITextureDesc& InDesc, VkImage InImage, bool bInOwnsImage);
    ~FVulkanRHITexture() override;

public:
    const FRHITextureDesc& GetDesc() const override { return this->Desc; }

public:
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;

private:
    friend class FVulkanRHICommandList;
    friend class FVulkanRHIDevice;
    bool bOwnsImage = true;
};
class FVulkanRHITextureView : public FRHITextureView, public TVulkanResourceBase<FRHITextureViewDesc>
{
public:
    FVulkanRHITextureView(FVulkanRHIDevice* InDevice, const FRHITextureViewDesc& InDesc);
    FVulkanRHITextureView(FVulkanRHIDevice* InDevice, FVulkanRHITexture* InOwner, VkImageView InView, const FRHITextureViewDesc& InDesc);
    ~FVulkanRHITextureView() override;

public:
    FRHITexture* GetTexture() const override { return this->Owner; }
    const FRHITextureViewDesc& GetDesc() const override { return this->Desc; }

public:
    FVulkanRHITexture* Owner = nullptr;
    VkImageView View = VK_NULL_HANDLE;

private:
    void InitTextureView(FVulkanRHITexture* InOwner, VkImageView InViewHandle);
};

} // namespace LE
