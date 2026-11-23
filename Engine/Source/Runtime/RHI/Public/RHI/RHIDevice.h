#pragma once

#include "CoreMinimal.h"
#include "RHIQueue.h"
#include "RHIResource.h"
#include "RHIDescription.h"

namespace LE
{

class FRHIBuffer;
class FRHIShader;
class FRHISampler;
class FRHITexture;
class FRHITextureView;
class FRHICommandList;
class FRHICommandAllocator;
class FRHISwapchain;
class FRHIPipeline_Graphics;
class FRHIBindGroup;
class FRHIBindGroupLayout;
class FRHISemaphore;
class FRHIFence;

class RHI_API FRHIDevice : public FRHIResource
{
public:
    virtual ~FRHIDevice() = default;

public:
    virtual FRHIQueue* GetGraphicsQueue() const = 0;

public:
    virtual FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) = 0;
    virtual FRHITexture* CreateTexture(const FRHITextureDesc& Desc) = 0;
    virtual FRHITextureView* CreateTextureView(const FRHITextureViewDesc& Desc) = 0;
    virtual FRHIShader* CreateShaderFromFile(EShaderStage Stage, const void* Data, uint64 Size, const LE::String& EntryPoint = "main") = 0;
    virtual FRHIPipeline_Graphics* CreateGraphicsPipeline(const FRHIPipelineDesc_Graphics& Desc) = 0;
    virtual FRHICommandAllocator* CreateCommandAllocator(EQueueType Type = EQueueType::Graphics) = 0;
    /** The allocator remains alive until after the returned command list is destroyed. */
    virtual FRHICommandList* CreateCommandList(FRHICommandAllocator* Allocator) = 0;
    virtual FRHISemaphore* CreateBinarySemaphore() = 0;
    virtual FRHIFence* CreateFence(bool bInitiallySignaled = false) = 0;
    virtual FRHISwapchain* CreateSwapchain(const FRHISwapchainDesc& Desc) = 0;
    virtual FRHIBindGroup* CreateBindGroup(const FRHIBindGroupDesc& Desc) = 0;
    virtual FRHIBindGroupLayout* CreateBindGroupLayout(const FRHIBindGroupLayoutDesc& Desc) = 0;
    virtual FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) = 0;
};

namespace RHI
{
RHI_API FRHIDevice* CreateDevice();
// RHI objects are allocated by the backend module. Route destruction back
// through RHI so Editor DLL consumers never perform a cross-module delete.
RHI_API void DestroyResource(FRHIResource* Resource) noexcept;
}

} // namespace LE
