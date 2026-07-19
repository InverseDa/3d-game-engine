#include "CoreMinimal.h"
#include <fstream>
#include <string>
#include <vector>

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

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);
LE_DECLARE_LOG_CATEGORY(LogXBD);

// 顶点结构：位置 + 颜色
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
        return false;
    }

    FRALTextureViewDesc TextureViewDesc;
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

#if PLATFORM_WINDOWS
static std::string GetExecutableDirectory()
{
    char ModulePath[MAX_PATH] = {};
    const DWORD PathLength = GetModuleFileNameA(nullptr, ModulePath, static_cast<DWORD>(sizeof(ModulePath)));
    if (PathLength == 0 || PathLength >= sizeof(ModulePath))
    {
        return std::string();
    }

    return GetParentPath(std::string(ModulePath, PathLength));
}
#endif

// Load SPIRV shader from file
std::vector<char> ReadShaderFile(const char* Filename)
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

#if PLATFORM_WINDOWS
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
#endif

    LE_LOG(LogXBD, Error, "Failed to open shader (all tried paths): {}", Filename);
    return {};
}

bool IsValidSpirv(const std::vector<char>& ByteCode, const char* ShaderLabel)
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

// Create triangle vertex buffer
FRALBuffer* CreateTriangleVertexBuffer(FRALDevice* Device)
{
    FSimpleVertex Vertices[3] = {
        { FVector3f( 0.0f,  0.5f, 0.0f), FVector3f(1.0f, 0.0f, 0.0f) }, // Top - Red
        { FVector3f( 0.5f, -0.5f, 0.0f), FVector3f(0.0f, 1.0f, 0.0f) }, // Right - Green
        { FVector3f(-0.5f, -0.5f, 0.0f), FVector3f(0.0f, 0.0f, 1.0f) }, // Left - Blue
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

    LE_LOG(LogXBD, Info, "Created triangle vertex buffer");
    return Buffer;
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

static void GetBackBufferExtent(
    FRALSwapchain* Swapchain,
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

    FRALTextureView* BackBufferView = Swapchain->GetCurrentBackBufferView();
    if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
    {
        return;
    }

    const FRALTextureDesc& BackBufferDesc = BackBufferView->GetTexture()->GetDesc();
    OutWidth = BackBufferDesc.Width;
    OutHeight = BackBufferDesc.Height;
}

static bool SyncSwapchainToWindowSize(
    FPlatformWindow* Window,
    FRALSwapchain* Swapchain,
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
    FRALDevice* Device = RAL::CreateDevice();
    if (!Device) {
        LE_LOG(LogXBD, Error, "Failed to create RAL Device");
        delete Window;
        return -1;
    }
    LE_LOG(LogXBD, Info, "RAL Device created");

    // 创建交换链
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
    LE_LOG(LogXBD, Info, "Swapchain created");

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
    FRALBuffer* VertexBuffer = CreateTriangleVertexBuffer(Device);

    // Command list
    FRALCommandList* CmdList = Device->CreateCommandList(EQueueType::Graphics);
    FRenderer Renderer;
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
                EPixelFormat::B8G8R8A8_SRGB);
            if (CompositePipeline == nullptr)
            {
                LE_LOG(LogXBD, Error, "Failed to recreate composite pipeline after resize.");
                break;
            }
        }

        FWorld World;
        FWorldMeshComponent MeshComponent;
        MeshComponent.DebugName = "TriangleMesh";
        MeshComponent.GraphicsPipeline = TrianglePipeline;
        MeshComponent.VertexBuffer = VertexBuffer;
        MeshComponent.VertexCount = 3;
        MeshComponent.PassMask = ERenderMeshPassMask::SceneColor;
        MeshComponent.SortKey = 0;
        World.MeshComponents.push_back(MeshComponent);

        FRenderScene RenderScene;
        FWorldRenderSceneExtractor::ExtractRenderScene(World, RenderScene);

        FRendererFrameContext FrameContext;
        FrameContext.Device = Device;
        FrameContext.Swapchain = Swapchain;
        FrameContext.CommandList = CmdList;
        FrameContext.ViewFamily.PrimarySwapchain = Swapchain;
        FTriangleCompositePipelineDesc RenderPipelineDesc;
        RenderPipelineDesc.Swapchain = Swapchain;
        RenderPipelineDesc.CompositePipeline = CompositePipeline;
        RenderPipelineDesc.SceneColorTexture = OffscreenResources.Texture;
        RenderPipelineDesc.SceneColorView = OffscreenResources.TextureView;
        RenderPipelineDesc.CompositeBindGroup = OffscreenResources.BindGroup;
        FTriangleCompositePipeline RenderPipeline(RenderPipelineDesc);
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
