#pragma once

#include "CoreMinimal.h"
#include "RALQueue.h"
#include "RALResource.h"
#include "RALDescription.h"

class FRALBuffer;
class FRALShader;
class FRALSampler;
class FRALTexture;
class FRALTextureView;
class FRALCommandList;
class FRALSwapchain;
class FRALPipeline_Graphics;
class FRALBindGroup;
class FRALBindGroupLayout;

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
    virtual FRALShader* CreateShaderFromFile(EShaderStage Stage, const void* Data, uint64 Size, const FString& EntryPoint = "main") = 0;
    virtual FRALPipeline_Graphics* CreateGraphicsPipeline(const FRALPipelineDesc_Graphics& Desc) = 0;
    virtual FRALCommandList* CreateCommandList(EQueueType Type = EQueueType::Graphics) = 0;
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
}
