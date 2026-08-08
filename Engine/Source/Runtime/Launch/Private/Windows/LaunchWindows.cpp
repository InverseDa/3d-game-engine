#include "CoreMinimal.h"
#include <fstream>

#if PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include "Windows/WindowsWindow.h"
#endif

#include "RALMinimal.h"
#include "RFGMinimal.h"
#include "Renderer/RendererMinimal.h"
#include "ShaderRuntimeCompiler.h"
#include "World/WorldMinimal.h"

namespace LE
{

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);
LE_DECLARE_LOG_CATEGORY(LogXBD);

// 顶点结构：位置 + 颜色
struct FSimpleVertex
{
    LE::Math::Vector3f Position;
    LE::Math::Vector3f Color;
};

struct FOffscreenPassResources
{
    LE::FRALTexture* Texture = nullptr;
    LE::FRALTextureView* TextureView = nullptr;
    LE::FRALSampler* Sampler = nullptr;
    LE::FRALBindGroupLayout* BindGroupLayout = nullptr;
    LE::FRALBindGroup* BindGroup = nullptr;
};

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
    LE::FRALDevice* Device,
    uint32 Width,
    uint32 Height,
    FOffscreenPassResources& Resources)
{
    DestroyOffscreenPassResources(Resources);

    LE::FRALTextureDesc TextureDesc;
    TextureDesc.Name = "RFGSceneColor";
    TextureDesc.Width = Width;
    TextureDesc.Height = Height;
    TextureDesc.Depth = 1;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArrayLayers = 1;
    TextureDesc.Format = LE::EPixelFormat::B8G8R8A8_SRGB;
    TextureDesc.bIsRenderTarget = true;
    TextureDesc.bIsShaderResource = true;

    Resources.Texture = Device->CreateTexture(TextureDesc);
    if (Resources.Texture == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create offscreen texture.");
        return false;
    }

    LE::FRALTextureViewDesc TextureViewDesc;
    TextureViewDesc.Texture = Resources.Texture;
    TextureViewDesc.Format = TextureDesc.Format;
    TextureViewDesc.MipSlice = 0;
    TextureViewDesc.ArraySlice = 0;
    TextureViewDesc.MipLevels = 1;
    TextureViewDesc.ArrayLayers = 1;
    Resources.TextureView = Device->CreateTextureView(TextureViewDesc);
    if (Resources.TextureView == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create offscreen texture view.");
        DestroyOffscreenPassResources(Resources);
        return false;
    }

    LE::FRALSamplerDesc SamplerDesc;
    SamplerDesc.Name = "RFGSceneColorSampler";
    SamplerDesc.AddressU = LE::ESamplerAddressMode::ClampToEdge;
    SamplerDesc.AddressV = LE::ESamplerAddressMode::ClampToEdge;
    SamplerDesc.AddressW = LE::ESamplerAddressMode::ClampToEdge;
    Resources.Sampler = Device->CreateSampler(SamplerDesc);
    if (Resources.Sampler == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create offscreen sampler.");
        DestroyOffscreenPassResources(Resources);
        return false;
    }

    LE::FRALBindGroupLayoutDesc BindGroupLayoutDesc;
    BindGroupLayoutDesc.Name = "CompositeSceneColorLayout";
    BindGroupLayoutDesc.SetIndex = 0;

    LE::FRALBindGroupLayoutItem LayoutItem;
    LayoutItem.Binding = 0;
    LayoutItem.Count = 1;
    LayoutItem.Type = LE::ERALBindGroupItemType::SampledImage;
    LayoutItem.StageFlags = LE::EShaderStage::Pixel;
    BindGroupLayoutDesc.Bindings.PushBack(LayoutItem);

    LayoutItem.Binding = 1;
    LayoutItem.Type = LE::ERALBindGroupItemType::Sampler;
    BindGroupLayoutDesc.Bindings.PushBack(LayoutItem);

    Resources.BindGroupLayout = Device->CreateBindGroupLayout(BindGroupLayoutDesc);
    if (Resources.BindGroupLayout == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create composite bind group layout.");
        DestroyOffscreenPassResources(Resources);
        return false;
    }

    LE::FRALBindGroupDesc BindGroupDesc;
    BindGroupDesc.Name = "CompositeSceneColorBindGroup";
    BindGroupDesc.Layout = Resources.BindGroupLayout;

    LE::FRALBindGroupItem BindGroupItem;
    BindGroupItem.Binding = 0;
    BindGroupItem.TextureView = Resources.TextureView;
    BindGroupDesc.Items.PushBack(BindGroupItem);

    BindGroupItem = {};
    BindGroupItem.Binding = 1;
    BindGroupItem.Sampler = Resources.Sampler;
    BindGroupDesc.Items.PushBack(BindGroupItem);

    Resources.BindGroup = Device->CreateBindGroup(BindGroupDesc);
    if (Resources.BindGroup == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create composite bind group.");
        DestroyOffscreenPassResources(Resources);
        return false;
    }

    return true;
}

static bool TryReadShaderFile(const LE::String& CandidatePath, LE::Array<char>& OutBuffer)
{
    std::ifstream File(CandidatePath.Data(), std::ios::ate | std::ios::binary);
    if (!File.is_open())
    {
        return false;
    }

    const size_t FileSize = static_cast<size_t>(File.tellg());
    if (FileSize == 0)
    {
        LE_LOG(LogXBD, Error, "Shader is empty: {}", CandidatePath.Data());
        OutBuffer.Clear();
        return true;
    }

    OutBuffer.Resize(FileSize);
    File.seekg(0);
    File.read(OutBuffer.Data(), static_cast<std::streamsize>(FileSize));
    File.close();

    LE_LOG(LogXBD, Info, "Loaded shader: {} ({} bytes)", CandidatePath.Data(), FileSize);
    return true;
}

static LE::String GetParentPath(const LE::String& Path)
{
    if (Path.IsEmpty())
    {
        return {};
    }

    for (size_t Index = Path.Size(); Index > 0; --Index)
    {
        const char Character = Path[Index - 1];
        if (Character == '/' || Character == '\\')
        {
            return LE::String(Path.View().Substr(0, Index - 1));
        }
    }
    return {};
}

static LE::String JoinPath(const LE::String& Left, const LE::StringView Right)
{
    if (Left.IsEmpty())
    {
        return LE::String(Right);
    }

    const char LastChar = Left[Left.Size() - 1];
    LE::String Result(Left);
    if (LastChar == '/' || LastChar == '\\')
    {
        Result.Append(Right);
        return Result;
    }

    Result.Append("/");
    Result.Append(Right);
    return Result;
}

#if PLATFORM_WINDOWS
static LE::String GetExecutableDirectory()
{
    char ModulePath[MAX_PATH] = {};
    const DWORD PathLength = GetModuleFileNameA(nullptr, ModulePath, static_cast<DWORD>(sizeof(ModulePath)));
    if (PathLength == 0 || PathLength >= sizeof(ModulePath))
    {
        return {};
    }

    return GetParentPath(LE::String(ModulePath, PathLength));
}
#endif

// Load SPIRV shader from file
LE::Array<char> ReadShaderFile(const char* Filename)
{
    LE::Array<char> Buffer;

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
        LE::String Candidate(Prefix);
        if (Prefix[0]) { Candidate.Append("/"); }
        Candidate.Append(Filename);
        if (TryReadShaderFile(Candidate, Buffer))
        {
            return Buffer;
        }
    }

#if PLATFORM_WINDOWS
    LE::String SearchBase = GetExecutableDirectory();
    for (int32 i = 0; i < 10 && !SearchBase.IsEmpty(); ++i)
    {
        const LE::String Candidate = JoinPath(SearchBase, Filename);
        if (TryReadShaderFile(Candidate, Buffer))
        {
            return Buffer;
        }
        SearchBase = GetParentPath(SearchBase);
    }
#endif

    LE_LOG(LogXBD, Error, "Failed to open shader (all tried paths): {}", Filename);
    return {};
}

bool IsValidSpirv(const LE::Array<char>& ByteCode, const char* ShaderLabel)
{
    if (ByteCode.IsEmpty())
    {
        LE_LOG(LogXBD, Error, "Shader bytecode is empty: {}", ShaderLabel);
        return false;
    }
    if ((ByteCode.Size() % 4) != 0)
    {
        LE_LOG(LogXBD, Error, "Shader bytecode is not 4-byte aligned: {} (size={})", ShaderLabel, ByteCode.Size());
        return false;
    }
    return true;
}

// Create triangle vertex buffer
LE::FRALBuffer* CreateTriangleVertexBuffer(LE::FRALDevice* Device)
{
    FSimpleVertex Vertices[3] = {
        { LE::Math::Vector3f( 0.0f,  0.5f, 0.0f), LE::Math::Vector3f(1.0f, 0.0f, 0.0f) }, // Top - Red
        { LE::Math::Vector3f( 0.5f, -0.5f, 0.0f), LE::Math::Vector3f(0.0f, 1.0f, 0.0f) }, // Right - Green
        { LE::Math::Vector3f(-0.5f, -0.5f, 0.0f), LE::Math::Vector3f(0.0f, 0.0f, 1.0f) }, // Left - Blue
    };

    const uint64 BufferSize = sizeof(Vertices);

    LE::FRALBufferDesc BufferDesc;
    BufferDesc.Name = "TriangleVertexBuffer";
    BufferDesc.Size = BufferSize;
    BufferDesc.Usage = static_cast<uint32>(LE::EResourceUsage::Upload);
    BufferDesc.UsageFlag = static_cast<uint32>(LE::EBufferUsageFlags::VertexBuffer);

    LE::FRALBuffer* Buffer = Device->CreateBuffer(BufferDesc);

    void* MappedData = Buffer->Map(0, BufferSize);
    memcpy(MappedData, Vertices, BufferSize);
    Buffer->Unmap();

    LE_LOG(LogXBD, Info, "Created triangle vertex buffer");
    return Buffer;
}

static LE::FRALPipeline_Graphics* CreateTrianglePipeline(
    LE::FRALDevice* Device,
    LE::FRALShader* VertexShader,
    LE::FRALShader* PixelShader,
    LE::EPixelFormat RenderTargetFormat)
{
    LE::FRALPipelineDesc_Graphics PipelineDesc;
    PipelineDesc.Name = "TrianglePipeline";
    PipelineDesc.VertexShader = VertexShader;
    PipelineDesc.PixelShader = PixelShader;

    LE::FRALVertexInputBinding Binding;
    Binding.Binding = 0;
    Binding.Stride = sizeof(FSimpleVertex);
    Binding.bPerInstance = false;
    PipelineDesc.VertexBindings.PushBack(Binding);

    LE::FRALVertexInputAttribute PosAttr;
    PosAttr.Location = 0;
    PosAttr.Binding = 0;
    PosAttr.Format = LE::EPixelFormat::R32G32B32_FLOAT;
    PosAttr.Offset = offsetof(FSimpleVertex, Position);
    PipelineDesc.VertexAttributes.PushBack(PosAttr);

    LE::FRALVertexInputAttribute ColorAttr;
    ColorAttr.Location = 1;
    ColorAttr.Binding = 0;
    ColorAttr.Format = LE::EPixelFormat::R32G32B32_FLOAT;
    ColorAttr.Offset = offsetof(FSimpleVertex, Color);
    PipelineDesc.VertexAttributes.PushBack(ColorAttr);

    PipelineDesc.RenderTargetCount = 1;
    PipelineDesc.RenderTargetFormats[0] = RenderTargetFormat;
    PipelineDesc.DepthStencilFormat = LE::EPixelFormat::Unknown;
    PipelineDesc.RasterizerState.CullMode = LE::ECullMode::None;
    PipelineDesc.RasterizerState.FillMode = LE::EFillMode::Solid;
    PipelineDesc.BlendState.bEnable = false;

    return Device->CreateGraphicsPipeline(PipelineDesc);
}

static LE::FRALPipeline_Graphics* CreateCompositePipeline(
    LE::FRALDevice* Device,
    LE::FRALShader* VertexShader,
    LE::FRALShader* PixelShader,
    LE::FRALBindGroupLayout* BindGroupLayout,
    LE::EPixelFormat RenderTargetFormat)
{
    LE::FRALPipelineDesc_Graphics PipelineDesc;
    PipelineDesc.Name = "CompositePipeline";
    PipelineDesc.VertexShader = VertexShader;
    PipelineDesc.PixelShader = PixelShader;
    PipelineDesc.RenderTargetCount = 1;
    PipelineDesc.RenderTargetFormats[0] = RenderTargetFormat;
    PipelineDesc.DepthStencilFormat = LE::EPixelFormat::Unknown;
    PipelineDesc.RasterizerState.CullMode = LE::ECullMode::None;
    PipelineDesc.RasterizerState.FillMode = LE::EFillMode::Solid;
    PipelineDesc.BlendState.bEnable = false;
    PipelineDesc.BindGroupLayouts.PushBack(BindGroupLayout);

    return Device->CreateGraphicsPipeline(PipelineDesc);
}

static void GetBackBufferExtent(
    LE::FRALSwapchain* Swapchain,
    uint32 FallbackWidth,
    uint32 FallbackHeight,
    uint32& OutWidth,
    uint32& OutHeight)
{
    OutWidth = FallbackWidth;
    OutHeight = FallbackHeight;

    if (Swapchain == nullptr)
    {
        return;
    }

    LE::FRALTextureView* BackBufferView = Swapchain->GetCurrentBackBufferView();
    if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
    {
        return;
    }

    const LE::FRALTextureDesc& BackBufferDesc = BackBufferView->GetTexture()->GetDesc();
    OutWidth = BackBufferDesc.Width;
    OutHeight = BackBufferDesc.Height;
}

static bool SyncSwapchainToWindowSize(
    FPlatformWindow* Window,
    LE::FRALSwapchain* Swapchain,
    uint32& InOutWidth,
    uint32& InOutHeight)
{
    uint32 CurrentWindowWidth = 0;
    uint32 CurrentWindowHeight = 0;
    Window->GetSize(CurrentWindowWidth, CurrentWindowHeight);
    if (CurrentWindowWidth == 0 || CurrentWindowHeight == 0)
    {
        return false;
    }

    if (CurrentWindowWidth != InOutWidth || CurrentWindowHeight != InOutHeight)
    {
        LE_LOG(LogXBD, Info, "Window resized to {}x{}. Recreating swapchain.", CurrentWindowWidth, CurrentWindowHeight);
        Swapchain->Resize(CurrentWindowWidth, CurrentWindowHeight);
        InOutWidth = CurrentWindowWidth;
        InOutHeight = CurrentWindowHeight;
    }

    return true;
}

int32 GuardedMain()
{
    LE_INIT()
    LE_LOG(LogXBD, Info, "=== Limitless Engine P1: RFG Composite Triangle Demo ===")

#if !PLATFORM_WINDOWS
    LE_LOG(LogXBD, Error, "Launch module is not implemented for this platform yet.");
    LE_SHUTDOWN()
    return -1;
#else
    // 创建窗口（使用平台抽象）
    const uint32 WindowWidth = 1280;
    const uint32 WindowHeight = 720;
    FPlatformWindow* Window = new FWindowsWindow(WindowWidth, WindowHeight, "Limitless Engine - RFG Composite Triangle Demo");
    if (!Window) {
        LE_LOG(LogXBD, Error, "Failed to create window");
        return -1;
    }
    LE_LOG(LogXBD, Info, "Window created: {}x{}", WindowWidth, WindowHeight);

    // 创建 RAL 设备
    LE::FRALDevice* Device = LE::RAL::CreateDevice();
    if (!Device) {
        LE_LOG(LogXBD, Error, "Failed to create RAL Device");
        delete Window;
        return -1;
    }
    LE_LOG(LogXBD, Info, "RAL Device created");

    // 创建交换链
    LE::FRALSwapchainDesc SwapchainDesc;
    SwapchainDesc.Surface = Window->GetSurfaceDesc();
    SwapchainDesc.Width = WindowWidth;
    SwapchainDesc.Height = WindowHeight;
    SwapchainDesc.BackBufferFormat = LE::EPixelFormat::B8G8R8A8_SRGB;
    SwapchainDesc.bEnableVsync = true;
    LE::FRALSwapchain* Swapchain = Device->CreateSwapchain(SwapchainDesc);
    if (!Swapchain)
    {
        LE_LOG(LogXBD, Error, "Failed to create swapchain");
        delete Device;
        delete Window;
        LE_SHUTDOWN()
        return -1;
    }
    LE_LOG(LogXBD, Info, "Swapchain created");

    LE::Array<char> TriangleVertBytecode;
    LE::Array<char> TriangleFragBytecode;
    LE::Array<char> CompositeVertBytecode;
    LE::Array<char> CompositeFragBytecode;
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

    LE::FRALShader* TriangleVertexShader = Device->CreateShaderFromFile(
        LE::EShaderStage::Vertex, TriangleVertBytecode.Data(), TriangleVertBytecode.Size(), "MainVS");
    LE::FRALShader* TrianglePixelShader = Device->CreateShaderFromFile(
        LE::EShaderStage::Pixel, TriangleFragBytecode.Data(), TriangleFragBytecode.Size(), "MainPS");
    LE::FRALShader* CompositeVertexShader = Device->CreateShaderFromFile(
        LE::EShaderStage::Vertex, CompositeVertBytecode.Data(), CompositeVertBytecode.Size(), "MainVS");
    LE::FRALShader* CompositePixelShader = Device->CreateShaderFromFile(
        LE::EShaderStage::Pixel, CompositeFragBytecode.Data(), CompositeFragBytecode.Size(), "MainPS");
    if (!TriangleVertexShader || !TrianglePixelShader || !CompositeVertexShader || !CompositePixelShader)
    {
        LE_LOG(LogXBD, Error, "Failed to create shader modules.");
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

    LE::FRALPipeline_Graphics* TrianglePipeline = CreateTrianglePipeline(
        Device,
        TriangleVertexShader,
        TrianglePixelShader,
        LE::EPixelFormat::B8G8R8A8_SRGB);
    LE::FRALPipeline_Graphics* CompositePipeline = CreateCompositePipeline(
        Device,
        CompositeVertexShader,
        CompositePixelShader,
        OffscreenResources.BindGroupLayout,
        LE::EPixelFormat::B8G8R8A8_SRGB);
    if (!TrianglePipeline || !CompositePipeline)
    {
        LE_LOG(LogXBD, Error, "Failed to create composite demo pipelines.");
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
    LE_LOG(LogXBD, Info, "Composite demo pipelines created");

    // Vertex buffer
    LE::FRALBuffer* VertexBuffer = CreateTriangleVertexBuffer(Device);

    // Command list
    LE::FRALCommandList* CmdList = Device->CreateCommandList(LE::EQueueType::Graphics);
    LE::FRenderer Renderer;
    Renderer.Initialize();
    uint32 CachedWindowWidth = WindowWidth;
    uint32 CachedWindowHeight = WindowHeight;

    LE_LOG(LogXBD, Info, "Entering composite main loop...");

    // 主循环
    while (Window->ProcessMessages())
    {
        if (Window->IsMinimized() || !SyncSwapchainToWindowSize(Window, Swapchain, CachedWindowWidth, CachedWindowHeight))
        {
            Sleep(16);
            continue;
        }

        uint32 TargetRenderWidth = CachedWindowWidth;
        uint32 TargetRenderHeight = CachedWindowHeight;
        GetBackBufferExtent(Swapchain, CachedWindowWidth, CachedWindowHeight, TargetRenderWidth, TargetRenderHeight);

        if (OffscreenResources.Texture == nullptr ||
            OffscreenResources.Texture->GetDesc().Width != TargetRenderWidth ||
            OffscreenResources.Texture->GetDesc().Height != TargetRenderHeight)
        {
            Device->GetGraphicsQueue()->WaitIdle();
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
                LE::EPixelFormat::B8G8R8A8_SRGB);
            if (CompositePipeline == nullptr)
            {
                LE_LOG(LogXBD, Error, "Failed to recreate composite pipeline after resize.");
                break;
            }
        }

        LE::FWorld World;
        LE::FWorldMeshComponent MeshComponent;
        MeshComponent.DebugName = "TriangleMesh";
        MeshComponent.GraphicsPipeline = TrianglePipeline;
        MeshComponent.VertexBuffer = VertexBuffer;
        MeshComponent.VertexCount = 3;
        MeshComponent.PassMask = LE::ERenderMeshPassMask::SceneColor;
        MeshComponent.SortKey = 0;
        World.MeshComponents.PushBack(MeshComponent);

        LE::FRenderScene RenderScene;
        LE::FWorldRenderSceneExtractor::ExtractRenderScene(World, RenderScene);

        LE::FRendererFrameContext FrameContext;
        FrameContext.Device = Device;
        FrameContext.Swapchain = Swapchain;
        FrameContext.CommandList = CmdList;
        FrameContext.ViewFamily.PrimarySwapchain = Swapchain;
        LE::FTriangleCompositePipelineDesc RenderPipelineDesc;
        RenderPipelineDesc.Swapchain = Swapchain;
        RenderPipelineDesc.CompositePipeline = CompositePipeline;
        RenderPipelineDesc.SceneColorTexture = OffscreenResources.Texture;
        RenderPipelineDesc.SceneColorView = OffscreenResources.TextureView;
        RenderPipelineDesc.CompositeBindGroup = OffscreenResources.BindGroup;
        LE::FTriangleCompositePipeline RenderPipeline(RenderPipelineDesc);
        FrameContext.Pipeline = &RenderPipeline;
        Renderer.RenderFrame(FrameContext, &RenderScene);
    }

    // 清理资源
    LE_LOG(LogXBD, Info, "Shutting down...");
    Device->GetGraphicsQueue()->WaitIdle();
    Renderer.Shutdown();

    delete CmdList;
    delete VertexBuffer;
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
    LE_LOG(LogXBD, Info, "Goodbye!");
    return 0;
#endif
}

} // namespace LE
