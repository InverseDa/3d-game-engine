#pragma once

#include "CoreMinimal.h"
#include "RAL/RALDevice.h"
#include "RAL/RALSwapchain.h"

#include <vulkan/vulkan.h>

#include "RAL/RALCommandList.h"

#define PLATFORM_WINDOWS 1 // TODO: kodak

#if PLATFORM_WINDOWS
#include <vulkan/vulkan_win32.h>
#endif

#if PLATFORM_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif

namespace RAL
{
    namespace Vulkan
    {
        static constexpr std::vector<const char*> DeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
        static constexpr std::vector<const char*> InstanceExtensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
#if PLATFORM_WINDOWS
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
        };
        static constexpr std::vector<const char*> InstanceLayers = {
#if LE_RAL_ENABLE_VALIDATION
            "VK_LAYER_KHRONOS_validation",
#endif
        };
        static constexpr uint32 MaxBindlessDescriptorCount = 1e6;
    }
}

// ***********************************************************************************************
// **************************************** Device ***********************************************
// ***********************************************************************************************

struct FVulkanRALContext
{
public:
    VkInstance Instance = VK_NULL_HANDLE;
    VkDevice LogicalDevice = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;

public:
    uint32 GraphicsFamilyIndex = -1;

public:
    // ====== Bindless
    VkDescriptorPool BindlessPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout BindlessLayout = VK_NULL_HANDLE;
    VkDescriptorSet BindlessDescriptorSet = VK_NULL_HANDLE;
};

class RAL_API FVulkanRALDevice : public FRALDevice
{
public:
    FVulkanRALDevice();
    virtual ~FVulkanRALDevice();

public:
    FVulkanRALContext VkContext;

    void InternalCreateInstance();
    void InternalSelectPhysicalDevice();
    void InternalCreateLogicalDevice();

    FRALSwapchain* InternalCreateSwapchain(const FRALSwapchainDesc& InDesc);

    void InternalSetupBindlessHeap();

private:
    FRALQueue* GraphicsQueue = nullptr;
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class FVulkanRALTexture : public FRALTexture
{
    public:
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    FRALTextureDesc TextureDesc;

    const FRALTextureDesc& GetDesc() const override { return this->TextureDesc; }
};

class FVulkanRALTextureView : public FRALTextureView
{
public:
    FVulkanRALTexture* Owner = nullptr;
    VkImageView View = VK_NULL_HANDLE;

    FRALTexture* GetTexture() const override { return this->Owner; }
};

// ***********************************************************************************************
// ********************************** Regular Math Calc ******************************************
// ***********************************************************************************************

class RAL_API FVulkanRALSwapchain : public FRALSwapchain
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
    void SetGraphicsPipeline(FRALGraphicsPipeline* Pipeline) override;

public:
    void SetVertexBuffer(uint32 Slot, FRALBuffer* Buffer, uint64 Offset) override;
    void SetIndexBuffer(FRALBuffer* Buffer, uint64 Offset, EPixelFormat IndexFormat) override;
    void SetBindGroup(uint32 SetIndex, FRALBindGroup* BindGroup) override;

public:
    void Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstInstance) override;
    void DrawIndexed(uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, int32 VertexOffset, uint32 FirstInstance) override;

public:
    VkCommandBuffer Handle = VK_NULL_HANDLE;

private:
    FVulkanRALDevice* Device = nullptr;
    VkCommandPool Pool = VK_NULL_HANDLE;

private:
    FVulkanRALGraphicsPipeline* CurrentPipeline = nullptr;

private:
    VkRenderPass InternalGetRenderPass(const FRALRenderPassDesc& Desc, bool bIsCreate = false);
    VkFramebuffer InternalGetFramebuffer(const FRALRenderPassDesc& Desc, VkRenderPass Pass, bool bIsCreate = false);
};
