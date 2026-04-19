#include "CoreMinimal.h"

#if PLATFORM_MAC

#include <fstream>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <mach-o/dyld.h>

#include "Mac/MacWindow.h"
#include "RALMinimal.h"
#include "RFGMinimal.h"
#include "ShaderRuntimeCompiler.h"
#include "Vulkan/VulkanRAL.h"

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);
LE_DECLARE_LOG_CATEGORY(LogXBD);

namespace
{
constexpr uint32 GTextureUsageRenderTarget = 1u << 1;
constexpr uint32 GTextureUsageShaderResource = 1u << 3;
}

struct FSimpleVertex
{
    FVector3f Position;
    FVector3f Color;
};

struct FOffscreenPassResources
{
    FRALTexture* Texture = nullptr;
    FRALTextureView* TextureView = nullptr;
    FRALSampler* Sampler = nullptr;
    FRALBindGroupLayout* BindGroupLayout = nullptr;
    FRALBindGroup* BindGroup = nullptr;
};

static bool TryReadShaderFile(const std::string& CandidatePath, std::vector<char>& OutBuffer)
{
    std::ifstream File(CandidatePath.c_str(), std::ios::ate | std::ios::binary);
    if (!File.is_open())
    {
        return false;
    }

    const size_t FileSize = static_cast<size_t>(File.tellg());
    if (FileSize == 0)
    {
        LE_LOG(LogXBD, Error, "Shader is empty: {}", CandidatePath);
        OutBuffer.clear();
        return true;
    }

    OutBuffer.resize(FileSize);
    File.seekg(0);
    File.read(OutBuffer.data(), FileSize);
    File.close();

    LE_LOG(LogXBD, Info, "Loaded shader: {} ({} bytes)", CandidatePath, FileSize);
    return true;
}

static std::string GetParentPath(const std::string& Path)
{
    if (Path.empty())
    {
        return std::string();
    }

    const size_t LastSeparator = Path.find_last_of("/\\");
    if (LastSeparator == std::string::npos)
    {
        return std::string();
    }

    return Path.substr(0, LastSeparator);
}

static std::string JoinPath(const std::string& Left, const std::string& Right)
{
    if (Left.empty())
    {
        return Right;
    }

    const char LastChar = Left[Left.size() - 1];
    if (LastChar == '/' || LastChar == '\\')
    {
        return Left + Right;
    }

    return Left + "/" + Right;
}

static std::string GetExecutableDirectory()
{
    uint32 BufferSize = 0;
    _NSGetExecutablePath(nullptr, &BufferSize);
    if (BufferSize == 0)
    {
        return std::string();
    }

    std::vector<char> Buffer(BufferSize);
    if (_NSGetExecutablePath(Buffer.data(), &BufferSize) != 0)
    {
        return std::string();
    }

    return GetParentPath(std::string(Buffer.data()));
}

static std::vector<char> ReadShaderFile(const char* Filename)
{
    std::vector<char> Buffer;

    const char* RelativePrefixes[] = {
        "",
        ".",
        "..",
        "../..",
        "../../..",
        "../../../..",
        "../../../../..",
        "../../../../../..",
        "../../../../../../.."
    };

    for (const char* Prefix : RelativePrefixes)
    {
        std::string Candidate = Prefix[0]
            ? (std::string(Prefix) + "/" + Filename)
            : std::string(Filename);
        if (TryReadShaderFile(Candidate, Buffer))
        {
            return Buffer;
        }
    }

    std::string SearchBase = GetExecutableDirectory();
    for (int32 i = 0; i < 10 && !SearchBase.empty(); ++i)
    {
        const std::string Candidate = JoinPath(SearchBase, Filename);
        if (TryReadShaderFile(Candidate, Buffer))
        {
            return Buffer;
        }
        SearchBase = GetParentPath(SearchBase);
    }

    LE_LOG(LogXBD, Error, "Failed to open shader (all tried paths): {}", Filename);
    return {};
}

static bool IsValidSpirv(const std::vector<char>& ByteCode, const char* ShaderLabel)
{
    if (ByteCode.empty())
    {
        LE_LOG(LogXBD, Error, "Shader bytecode is empty: {}", ShaderLabel);
        return false;
    }
    if ((ByteCode.size() % 4) != 0)
    {
        LE_LOG(LogXBD, Error, "Shader bytecode is not 4-byte aligned: {} (size={})", ShaderLabel, ByteCode.size());
        return false;
    }
    return true;
}

static FRALBuffer* CreateTriangleVertexBuffer(FRALDevice* Device)
{
    FSimpleVertex Vertices[3] = {
        { FVector3f( 0.0f,  0.5f, 0.0f), FVector3f(1.0f, 0.0f, 0.0f) },
        { FVector3f( 0.5f, -0.5f, 0.0f), FVector3f(0.0f, 1.0f, 0.0f) },
        { FVector3f(-0.5f, -0.5f, 0.0f), FVector3f(0.0f, 0.0f, 1.0f) },
    };

    const uint64 BufferSize = sizeof(Vertices);

    FRALBufferDesc BufferDesc;
    BufferDesc.Name = "TriangleVertexBuffer";
    BufferDesc.Size = BufferSize;
    BufferDesc.Usage = static_cast<uint32>(EResourceUsage::Upload);
    BufferDesc.UsageFlag = static_cast<uint32>(EBufferUsageFlags::VertexBuffer);

    FRALBuffer* Buffer = Device->CreateBuffer(BufferDesc);
    void* MappedData = Buffer->Map(0, BufferSize);
    memcpy(MappedData, Vertices, BufferSize);
    Buffer->Unmap();

    return Buffer;
}

static void DestroyOffscreenPassResources(FOffscreenPassResources& Resources)
{
    delete Resources.BindGroup;
    delete Resources.BindGroupLayout;
    delete Resources.Sampler;
    delete Resources.TextureView;
    delete Resources.Texture;

    Resources.BindGroup = nullptr;
    Resources.BindGroupLayout = nullptr;
    Resources.Sampler = nullptr;
    Resources.TextureView = nullptr;
    Resources.Texture = nullptr;
}

static bool CreateOffscreenPassResources(
    FRALDevice* Device,
    uint32 Width,
    uint32 Height,
    FOffscreenPassResources& Resources)
{
    DestroyOffscreenPassResources(Resources);

    FRALTextureDesc TextureDesc;
    TextureDesc.Name = "RFGSceneColor";
    TextureDesc.Width = Width;
    TextureDesc.Height = Height;
    TextureDesc.Depth = 1;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArrayLayers = 1;
    TextureDesc.Format = EPixelFormat::B8G8R8A8_SRGB;
    TextureDesc.bIsRenderTarget = true;
    TextureDesc.bIsShaderResource = true;

    Resources.Texture = Device->CreateTexture(TextureDesc);
    if (Resources.Texture == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create offscreen texture.");
        DestroyOffscreenPassResources(Resources);
        return false;
    }

    FVulkanRALDevice* VulkanDevice = static_cast<FVulkanRALDevice*>(Device);
    FVulkanRALTexture* VulkanTexture = static_cast<FVulkanRALTexture*>(Resources.Texture);

    FRALTextureViewDesc TextureViewDesc;
    TextureViewDesc.Texture = Resources.Texture;
    TextureViewDesc.Format = TextureDesc.Format;
    TextureViewDesc.MipSlice = 0;
    TextureViewDesc.ArraySlice = 0;
    TextureViewDesc.MipLevels = 1;
    TextureViewDesc.ArrayLayers = 1;
    Resources.TextureView = new FVulkanRALTextureView(VulkanDevice, VulkanTexture, VK_NULL_HANDLE, TextureViewDesc);
    if (Resources.TextureView == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create offscreen texture view.");
        DestroyOffscreenPassResources(Resources);
        return false;
    }

    FRALSamplerDesc SamplerDesc;
    SamplerDesc.Name = "RFGSceneColorSampler";
    SamplerDesc.AddressU = ESamplerAddressMode::ClampToEdge;
    SamplerDesc.AddressV = ESamplerAddressMode::ClampToEdge;
    SamplerDesc.AddressW = ESamplerAddressMode::ClampToEdge;
    Resources.Sampler = Device->CreateSampler(SamplerDesc);
    if (Resources.Sampler == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create offscreen sampler.");
        DestroyOffscreenPassResources(Resources);
        return false;
    }

    FRALBindGroupLayoutDesc BindGroupLayoutDesc;
    BindGroupLayoutDesc.Name = "CompositeSceneColorLayout";
    BindGroupLayoutDesc.SetIndex = 0;
    FRALBindGroupLayoutItem LayoutItem;
    LayoutItem.Binding = 0;
    LayoutItem.Count = 1;
    LayoutItem.Type = ERALBindGroupItemType::SampledImage;
    LayoutItem.StageFlags = EShaderStage::Pixel;
    BindGroupLayoutDesc.Bindings.push_back(LayoutItem);

    LayoutItem.Binding = 1;
    LayoutItem.Type = ERALBindGroupItemType::Sampler;
    BindGroupLayoutDesc.Bindings.push_back(LayoutItem);
    Resources.BindGroupLayout = Device->CreateBindGroupLayout(BindGroupLayoutDesc);
    if (Resources.BindGroupLayout == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create composite bind group layout.");
        DestroyOffscreenPassResources(Resources);
        return false;
    }

    FRALBindGroupDesc BindGroupDesc;
    BindGroupDesc.Name = "CompositeSceneColorBindGroup";
    BindGroupDesc.Layout = Resources.BindGroupLayout;
    FRALBindGroupItem BindGroupItem;
    BindGroupItem.Binding = 0;
    BindGroupItem.TextureView = Resources.TextureView;
    BindGroupDesc.Items.push_back(BindGroupItem);

    BindGroupItem = {};
    BindGroupItem.Binding = 1;
    BindGroupItem.Sampler = Resources.Sampler;
    BindGroupDesc.Items.push_back(BindGroupItem);
    Resources.BindGroup = Device->CreateBindGroup(BindGroupDesc);
    if (Resources.BindGroup == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create composite bind group.");
        DestroyOffscreenPassResources(Resources);
        return false;
    }

    return true;
}

static void TransitionTextureToShaderRead(
    FRALCommandList* CommandList,
    FRALTexture* Texture)
{
    FVulkanRALCommandList* VulkanCommandList = static_cast<FVulkanRALCommandList*>(CommandList);
    FVulkanRALTexture* VulkanTexture = static_cast<FVulkanRALTexture*>(Texture);
    if (VulkanCommandList == nullptr || VulkanTexture == nullptr || VulkanTexture->Image == VK_NULL_HANDLE)
    {
        return;
    }

    VkImageMemoryBarrier Barrier{};
    Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    Barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image = VulkanTexture->Image;
    Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.baseMipLevel = 0;
    Barrier.subresourceRange.levelCount = 1;
    Barrier.subresourceRange.baseArrayLayer = 0;
    Barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        VulkanCommandList->Handle,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &Barrier);
}

static FRALPipeline_Graphics* CreateTrianglePipeline(
    FRALDevice* Device,
    FRALShader* VertexShader,
    FRALShader* PixelShader,
    EPixelFormat RenderTargetFormat)
{
    FRALPipelineDesc_Graphics PipelineDesc;
    PipelineDesc.Name = "TrianglePipeline";
    PipelineDesc.VertexShader = VertexShader;
    PipelineDesc.PixelShader = PixelShader;

    FRALVertexInputBinding Binding;
    Binding.Binding = 0;
    Binding.Stride = sizeof(FSimpleVertex);
    Binding.bPerInstance = false;
    PipelineDesc.VertexBindings.push_back(Binding);

    FRALVertexInputAttribute PosAttr;
    PosAttr.Location = 0;
    PosAttr.Binding = 0;
    PosAttr.Format = EPixelFormat::R32G32B32_FLOAT;
    PosAttr.Offset = offsetof(FSimpleVertex, Position);
    PipelineDesc.VertexAttributes.push_back(PosAttr);

    FRALVertexInputAttribute ColorAttr;
    ColorAttr.Location = 1;
    ColorAttr.Binding = 0;
    ColorAttr.Format = EPixelFormat::R32G32B32_FLOAT;
    ColorAttr.Offset = offsetof(FSimpleVertex, Color);
    PipelineDesc.VertexAttributes.push_back(ColorAttr);

    PipelineDesc.RenderTargetCount = 1;
    PipelineDesc.RenderTargetFormats[0] = RenderTargetFormat;
    PipelineDesc.DepthStencilFormat = EPixelFormat::Unknown;
    PipelineDesc.RasterizerState.CullMode = ECullMode::None;
    PipelineDesc.RasterizerState.FillMode = EFillMode::Solid;
    PipelineDesc.BlendState.bEnable = false;

    return Device->CreateGraphicsPipeline(PipelineDesc);
}

static FRALPipeline_Graphics* CreateCompositePipeline(
    FRALDevice* Device,
    FRALShader* VertexShader,
    FRALShader* PixelShader,
    FRALBindGroupLayout* BindGroupLayout,
    EPixelFormat RenderTargetFormat)
{
    FRALPipelineDesc_Graphics PipelineDesc;
    PipelineDesc.Name = "CompositePipeline";
    PipelineDesc.VertexShader = VertexShader;
    PipelineDesc.PixelShader = PixelShader;
    PipelineDesc.RenderTargetCount = 1;
    PipelineDesc.RenderTargetFormats[0] = RenderTargetFormat;
    PipelineDesc.DepthStencilFormat = EPixelFormat::Unknown;
    PipelineDesc.RasterizerState.CullMode = ECullMode::None;
    PipelineDesc.RasterizerState.FillMode = EFillMode::Solid;
    PipelineDesc.BlendState.bEnable = false;
    PipelineDesc.BindGroupLayouts.push_back(BindGroupLayout);

    return Device->CreateGraphicsPipeline(PipelineDesc);
}

static void RenderFrame(
    FRALSwapchain* Swapchain,
    FRALCommandList* CmdList,
    FRALPipeline_Graphics* TrianglePipeline,
    FRALPipeline_Graphics* CompositePipeline,
    FRALBuffer* VertexBuffer,
    const FOffscreenPassResources& OffscreenResources,
    FRFGInstance& GraphInstance,
    FRALDevice* Device)
{
    FRALTextureView* BackBufferView = Swapchain->GetCurrentBackBufferView();
    if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
    {
        LE_LOG(LogXBD, Warn, "Skipping frame because the swapchain back buffer is not ready yet.");
        return;
    }

    if (OffscreenResources.Texture == nullptr || OffscreenResources.TextureView == nullptr || OffscreenResources.BindGroup == nullptr)
    {
        LE_LOG(LogXBD, Warn, "Skipping frame because the offscreen resources are incomplete.");
        return;
    }

    FRFGBuilder Builder = GraphInstance.CreateBuilder();
    const FRFGResourceHandle BackBufferHandle = Builder.ImportTexture("BackBuffer", BackBufferView->GetTexture());
    const FRFGResourceHandle SceneColorHandle = Builder.ImportTexture("SceneColor", OffscreenResources.Texture);

    FRFGAccessDesc GraphicsWrite;
    GraphicsWrite.Access = ERFGAccessType::Write;
    GraphicsWrite.PipelineStage = ERFGPipelineStage::Graphics;

    FRFGAccessDesc GraphicsRead;
    GraphicsRead.Access = ERFGAccessType::Read;
    GraphicsRead.PipelineStage = ERFGPipelineStage::Graphics;

    const FRFGPassHandle OffscreenColorPass = Builder.AddPass(
        "OffscreenColorPass",
        "TriangleRaster",
        {},
        ERFGPassFlags::None,
        ERFGQueueType::Graphics);
    const FRFGPassHandle CompositePass = Builder.AddPass(
        "CompositeToBackBufferPass",
        "CompositeRaster",
        {},
        ERFGPassFlags::HasSideEffects,
        ERFGQueueType::Graphics);

    Builder.Write(OffscreenColorPass, SceneColorHandle, GraphicsWrite);
    Builder.Read(CompositePass, SceneColorHandle, GraphicsRead);
    Builder.Write(CompositePass, BackBufferHandle, GraphicsWrite);
    Builder.MarkOutput(BackBufferHandle);

    Builder.SetPassCallback(
        OffscreenColorPass,
        [TrianglePipeline, VertexBuffer, SceneColorView = OffscreenResources.TextureView](FRFGPassContext& Context)
        {
            FRALCommandList* GraphCmdList = Context.GetCommandList();
            if (GraphCmdList == nullptr)
            {
                return;
            }

            GraphCmdList->SetGraphicsPipeline(TrianglePipeline);

            FRALRenderPassDesc RenderPassDesc{};
            RenderPassDesc.ColorAttachmentCount = 1;
            RenderPassDesc.ColorAttachments[0].RenderTarget = SceneColorView;
            RenderPassDesc.ColorAttachments[0].LoadOp = EAttachmentLoadOp::Clear;
            RenderPassDesc.ColorAttachments[0].StoreOp = EAttachmentStoreOp::Store;
            RenderPassDesc.ColorAttachments[0].ClearColor[0] = 0.04f;
            RenderPassDesc.ColorAttachments[0].ClearColor[1] = 0.08f;
            RenderPassDesc.ColorAttachments[0].ClearColor[2] = 0.12f;
            RenderPassDesc.ColorAttachments[0].ClearColor[3] = 1.0f;
            RenderPassDesc.bHasDepthStencil = false;

            GraphCmdList->BeginRenderPass(RenderPassDesc);

            const FRALTextureDesc& TexDesc = SceneColorView->GetTexture()->GetDesc();
            FRALViewport Viewport;
            Viewport.X = 0.0f;
            Viewport.Y = 0.0f;
            Viewport.Width = static_cast<float>(TexDesc.Width);
            Viewport.Height = static_cast<float>(TexDesc.Height);
            Viewport.MinDepth = 0.0f;
            Viewport.MaxDepth = 1.0f;
            GraphCmdList->SetViewport(Viewport);

            FRALScissorRect Scissor;
            Scissor.X = 0;
            Scissor.Y = 0;
            Scissor.Width = TexDesc.Width;
            Scissor.Height = TexDesc.Height;
            GraphCmdList->SetScissorRect(Scissor);

            GraphCmdList->SetVertexBuffer(0, VertexBuffer, 0);
            GraphCmdList->Draw(3, 1, 0);
            GraphCmdList->EndRenderPass();
        });

    Builder.SetPassCallback(
        CompositePass,
        [CompositePipeline, SceneColorTexture = OffscreenResources.Texture, CompositeBindGroup = OffscreenResources.BindGroup, BackBufferView](FRFGPassContext& Context)
        {
            FRALCommandList* GraphCmdList = Context.GetCommandList();
            if (GraphCmdList == nullptr)
            {
                return;
            }

            TransitionTextureToShaderRead(GraphCmdList, SceneColorTexture);
            GraphCmdList->SetGraphicsPipeline(CompositePipeline);
            GraphCmdList->SetBindGroup(0, CompositeBindGroup);

            FRALRenderPassDesc RenderPassDesc{};
            RenderPassDesc.ColorAttachmentCount = 1;
            RenderPassDesc.ColorAttachments[0].RenderTarget = BackBufferView;
            RenderPassDesc.ColorAttachments[0].LoadOp = EAttachmentLoadOp::Clear;
            RenderPassDesc.ColorAttachments[0].StoreOp = EAttachmentStoreOp::Store;
            RenderPassDesc.ColorAttachments[0].ClearColor[0] = 0.0f;
            RenderPassDesc.ColorAttachments[0].ClearColor[1] = 0.0f;
            RenderPassDesc.ColorAttachments[0].ClearColor[2] = 0.0f;
            RenderPassDesc.ColorAttachments[0].ClearColor[3] = 1.0f;
            RenderPassDesc.bHasDepthStencil = false;

            GraphCmdList->BeginRenderPass(RenderPassDesc);

            const FRALTextureDesc& TexDesc = BackBufferView->GetTexture()->GetDesc();
            FRALViewport Viewport;
            Viewport.X = 0.0f;
            Viewport.Y = 0.0f;
            Viewport.Width = static_cast<float>(TexDesc.Width);
            Viewport.Height = static_cast<float>(TexDesc.Height);
            Viewport.MinDepth = 0.0f;
            Viewport.MaxDepth = 1.0f;
            GraphCmdList->SetViewport(Viewport);

            FRALScissorRect Scissor;
            Scissor.X = 0;
            Scissor.Y = 0;
            Scissor.Width = TexDesc.Width;
            Scissor.Height = TexDesc.Height;
            GraphCmdList->SetScissorRect(Scissor);

            GraphCmdList->Draw(3, 1, 0);
            GraphCmdList->EndRenderPass();
        });

    const FRFGGraphSignature Signature = Builder.BuildSignature();
    const FRFGCompileResult CompileResult = GraphInstance.Compile(Builder.GetRecordedGraph(), Signature);

    FRFGExecutionContext ExecutionContext;
    ExecutionContext.Device = Device;
    ExecutionContext.GraphicsQueue = Device->GetGraphicsQueue();
    ExecutionContext.CommandList = CmdList;

    FRFGExecuteOptions ExecuteOptions;
    ExecuteOptions.bSubmitImmediately = true;
    ExecuteOptions.bWaitForCompletion = true;
    GraphInstance.Execute(CompileResult, Builder.GetRecordedGraph(), ExecutionContext, ExecuteOptions);

    Swapchain->Present();
}

static void GetBackBufferExtent(FRALSwapchain* Swapchain, uint32 FallbackWidth, uint32 FallbackHeight, uint32& OutWidth, uint32& OutHeight)
{
    OutWidth = FallbackWidth;
    OutHeight = FallbackHeight;

    if (Swapchain == nullptr)
    {
        return;
    }

    FRALTextureView* BackBufferView = Swapchain->GetCurrentBackBufferView();
    if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
    {
        return;
    }

    const FRALTextureDesc& BackBufferDesc = BackBufferView->GetTexture()->GetDesc();
    OutWidth = BackBufferDesc.Width;
    OutHeight = BackBufferDesc.Height;
}

static bool SyncSwapchainToWindowSize(FPlatformWindow* Window, FRALSwapchain* Swapchain, uint32& InOutWidth, uint32& InOutHeight)
{
    uint32 WindowWidth = 0;
    uint32 WindowHeight = 0;
    Window->GetSize(WindowWidth, WindowHeight);
    if (WindowWidth == 0 || WindowHeight == 0)
    {
        return false;
    }

    if (WindowWidth != InOutWidth || WindowHeight != InOutHeight)
    {
        LE_LOG(LogXBD, Info, "Window resized to {}x{}. Recreating swapchain.", WindowWidth, WindowHeight);
        Swapchain->Resize(WindowWidth, WindowHeight);
        InOutWidth = WindowWidth;
        InOutHeight = WindowHeight;
    }

    return true;
}

int32 GuardedMain()
{
    LE_INIT()
    LE_LOG(LogXBD, Info, "=== Limitless Engine P1: Triangle Demo (macOS) ===")

    const uint32 WindowWidth = 1280;
    const uint32 WindowHeight = 720;
    FPlatformWindow* Window = new FMacWindow(WindowWidth, WindowHeight, "Limitless Engine - Triangle Demo");
    if (!Window)
    {
        LE_LOG(LogXBD, Error, "Failed to create macOS window");
        LE_SHUTDOWN()
        return -1;
    }

    FRALDevice* Device = RAL::CreateDevice();
    if (!Device)
    {
        LE_LOG(LogXBD, Error, "Failed to create RAL Device");
        delete Window;
        LE_SHUTDOWN()
        return -1;
    }

    FRALSwapchainDesc SwapchainDesc;
    SwapchainDesc.Surface = Window->GetSurfaceDesc();
    SwapchainDesc.Width = WindowWidth;
    SwapchainDesc.Height = WindowHeight;
    SwapchainDesc.BackBufferFormat = EPixelFormat::B8G8R8A8_SRGB;
    SwapchainDesc.bEnableVsync = true;
    FRALSwapchain* Swapchain = Device->CreateSwapchain(SwapchainDesc);
    if (!Swapchain)
    {
        LE_LOG(LogXBD, Error, "Failed to create swapchain");
        delete Device;
        delete Window;
        LE_SHUTDOWN()
        return -1;
    }

    std::vector<char> TriangleVertBytecode;
    std::vector<char> TriangleFragBytecode;
    std::vector<char> CompositeVertBytecode;
    std::vector<char> CompositeFragBytecode;
    if (!Launch::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/TriangleVS.lsf", ERuntimeShaderStage::Vertex, TriangleVertBytecode, "MainVS") ||
        !Launch::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/TrianglePS.lsf", ERuntimeShaderStage::Fragment, TriangleFragBytecode, "MainPS") ||
        !Launch::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/CompositeVS.lsf", ERuntimeShaderStage::Vertex, CompositeVertBytecode, "MainVS") ||
        !Launch::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/CompositePS.lsf", ERuntimeShaderStage::Fragment, CompositeFragBytecode, "MainPS"))
    {
        delete Swapchain;
        delete Device;
        delete Window;
        LE_SHUTDOWN()
        return -1;
    }

    FRALShader* TriangleVertexShader = Device->CreateShaderFromFile(
        EShaderStage::Vertex, TriangleVertBytecode.data(), TriangleVertBytecode.size(), "MainVS");
    FRALShader* TrianglePixelShader = Device->CreateShaderFromFile(
        EShaderStage::Pixel, TriangleFragBytecode.data(), TriangleFragBytecode.size(), "MainPS");
    FRALShader* CompositeVertexShader = Device->CreateShaderFromFile(
        EShaderStage::Vertex, CompositeVertBytecode.data(), CompositeVertBytecode.size(), "MainVS");
    FRALShader* CompositePixelShader = Device->CreateShaderFromFile(
        EShaderStage::Pixel, CompositeFragBytecode.data(), CompositeFragBytecode.size(), "MainPS");
    if (!TriangleVertexShader || !TrianglePixelShader || !CompositeVertexShader || !CompositePixelShader)
    {
        delete CompositePixelShader;
        delete CompositeVertexShader;
        delete TrianglePixelShader;
        delete TriangleVertexShader;
        delete Swapchain;
        delete Device;
        delete Window;
        LE_SHUTDOWN()
        return -1;
    }

    uint32 InitialRenderWidth = WindowWidth;
    uint32 InitialRenderHeight = WindowHeight;
    GetBackBufferExtent(Swapchain, WindowWidth, WindowHeight, InitialRenderWidth, InitialRenderHeight);

    FOffscreenPassResources OffscreenResources;
    if (!CreateOffscreenPassResources(Device, InitialRenderWidth, InitialRenderHeight, OffscreenResources))
    {
        delete CompositePixelShader;
        delete CompositeVertexShader;
        delete TrianglePixelShader;
        delete TriangleVertexShader;
        delete Swapchain;
        delete Device;
        delete Window;
        LE_SHUTDOWN()
        return -1;
    }

    FRALPipeline_Graphics* TrianglePipeline = CreateTrianglePipeline(
        Device,
        TriangleVertexShader,
        TrianglePixelShader,
        EPixelFormat::B8G8R8A8_SRGB);
    FRALPipeline_Graphics* CompositePipeline = CreateCompositePipeline(
        Device,
        CompositeVertexShader,
        CompositePixelShader,
        OffscreenResources.BindGroupLayout,
        EPixelFormat::B8G8R8A8_SRGB);
    if (!TrianglePipeline || !CompositePipeline)
    {
        delete CompositePipeline;
        delete TrianglePipeline;
        DestroyOffscreenPassResources(OffscreenResources);
        delete CompositePixelShader;
        delete CompositeVertexShader;
        delete TrianglePixelShader;
        delete TriangleVertexShader;
        delete Swapchain;
        delete Device;
        delete Window;
        LE_SHUTDOWN()
        return -1;
    }

    FRALBuffer* VertexBuffer = CreateTriangleVertexBuffer(Device);
    FRALCommandList* CmdList = Device->CreateCommandList(EQueueType::Graphics);
    FRFGRuntime GraphRuntime;
    GraphRuntime.Initialize(nullptr);
    FRFGInstance GraphInstance;
    GraphInstance.Initialize(&GraphRuntime);
    uint32 CachedWindowWidth = WindowWidth;
    uint32 CachedWindowHeight = WindowHeight;

    while (Window->ProcessMessages())
    {
        if (Window->IsMinimized() || !SyncSwapchainToWindowSize(Window, Swapchain, CachedWindowWidth, CachedWindowHeight))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        uint32 TargetRenderWidth = CachedWindowWidth;
        uint32 TargetRenderHeight = CachedWindowHeight;
        GetBackBufferExtent(Swapchain, CachedWindowWidth, CachedWindowHeight, TargetRenderWidth, TargetRenderHeight);

        if ((OffscreenResources.Texture == nullptr) ||
            (OffscreenResources.Texture->GetDesc().Width != TargetRenderWidth) ||
            (OffscreenResources.Texture->GetDesc().Height != TargetRenderHeight))
        {
            if (!CreateOffscreenPassResources(Device, TargetRenderWidth, TargetRenderHeight, OffscreenResources))
            {
                break;
            }

            delete CompositePipeline;
            CompositePipeline = CreateCompositePipeline(
                Device,
                CompositeVertexShader,
                CompositePixelShader,
                OffscreenResources.BindGroupLayout,
                EPixelFormat::B8G8R8A8_SRGB);
            if (CompositePipeline == nullptr)
            {
                LE_LOG(LogXBD, Error, "Failed to recreate composite pipeline after resize.");
                break;
            }
        }

        RenderFrame(
            Swapchain,
            CmdList,
            TrianglePipeline,
            CompositePipeline,
            VertexBuffer,
            OffscreenResources,
            GraphInstance,
            Device);
    }

    Device->GetGraphicsQueue()->WaitIdle();

    delete CmdList;
    delete VertexBuffer;
    GraphInstance.Shutdown();
    GraphRuntime.Shutdown();
    delete CompositePipeline;
    delete TrianglePipeline;
    DestroyOffscreenPassResources(OffscreenResources);
    delete CompositePixelShader;
    delete CompositeVertexShader;
    delete TrianglePixelShader;
    delete TriangleVertexShader;
    delete Swapchain;
    delete Device;
    delete Window;

    LE_SHUTDOWN()
    return 0;
}

#endif
