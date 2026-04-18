#pragma once

#include "CoreMinimal.h"
#include "RAL/RALDevice.h"
#include "RAL/RALSwapchain.h"
#include "RAL/RALBindGroup.h"
#include "RAL/RALCommandList.h"
#include "RAL/RALSampler.h"
#include "RAL/RALShader.h"

// Windows.h must be included before vulkan_win32.h to define HANDLE, HWND, etc.
// Include it AFTER all RAL headers to avoid macro pollution
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

#include <unordered_map>

#if PLATFORM_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif

LE_DECLARE_LOG_CATEGORY_EXTERN(LogRAL);

namespace RAL
{
    namespace Vulkan
    {
        static const char* const DeviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
        static const uint32 DeviceExtensionCount = 1;

        static const char* const InstanceExtensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
#if PLATFORM_WINDOWS
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#if PLATFORM_MAC
            VK_EXT_METAL_SURFACE_EXTENSION_NAME,
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
#if LE_RAL_ENABLE_VALIDATION
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
        };
#if PLATFORM_WINDOWS
#if LE_RAL_ENABLE_VALIDATION
        static const uint32 InstanceExtensionCount = 3;
#else
        static const uint32 InstanceExtensionCount = 2;
#endif
#else
#if PLATFORM_MAC
#if LE_RAL_ENABLE_VALIDATION
        static const uint32 InstanceExtensionCount = 4;
#else
        static const uint32 InstanceExtensionCount = 3;
#endif
#else
#if LE_RAL_ENABLE_VALIDATION
        static const uint32 InstanceExtensionCount = 2;
#else
        static const uint32 InstanceExtensionCount = 1;
#endif
#endif
#endif

#if LE_RAL_ENABLE_VALIDATION
        static const char* const InstanceLayers[] = {
            "VK_LAYER_KHRONOS_validation"
        };
        static const uint32 InstanceLayerCount = 1;
#else
        static const char* const InstanceLayers[] = { nullptr };
        static const uint32 InstanceLayerCount = 0;
#endif

        static const uint32 MaxBindlessDescriptorCount = 1000000;
    }
}

class FVulkanRALTexture;
class FVulkanRALTextureView;
class FVulkanRALDevice;
class FVulkanRALPipeline_Graphics;

template <typename TDesc>
class TVulkanResourceBase
{
protected:
    TVulkanResourceBase(FVulkanRALDevice* InDevice, const TDesc& InDesc): Device(InDevice), Desc(InDesc) {}

    FVulkanRALDevice* Device = nullptr;
    TDesc Desc;
};

// ***********************************************************************************************
// **************************************** Device ***********************************************
// ***********************************************************************************************

struct FVulkanRALContext
{
public:
    VkInstance Instance = VK_NULL_HANDLE;
    VkDevice LogicalDevice = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
#if LE_RAL_ENABLE_VALIDATION
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
#endif

public:
    uint32 GraphicsFamilyIndex = -1;

public:
    // ====== Bindless
    bool bBindlessSupported = false;
    VkDescriptorPool BindlessPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout BindlessLayout = VK_NULL_HANDLE;
    VkDescriptorSet BindlessDescriptorSet = VK_NULL_HANDLE;
};

class FVulkanRALDevice : public FRALDevice
{
public:
    FVulkanRALDevice();
    virtual ~FVulkanRALDevice();

public:
    FRALQueue* GetGraphicsQueue() const override;
    void* GetBindlessHeapGPUDescriptor() const override;
    uint32 AllocateBindlessIndex(FRALResource* Resource) override;

public:
    FRALBuffer* CreateBuffer(const FRALBufferDesc& Desc) override;
    FRALTexture* CreateTexture(const FRALTextureDesc& Desc) override;
    FRALShader* CreateShaderFromFile(EShaderStage Stage, const void* Data, uint64 Size) override;
    FRALPipeline_Graphics* CreateGraphicsPipeline(const FRALPipelineDesc_Graphics& Desc) override;
    FRALCommandList* CreateCommandList(EQueueType Type = EQueueType::Graphics) override;
    FRALSwapchain* CreateSwapchain(const FRALSwapchainDesc& Desc) override;
    FRALBindGroup* CreateBindGroup(const FRALBindGroupDesc& Desc) override;
    FRALBindGroupLayout* CreateBindGroupLayout(const FRALBindGroupLayoutDesc& Desc) override;
    FRALSampler* CreateSampler(const FRALSamplerDesc& Desc) override;

public:
    FVulkanRALContext VkContext;

    void InternalCreateInstance();
    void InternalSelectPhysicalDevice();
    void InternalCreateLogicalDevice();
    void InternalSetupValidationMessenger();
    void InternalDestroyValidationMessenger();

    FRALSwapchain* InternalCreateSwapchain(const FRALSwapchainDesc& InDesc);

    void InternalSetupBindlessHeap();

private:
    FRALQueue* GraphicsQueue = nullptr;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRALSwapchain : public FRALSwapchain
{
public:
    FVulkanRALSwapchain(FVulkanRALDevice* InDevice, const FRALSwapchainDesc& InDesc);
    ~FVulkanRALSwapchain() override;

public:
    FRALTextureView* GetCurrentBackBufferView() const override;
    void Present() override;
    void Resize(uint32 Width, uint32 Height) override;

public:
    VkSwapchainKHR SwapchainHandle = VK_NULL_HANDLE;
    VkSurfaceKHR SurfaceHandle = VK_NULL_HANDLE;

    std::vector<VkImage> Images;
    std::vector<VkImageView> ImageViews;

    std::vector<FVulkanRALTexture*> BackBufferTextures;
    std::vector<FVulkanRALTextureView*> BackBufferViews;

    uint32 CurrentImageIndex = 0;

private:
    FVulkanRALDevice* Device = nullptr;
    FRALSwapchainDesc Desc;

private:
    VkSemaphore ImageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore RenderFinishedSemaphore = VK_NULL_HANDLE;

private:
    void InternalCreateSurface();
    void InternalCreateSwapchain();
    void InternalCreateImageViews();

protected:
    void InternalDestroySwapchainResources();

protected:
    void AcquireNextImage();
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRALSemaphore : public FRALSemaphore
{
public:
    FVulkanRALSemaphore(FVulkanRALDevice* InDevice);
    ~FVulkanRALSemaphore() override;

public:
    VkSemaphore Handle = VK_NULL_HANDLE;

private:
    FVulkanRALDevice* Device;
};

class FVulkanRALFence : public FRALFence
{
public:
    FVulkanRALFence(FVulkanRALDevice* InDevice, bool bSignaled= false);
    ~FVulkanRALFence() override;

public:
    void Reset() override;
    void Wait(uint64 Timeout) override;
    bool IsSignaled() override;

public:
    VkFence Handle = VK_NULL_HANDLE;

private:
    FVulkanRALDevice* Device;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRALQueue : public FRALQueue
{
public:
    FVulkanRALQueue(FVulkanRALDevice* InDevice, uint32 FamilyIndex, uint32 QueueIndex);

public:
    void Submit(const FRALSubmitInfo& SubmitInfo) override;
    void WaitIdle() override;
    EQueueType GetType() const override { return EQueueType::Graphics; }

public:
    VkQueue Handle = VK_NULL_HANDLE;

private:
    FVulkanRALDevice* Device;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRALCommandList : public FRALCommandList
{
public:
    FVulkanRALCommandList(FVulkanRALDevice* InDevice, EQueueType Type);
    ~FVulkanRALCommandList() override;

public:
    void Begin() override;
    void End() override;

public:
    void BeginRenderPass(const FRALRenderPassDesc& Desc) override;
    void EndRenderPass() override;

public:
    void SetViewport(const FRALViewport& Viewport) override;
    void SetScissorRect(const FRALScissorRect& Scissor) override;

public:
    void SetGraphicsPipeline(FRALPipeline_Graphics* Pipeline) override;

public:
    void SetVertexBuffer(uint32 Slot, FRALBuffer* Buffer, uint64 Offset) override;
    void SetIndexBuffer(FRALBuffer* Buffer, uint64 Offset, EPixelFormat IndexFormat) override;
    void SetBindGroup(uint32 SetIndex, FRALBindGroup* BindGroup) override;
    void SetPushConstants(EShaderStage Stage, const void* Data, uint32 Size) override;

public:
    void Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstInstance) override;
    void DrawIndexed(uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, int32 VertexOffset, uint32 FirstInstance) override;

public:
    VkCommandBuffer Handle = VK_NULL_HANDLE;

private:
    FVulkanRALDevice* Device = nullptr;
    VkCommandPool Pool = VK_NULL_HANDLE;

private:
    FVulkanRALPipeline_Graphics* CurrentPipeline = nullptr;
    std::unordered_map<uint64, VkFramebuffer> FramebufferCache;
    std::vector<VkImage> PendingPresentTransitionImages;

private:
    VkFramebuffer InternalGetFramebuffer(const FRALRenderPassDesc& Desc, VkRenderPass Pass, bool bIsCreate = false);
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRALSampler : public FRALSampler
{
public:
    FVulkanRALSampler(FVulkanRALDevice* InDevice, const FRALSamplerDesc& InDesc);
    ~FVulkanRALSampler() override;

public:
    const FRALSamplerDesc& GetDesc() const override { return this->Desc; }

public:
    VkSampler Handle = VK_NULL_HANDLE;

private:
    FVulkanRALDevice* Device;
    FRALSamplerDesc Desc;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRALBindGroupLayout : public FRALBindGroupLayout, public TVulkanResourceBase<FRALBindGroupLayoutDesc>
{
public:
    FVulkanRALBindGroupLayout(FVulkanRALDevice* InDevice, const FRALBindGroupLayoutDesc& InDesc);
    ~FVulkanRALBindGroupLayout() override;

public:
    const FRALBindGroupLayoutDesc& GetDesc() const override { return this->Desc; }

public:
    VkDescriptorSetLayout Handle = VK_NULL_HANDLE;
};

class FVulkanRALBindGroup : public FRALBindGroup, public TVulkanResourceBase<FRALBindGroupDesc>
{
public:
    FVulkanRALBindGroup(FVulkanRALDevice* InDevice, const FRALBindGroupDesc& InDesc);
    ~FVulkanRALBindGroup() override;

public:
    const FRALBindGroupDesc& GetDesc() const override { return this->Desc; }

public:
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkDescriptorSet Set = VK_NULL_HANDLE;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRALShader : public FRALShader, public TVulkanResourceBase<FRALShaderDesc>
{
public:
    FVulkanRALShader(FVulkanRALDevice* InDevice, const FRALShaderDesc& InDesc);
    ~FVulkanRALShader() override;

public:
    const FRALShaderDesc& GetDesc() const override { return this->Desc; }

public:
    VkShaderModule Module = VK_NULL_HANDLE;
};

class FVulkanRALPipeline_Graphics : public FRALPipeline_Graphics, public TVulkanResourceBase<FRALPipelineDesc_Graphics>
{
public:
    FVulkanRALPipeline_Graphics(FVulkanRALDevice* InDevice, const FRALPipelineDesc_Graphics& InDesc);
    ~FVulkanRALPipeline_Graphics() override;

public:
    const FRALPipelineDesc_Graphics& GetDesc() const override { return this->Desc; }

public:
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRALBuffer : public FRALBuffer, public TVulkanResourceBase<FRALBufferDesc>
{
public:
    FVulkanRALBuffer(FVulkanRALDevice* InDevice, const FRALBufferDesc& InDesc);
    ~FVulkanRALBuffer() override;

public:
    const FRALBufferDesc& GetDesc() const override { return this->Desc; }

public:
    void* Map(uint64 Offset, uint64 Size) override;
    void  Unmap() override;

public:
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;

private:
    void* MappedPtr = nullptr;
};

class FVulkanRALTexture : public FRALTexture, public TVulkanResourceBase<FRALTextureDesc>
{
public:
    FVulkanRALTexture(FVulkanRALDevice* InDevice, const FRALTextureDesc& InDesc);
    FVulkanRALTexture(FVulkanRALDevice* InDevice, const FRALTextureDesc& InDesc, VkImage InImage, bool bInOwnsImage);
    ~FVulkanRALTexture() override;

public:
    const FRALTextureDesc& GetDesc() const override { return this->Desc; }

public:
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;

private:
    bool bOwnsImage = true;
};
class FVulkanRALTextureView : public FRALTextureView, public TVulkanResourceBase<FRALTextureViewDesc>
{
public:
    FVulkanRALTextureView(FVulkanRALDevice* InDevice, const FRALTextureViewDesc& InDesc);
    FVulkanRALTextureView(FVulkanRALDevice* InDevice, FVulkanRALTexture* InOwner, VkImageView InView, const FRALTextureViewDesc& InDesc);
    ~FVulkanRALTextureView() override;

public:
    FRALTexture* GetTexture() const override { return this->Owner; }
    const FRALTextureViewDesc& GetDesc() const override { return this->Desc; }

public:
    FVulkanRALTexture* Owner = nullptr;
    VkImageView View = VK_NULL_HANDLE;

private:
    void InitTextureView(FVulkanRALTexture* InOwner, VkImageView InViewHandle);
};
