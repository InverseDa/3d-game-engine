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

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);
LE_DECLARE_LOG_CATEGORY(LogXBD);

struct FSimpleVertex
{
    FVector3f Position;
    FVector3f Color;
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

static void RenderFrame(
    FRALSwapchain* Swapchain,
    FRALCommandList* CmdList,
    FRALPipeline_Graphics* Pipeline,
    FRALBuffer* VertexBuffer,
    FRALDevice* Device)
{
    FRALTextureView* BackBufferView = Swapchain->GetCurrentBackBufferView();
    if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
    {
        LE_LOG(LogXBD, Warn, "Skipping frame because the swapchain back buffer is not ready yet.");
        return;
    }

    CmdList->Begin();
    CmdList->SetGraphicsPipeline(Pipeline);

    FRALRenderPassDesc RenderPassDesc{};
    RenderPassDesc.ColorAttachmentCount = 1;
    RenderPassDesc.ColorAttachments[0].RenderTarget = BackBufferView;
    RenderPassDesc.ColorAttachments[0].LoadOp = EAttachmentLoadOp::Clear;
    RenderPassDesc.ColorAttachments[0].StoreOp = EAttachmentStoreOp::Store;
    RenderPassDesc.ColorAttachments[0].ClearColor[0] = 0.1f;
    RenderPassDesc.ColorAttachments[0].ClearColor[1] = 0.1f;
    RenderPassDesc.ColorAttachments[0].ClearColor[2] = 0.1f;
    RenderPassDesc.ColorAttachments[0].ClearColor[3] = 1.0f;
    RenderPassDesc.bHasDepthStencil = false;

    CmdList->BeginRenderPass(RenderPassDesc);

    const FRALTextureDesc& TexDesc = BackBufferView->GetTexture()->GetDesc();
    FRALViewport Viewport;
    Viewport.X = 0.0f;
    Viewport.Y = 0.0f;
    Viewport.Width = static_cast<float>(TexDesc.Width);
    Viewport.Height = static_cast<float>(TexDesc.Height);
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
    CmdList->SetViewport(Viewport);

    FRALScissorRect Scissor;
    Scissor.X = 0;
    Scissor.Y = 0;
    Scissor.Width = TexDesc.Width;
    Scissor.Height = TexDesc.Height;
    CmdList->SetScissorRect(Scissor);

    CmdList->SetVertexBuffer(0, VertexBuffer, 0);
    CmdList->Draw(3, 1, 0);
    CmdList->EndRenderPass();
    CmdList->End();

    FRALSubmitInfo SubmitInfo;
    SubmitInfo.CmdList = CmdList;
    Device->GetGraphicsQueue()->Submit(SubmitInfo);
    Device->GetGraphicsQueue()->WaitIdle();

    Swapchain->Present();
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

    auto VertBytecode = ReadShaderFile("Engine/Content/Shaders/triangle.vert.spv");
    auto FragBytecode = ReadShaderFile("Engine/Content/Shaders/triangle.frag.spv");
    if (!IsValidSpirv(VertBytecode, "triangle.vert.spv") || !IsValidSpirv(FragBytecode, "triangle.frag.spv"))
    {
        delete Swapchain;
        delete Device;
        delete Window;
        LE_SHUTDOWN()
        return -1;
    }

    FRALShader* VertexShader = Device->CreateShaderFromFile(
        EShaderStage::Vertex, VertBytecode.data(), VertBytecode.size());
    FRALShader* PixelShader = Device->CreateShaderFromFile(
        EShaderStage::Pixel, FragBytecode.data(), FragBytecode.size());
    if (!VertexShader || !PixelShader)
    {
        delete PixelShader;
        delete VertexShader;
        delete Swapchain;
        delete Device;
        delete Window;
        LE_SHUTDOWN()
        return -1;
    }

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
    PipelineDesc.RenderTargetFormats[0] = EPixelFormat::B8G8R8A8_SRGB;
    PipelineDesc.DepthStencilFormat = EPixelFormat::Unknown;
    PipelineDesc.RasterizerState.CullMode = ECullMode::None;
    PipelineDesc.RasterizerState.FillMode = EFillMode::Solid;
    PipelineDesc.BlendState.bEnable = false;

    FRALPipeline_Graphics* Pipeline = Device->CreateGraphicsPipeline(PipelineDesc);
    if (!Pipeline)
    {
        delete PixelShader;
        delete VertexShader;
        delete Swapchain;
        delete Device;
        delete Window;
        LE_SHUTDOWN()
        return -1;
    }

    FRALBuffer* VertexBuffer = CreateTriangleVertexBuffer(Device);
    FRALCommandList* CmdList = Device->CreateCommandList(EQueueType::Graphics);
    uint32 CachedWindowWidth = WindowWidth;
    uint32 CachedWindowHeight = WindowHeight;

    while (Window->ProcessMessages())
    {
        if (Window->IsMinimized() || !SyncSwapchainToWindowSize(Window, Swapchain, CachedWindowWidth, CachedWindowHeight))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        RenderFrame(Swapchain, CmdList, Pipeline, VertexBuffer, Device);
    }

    Device->GetGraphicsQueue()->WaitIdle();

    delete CmdList;
    delete VertexBuffer;
    delete Pipeline;
    delete PixelShader;
    delete VertexShader;
    delete Swapchain;
    delete Device;
    delete Window;

    LE_SHUTDOWN()
    return 0;
}

#endif
