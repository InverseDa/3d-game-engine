#include "DemoApplication/DemoApplication.h"

#include "Application/RuntimeModule.h"
#include "CoreMinimal.h"
#include "DemoSettings.h"

#include "Platform/Platform.h"
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
    RAL::DestroyResource(Resources.BindGroup);
    RAL::DestroyResource(Resources.BindGroupLayout);
    RAL::DestroyResource(Resources.Sampler);
    RAL::DestroyResource(Resources.TextureView);
    RAL::DestroyResource(Resources.Texture);

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
    uint32& InOutHeight,
    const bool bForceRecreate)
{
    uint32 CurrentWindowWidth = 0;
    uint32 CurrentWindowHeight = 0;
    const FPlatformWindowSize WindowSize = Window->GetSize();
    CurrentWindowWidth = WindowSize.Width;
    CurrentWindowHeight = WindowSize.Height;
    if (CurrentWindowWidth == 0 || CurrentWindowHeight == 0)
    {
        return false;
    }

    if (bForceRecreate || CurrentWindowWidth != InOutWidth || CurrentWindowHeight != InOutHeight)
    {
        LE_LOG(LogXBD, Info, "Explicitly recreating swapchain at {}x{}.", CurrentWindowWidth, CurrentWindowHeight);
        if (!Swapchain->Resize(CurrentWindowWidth, CurrentWindowHeight))
        {
            return false;
        }
        InOutWidth = CurrentWindowWidth;
        InOutHeight = CurrentWindowHeight;
    }

    return true;
}

namespace
{
class FDemoWindowRuntimeModule final
{
public:
    ~FDemoWindowRuntimeModule()
    {
        Shutdown();
    }

    bool Startup() noexcept
    {
        LE_LOG(LogXBD, Info, "=== Limitless Engine P1: RFG Composite Triangle Demo ===")

        if (RegisterDemoApplicationReflection(ReflectionRegistry, ReflectionHandle) !=
            EReflectionRegisterResult::Success)
        {
            LE_LOG(LogXBD, Error, "Failed to register demo settings reflection");
            return false;
        }
        bReflectionRegistered = true;

        const FTypeInfo* const SettingsType = ReflectionRegistry.FindType("LE.FDemoSettings");
        FDemoSettings Defaults;
        FPropertyBag Captured;
        Array<uint8> Encoded;
        if (SettingsType == nullptr ||
            TryCapturePropertyBag(
                ReflectionRegistry, SettingsType->Id, SettingsSchemaVersion, &Defaults, Captured) !=
                EPropertySerializationResult::Success ||
            TryEncodePropertyBag(Captured, Encoded) != EPropertySerializationResult::Success ||
            TryDecodePropertyBag(
                Span<const uint8>(Encoded.Data(), Encoded.Size()), DecodedSettings) !=
                EPropertySerializationResult::Success ||
            ValidatePropertyBag(ReflectionRegistry, DecodedSettings) !=
                EPropertySerializationResult::Success)
        {
            LE_LOG(LogXBD, Error, "Failed to capture and round-trip demo settings");
            CleanupReflection();
            return false;
        }

        const FPropertyInfo* const WidthProperty = SettingsType->FindProperty("WindowWidth");
        const FPropertyInfo* const HeightProperty = SettingsType->FindProperty("WindowHeight");
        const FPropertyInfo* const TitleProperty = SettingsType->FindProperty("WindowTitle");
        uint32 Width = 0;
        uint32 Height = 0;
        StringView Title;
        const FPropertyValue* const WidthValue = WidthProperty == nullptr
            ? nullptr : DecodedSettings.Find(WidthProperty->Id);
        const FPropertyValue* const HeightValue = HeightProperty == nullptr
            ? nullptr : DecodedSettings.Find(HeightProperty->Id);
        const FPropertyValue* const TitleValue = TitleProperty == nullptr
            ? nullptr : DecodedSettings.Find(TitleProperty->Id);
        if (WidthValue == nullptr || HeightValue == nullptr || TitleValue == nullptr ||
            !WidthValue->TryGetUInt32(Width) || !HeightValue->TryGetUInt32(Height) ||
            !TitleValue->TryGetString(Title) || Width == 0 || Height == 0)
        {
            LE_LOG(LogXBD, Error, "Decoded demo settings are incomplete or invalid");
            CleanupReflection();
            return false;
        }

        FPlatformWindowDesc WindowDesc;
        WindowDesc.Width = Width;
        WindowDesc.Height = Height;
        WindowDesc.Title = Title;
        Window = CreatePlatformWindow(WindowDesc);
        if (!Window)
        {
            LE_LOG(LogXBD, Error, "Failed to create window");
            CleanupReflection();
            return false;
        }
        return true;
    }

    void Shutdown() noexcept
    {
        if (bShutdown)
        {
            return;
        }
        bShutdown = true;
        CleanupReflection();
        Window.Reset();
        PlatformEvents.Clear();
    }

    EApplicationTickResult ProcessPlatformEvents()
    {
        Window->PumpEvents(PlatformEvents);
        FPlatformEvent PlatformEvent;
        while (PlatformEvents.Poll(PlatformEvent))
        {
            if (PlatformEvent.WindowId == Window->GetId() &&
                PlatformEvent.Type == EPlatformEventType::CloseRequested)
            {
                return EApplicationTickResult::Exit;
            }
        }
        return Window->IsCloseRequested()
            ? EApplicationTickResult::Exit
            : EApplicationTickResult::Continue;
    }

    FPlatformWindow* GetWindow() const noexcept { return Window.Get(); }

    static bool StartupThunk(void* Context) noexcept
    {
        return static_cast<FDemoWindowRuntimeModule*>(Context)->Startup();
    }

    static void ShutdownThunk(void* Context) noexcept
    {
        static_cast<FDemoWindowRuntimeModule*>(Context)->Shutdown();
    }

private:
    void CleanupReflection() noexcept
    {
        if (bReflectionRegistered)
        {
            static_cast<void>(
                UnregisterDemoApplicationReflection(ReflectionRegistry, ReflectionHandle));
            ReflectionHandle.Reset();
            bReflectionRegistered = false;
        }
        DecodedSettings.Reset();
    }

    static constexpr uint32 SettingsSchemaVersion = 1;
    FReflectionRegistry ReflectionRegistry;
    FReflectionModuleHandle ReflectionHandle;
    FPropertyBag DecodedSettings;
    FPlatformWindowPtr Window;
    FPlatformEventQueue PlatformEvents;
    bool bReflectionRegistered = false;
    bool bShutdown = false;
};

class FDemoRenderRuntimeModule final
{
public:
    explicit FDemoRenderRuntimeModule(FDemoWindowRuntimeModule& InWindowModule) noexcept
        : WindowModule(InWindowModule)
    {
    }

    ~FDemoRenderRuntimeModule()
    {
        Shutdown();
    }

    bool Startup() noexcept
    {
        FPlatformWindow* const Window = WindowModule.GetWindow();
        if (Window == nullptr)
        {
            LE_LOG(LogXBD, Error, "Render module requires an active window module");
            return false;
        }

        Device = LE::RAL::CreateDevice();
        if (Device == nullptr)
        {
            LE_LOG(LogXBD, Error, "Failed to create RAL Device");
            return false;
        }

        LE::FRALSwapchainDesc SwapchainDesc;
        SwapchainDesc.Surface = Window->GetSurface();
        SwapchainDesc.Width = DefaultWindowWidth;
        SwapchainDesc.Height = DefaultWindowHeight;
        SwapchainDesc.BackBufferFormat = LE::EPixelFormat::B8G8R8A8_SRGB;
        SwapchainDesc.bEnableVsync = true;
        Swapchain = Device->CreateSwapchain(SwapchainDesc);
        if (Swapchain == nullptr)
        {
            LE_LOG(LogXBD, Error, "Failed to create swapchain");
            return false;
        }

        LE::Array<char> TriangleVertBytecode;
        LE::Array<char> TriangleFragBytecode;
        LE::Array<char> CompositeVertBytecode;
        LE::Array<char> CompositeFragBytecode;
        if (!Demo::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/TriangleVS.lsf", ERuntimeShaderStage::Vertex, TriangleVertBytecode, "MainVS") ||
            !Demo::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/TrianglePS.lsf", ERuntimeShaderStage::Fragment, TriangleFragBytecode, "MainPS") ||
            !Demo::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/CompositeVS.lsf", ERuntimeShaderStage::Vertex, CompositeVertBytecode, "MainVS") ||
            !Demo::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/CompositePS.lsf", ERuntimeShaderStage::Fragment, CompositeFragBytecode, "MainPS"))
        {
            return false;
        }

        TriangleVertexShader = Device->CreateShaderFromFile(
            LE::EShaderStage::Vertex, TriangleVertBytecode.Data(), TriangleVertBytecode.Size(), "MainVS");
        TrianglePixelShader = Device->CreateShaderFromFile(
            LE::EShaderStage::Pixel, TriangleFragBytecode.Data(), TriangleFragBytecode.Size(), "MainPS");
        CompositeVertexShader = Device->CreateShaderFromFile(
            LE::EShaderStage::Vertex, CompositeVertBytecode.Data(), CompositeVertBytecode.Size(), "MainVS");
        CompositePixelShader = Device->CreateShaderFromFile(
            LE::EShaderStage::Pixel, CompositeFragBytecode.Data(), CompositeFragBytecode.Size(), "MainPS");
        if (TriangleVertexShader == nullptr || TrianglePixelShader == nullptr ||
            CompositeVertexShader == nullptr || CompositePixelShader == nullptr)
        {
            LE_LOG(LogXBD, Error, "Failed to create shader modules.");
            return false;
        }

        uint32 InitialRenderWidth = DefaultWindowWidth;
        uint32 InitialRenderHeight = DefaultWindowHeight;
        GetBackBufferExtent(Swapchain, DefaultWindowWidth, DefaultWindowHeight, InitialRenderWidth, InitialRenderHeight);
        if (!CreateOffscreenPassResources(Device, InitialRenderWidth, InitialRenderHeight, OffscreenResources))
        {
            return false;
        }

        TrianglePipeline = CreateTrianglePipeline(
            Device, TriangleVertexShader, TrianglePixelShader, LE::EPixelFormat::B8G8R8A8_SRGB);
        CompositePipeline = CreateCompositePipeline(
            Device,
            CompositeVertexShader,
            CompositePixelShader,
            OffscreenResources.BindGroupLayout,
            LE::EPixelFormat::B8G8R8A8_SRGB);
        if (TrianglePipeline == nullptr || CompositePipeline == nullptr)
        {
            LE_LOG(LogXBD, Error, "Failed to create composite demo pipelines.");
            return false;
        }

        VertexBuffer = CreateTriangleVertexBuffer(Device);
        if (VertexBuffer == nullptr || !FrameScheduler.Initialize(Device))
        {
            LE_LOG(LogXBD, Error, "Failed to create demo frame scheduler resources.");
            return false;
        }

        Renderer.Initialize();
        bRendererInitialized = true;
        CachedWindowWidth = DefaultWindowWidth;
        CachedWindowHeight = DefaultWindowHeight;
        bCanRender = true;
        return true;
    }

    void Shutdown() noexcept
    {
        if (bShutdown)
        {
            return;
        }
        bShutdown = true;

        LE_LOG(LogXBD, Info, "Shutting down demo render runtime...");
        FrameScheduler.Shutdown();
        if (bRendererInitialized)
        {
            Renderer.Shutdown();
            bRendererInitialized = false;
        }

        RAL::DestroyResource(VertexBuffer);
        VertexBuffer = nullptr;
        RAL::DestroyResource(CompositePipeline);
        CompositePipeline = nullptr;
        RAL::DestroyResource(TrianglePipeline);
        TrianglePipeline = nullptr;
        DestroyOffscreenPassResources(OffscreenResources);
        RAL::DestroyResource(CompositePixelShader);
        CompositePixelShader = nullptr;
        RAL::DestroyResource(CompositeVertexShader);
        CompositeVertexShader = nullptr;
        RAL::DestroyResource(TrianglePixelShader);
        TrianglePixelShader = nullptr;
        RAL::DestroyResource(TriangleVertexShader);
        TriangleVertexShader = nullptr;
        RAL::DestroyResource(Swapchain);
        Swapchain = nullptr;
        RAL::DestroyResource(Device);
        Device = nullptr;
    }

    EApplicationTickResult Update()
    {
        FPlatformWindow* const Window = WindowModule.GetWindow();
        bCanRender = !Window->IsMinimized() &&
            SyncSwapchainToWindowSize(
                Window,
                Swapchain,
                CachedWindowWidth,
                CachedWindowHeight,
                bSwapchainRecreateRequested);
        if (!bCanRender)
        {
            FPlatformTime::SleepForMilliseconds(16);
            return EApplicationTickResult::Continue;
        }
        bSwapchainRecreateRequested = false;

        uint32 TargetRenderWidth = CachedWindowWidth;
        uint32 TargetRenderHeight = CachedWindowHeight;
        GetBackBufferExtent(Swapchain, CachedWindowWidth, CachedWindowHeight, TargetRenderWidth, TargetRenderHeight);
        if (OffscreenResources.Texture != nullptr &&
            OffscreenResources.Texture->GetDesc().Width == TargetRenderWidth &&
            OffscreenResources.Texture->GetDesc().Height == TargetRenderHeight)
        {
            return EApplicationTickResult::Continue;
        }

        FrameScheduler.WaitForAllFrames();
        if (!CreateOffscreenPassResources(Device, TargetRenderWidth, TargetRenderHeight, OffscreenResources))
        {
            return EApplicationTickResult::Exit;
        }

        RAL::DestroyResource(CompositePipeline);
        CompositePipeline = CreateCompositePipeline(
            Device,
            CompositeVertexShader,
            CompositePixelShader,
            OffscreenResources.BindGroupLayout,
            LE::EPixelFormat::B8G8R8A8_SRGB);
        if (CompositePipeline == nullptr)
        {
            LE_LOG(LogXBD, Error, "Failed to recreate composite pipeline after resize.");
            return EApplicationTickResult::Exit;
        }
        return EApplicationTickResult::Continue;
    }

    EApplicationTickResult Render()
    {
        if (!bCanRender)
        {
            return EApplicationTickResult::Continue;
        }

        auto RecordFrame = [this](LE::FRenderFrameScope& ScheduledFrame)
        {
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
            FrameContext.FrameIndex = ScheduledFrame.GetFrameIndex();
            FrameContext.Device = Device;
            FrameContext.Swapchain = Swapchain;
            FrameContext.CommandList = ScheduledFrame.GetCommandList();
            FrameContext.FrameScope = &ScheduledFrame;
            FrameContext.ViewFamily.PrimarySwapchain = Swapchain;
            LE::FTriangleCompositePipelineDesc RenderPipelineDesc;
            RenderPipelineDesc.BackBufferView = ScheduledFrame.GetBackBufferView();
            RenderPipelineDesc.CompositePipeline = CompositePipeline;
            RenderPipelineDesc.SceneColorTexture = OffscreenResources.Texture;
            RenderPipelineDesc.SceneColorView = OffscreenResources.TextureView;
            RenderPipelineDesc.CompositeBindGroup = OffscreenResources.BindGroup;
            LE::FTriangleCompositePipeline RenderPipeline(RenderPipelineDesc);
            FrameContext.Pipeline = &RenderPipeline;
            return Renderer.RenderFrame(FrameContext, &RenderScene);
        };

        const FRenderFrameResult FrameResult = FrameScheduler.ExecuteFrame(Swapchain, RecordFrame);
        if (FrameResult.Action == ERenderFrameAction::RecreateSwapchain)
        {
            bSwapchainRecreateRequested = true;
            return EApplicationTickResult::Continue;
        }
        if (FrameResult.Action == ERenderFrameAction::Exit)
        {
            LE_LOG(LogXBD, Error,
                "Explicit frame failed. Acquire={}, Submit={}, Present={}.",
                static_cast<uint32>(FrameResult.AcquireStatus),
                static_cast<uint32>(FrameResult.SubmitResult),
                static_cast<uint32>(FrameResult.PresentStatus));
            return EApplicationTickResult::Exit;
        }
        return EApplicationTickResult::Continue;
    }

    static bool StartupThunk(void* Context) noexcept
    {
        return static_cast<FDemoRenderRuntimeModule*>(Context)->Startup();
    }

    static void ShutdownThunk(void* Context) noexcept
    {
        static_cast<FDemoRenderRuntimeModule*>(Context)->Shutdown();
    }

private:
    static constexpr uint32 DefaultWindowWidth = 1280;
    static constexpr uint32 DefaultWindowHeight = 720;

    FDemoWindowRuntimeModule& WindowModule;
    FRALDevice* Device = nullptr;
    FRALSwapchain* Swapchain = nullptr;
    FRALShader* TriangleVertexShader = nullptr;
    FRALShader* TrianglePixelShader = nullptr;
    FRALShader* CompositeVertexShader = nullptr;
    FRALShader* CompositePixelShader = nullptr;
    FRALPipeline_Graphics* TrianglePipeline = nullptr;
    FRALPipeline_Graphics* CompositePipeline = nullptr;
    FRALBuffer* VertexBuffer = nullptr;
    FRenderFrameScheduler FrameScheduler;
    FOffscreenPassResources OffscreenResources;
    FRenderer Renderer;
    uint32 CachedWindowWidth = DefaultWindowWidth;
    uint32 CachedWindowHeight = DefaultWindowHeight;
    bool bCanRender = false;
    bool bSwapchainRecreateRequested = false;
    bool bRendererInitialized = false;
    bool bShutdown = false;
};

class FDemoApplication final : public IApplication
{
public:
    FDemoApplication() noexcept
        : RenderModule(WindowModule)
    {
    }

    ~FDemoApplication() override
    {
        Shutdown();
    }

    void RegisterModules(FRuntimeModuleRegistry& Registry) override
    {
        FRuntimeModuleDescriptor WindowDescriptor;
        WindowDescriptor.Name = "Demo.Window";
        WindowDescriptor.Context = &WindowModule;
        WindowDescriptor.Startup = &FDemoWindowRuntimeModule::StartupThunk;
        WindowDescriptor.Shutdown = &FDemoWindowRuntimeModule::ShutdownThunk;
        static_cast<void>(Registry.RegisterModule(WindowDescriptor));

        const StringView RenderDependencies[] = { "Demo.Window" };
        FRuntimeModuleDescriptor RenderDescriptor;
        RenderDescriptor.Name = "Demo.Render";
        RenderDescriptor.Dependencies = Span<const StringView>(RenderDependencies);
        RenderDescriptor.Context = &RenderModule;
        RenderDescriptor.Startup = &FDemoRenderRuntimeModule::StartupThunk;
        RenderDescriptor.Shutdown = &FDemoRenderRuntimeModule::ShutdownThunk;
        static_cast<void>(Registry.RegisterModule(RenderDescriptor));
    }

    bool Initialize() override
    {
        LE_LOG(LogXBD, Info, "Entering composite main loop...");
        return true;
    }

    EApplicationTickResult Tick(const FApplicationTickContext& Context) override
    {
        switch (Context.Phase)
        {
        case EApplicationFramePhase::ProcessPlatformEvents:
            return WindowModule.ProcessPlatformEvents();
        case EApplicationFramePhase::Update:
            return RenderModule.Update();
        case EApplicationFramePhase::Render:
            return RenderModule.Render();
        }
        return EApplicationTickResult::Exit;
    }

    void Shutdown() noexcept override
    {
        if (bShutdown)
        {
            return;
        }
        bShutdown = true;
        LE_LOG(LogXBD, Info, "Shutting down demo application...");
    }

private:
    FDemoWindowRuntimeModule WindowModule;
    FDemoRenderRuntimeModule RenderModule;
    bool bShutdown = false;
};

void DestroyDemoApplication(void*, IApplication* Application) noexcept
{
    delete static_cast<FDemoApplication*>(Application);
}
} // namespace

FApplicationPtr CreateDemoApplication()
{
    FDemoApplication* const Application = new (std::nothrow) FDemoApplication();
    return FApplicationPtr::Adopt(Application, nullptr, &DestroyDemoApplication);
}

} // namespace LE
