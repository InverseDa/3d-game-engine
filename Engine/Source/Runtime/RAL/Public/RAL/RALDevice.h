#pragma once

#include "CoreMinimal.h"
#include "RALQueue.h"
#include "RALResource.h"
#include "RALDescription.h"

namespace LE
{

class FRALBuffer;
class FRALShader;
class FRALSampler;
class FRALTexture;
class FRALTextureView;
class FRALCommandList;
class FRALCommandAllocator;
class FRALSwapchain;
class FRALPipeline_Graphics;
class FRALBindGroup;
class FRALBindGroupLayout;
class FRALSemaphore;
class FRALFence;

class RAL_API FRALDevice : public FRALResource
{
public:
    virtual ~FRALDevice() = default;

public:
    virtual FRALQueue* GetGraphicsQueue() const = 0;

public:
    virtual FRALBuffer* CreateBuffer(const FRALBufferDesc& Desc) = 0;
    virtual FRALTexture* CreateTexture(const FRALTextureDesc& Desc) = 0;
    virtual FRALTextureView* CreateTextureView(const FRALTextureViewDesc& Desc) = 0;
    virtual FRALShader* CreateShaderFromFile(EShaderStage Stage, const void* Data, uint64 Size, const LE::String& EntryPoint = "main") = 0;
    virtual FRALPipeline_Graphics* CreateGraphicsPipeline(const FRALPipelineDesc_Graphics& Desc) = 0;
    virtual FRALCommandAllocator* CreateCommandAllocator(EQueueType Type = EQueueType::Graphics) = 0;
    /** The allocator remains alive until after the returned command list is destroyed. */
    virtual FRALCommandList* CreateCommandList(FRALCommandAllocator* Allocator) = 0;
    virtual FRALSemaphore* CreateBinarySemaphore() = 0;
    virtual FRALFence* CreateFence(bool bInitiallySignaled = false) = 0;
    virtual FRALSwapchain* CreateSwapchain(const FRALSwapchainDesc& Desc) = 0;
    virtual FRALBindGroup* CreateBindGroup(const FRALBindGroupDesc& Desc) = 0;
    virtual FRALBindGroupLayout* CreateBindGroupLayout(const FRALBindGroupLayoutDesc& Desc) = 0;
    virtual FRALSampler* CreateSampler(const FRALSamplerDesc& Desc) = 0;

public:
    virtual void* GetBindlessHeapGPUDescriptor() const = 0;
    virtual uint32 AllocateBindlessIndex(FRALResource* Resource) = 0;
};

namespace RAL
{
    RAL_API FRALDevice* CreateDevice();
    // RAL objects are allocated by the backend module. Route destruction back
    // through RAL so Editor DLL consumers never perform a cross-module delete.
    RAL_API void DestroyResource(FRALResource* Resource) noexcept;
}

} // namespace LE
