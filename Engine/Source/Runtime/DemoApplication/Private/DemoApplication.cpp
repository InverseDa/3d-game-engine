#include "DemoApplication/DemoApplication.h"

#include "Application/RuntimeModule.h"
#include "AssetLoading/AssetLoading.h"
#include "CoreMinimal.h"
#include "DemoSettings.h"

#include "Platform/Platform.h"
#include "RHIMinimal.h"
#include "RFGMinimal.h"
#include "Renderer/RenderAssetProvider.h"
#include "Renderer/RendererMinimal.h"
#include "ShaderRuntimeCompiler.h"
#include "Scene/Scene.h"
#include "World/WorldMinimal.h"
#include "WorldRenderer/WorldRendererMinimal.h"

#include <cmath>
#include <cstdlib>
#include <locale.h>
#if PLATFORM_MAC
#include <xlocale.h>
#endif

namespace LE
{

LE_DECLARE_LOG_CATEGORY_EXTERN(LogXBD);
LE_DECLARE_LOG_CATEGORY(LogXBD);

struct FDemoRenderResources
{
    LE::FRHITexture* SceneColorTexture = nullptr;
    LE::FRHITextureView* SceneColorView = nullptr;
    LE::FRHITexture* SceneDepthTexture = nullptr;
    LE::FRHITextureView* SceneDepthView = nullptr;
    LE::FRHISampler* Sampler = nullptr;
    LE::FRHIBindGroupLayout* BindGroupLayout = nullptr;
    LE::FRHIBindGroup* BindGroup = nullptr;
    LE::FRHIPipeline_Graphics* CompositePipeline = nullptr;
};

static void DestroyDemoRenderResources(FDemoRenderResources& Resources)
{
    RHI::DestroyResource(Resources.CompositePipeline);
    RHI::DestroyResource(Resources.BindGroup);
    RHI::DestroyResource(Resources.BindGroupLayout);
    RHI::DestroyResource(Resources.Sampler);
    RHI::DestroyResource(Resources.SceneDepthView);
    RHI::DestroyResource(Resources.SceneDepthTexture);
    RHI::DestroyResource(Resources.SceneColorView);
    RHI::DestroyResource(Resources.SceneColorTexture);

    Resources.CompositePipeline = nullptr;
    Resources.BindGroup = nullptr;
    Resources.BindGroupLayout = nullptr;
    Resources.Sampler = nullptr;
    Resources.SceneDepthView = nullptr;
    Resources.SceneDepthTexture = nullptr;
    Resources.SceneColorView = nullptr;
    Resources.SceneColorTexture = nullptr;
}

static LE::FRHIPipeline_Graphics* CreateBasePassPipeline(
    LE::FRHIDevice* Device,
    LE::FRHIShader* VertexShader,
    LE::FRHIShader* PixelShader,
    LE::FRHIBindGroupLayout* MaterialLayout)
{
    LE::FRHIPipelineDesc_Graphics PipelineDesc;
    PipelineDesc.Name = "BasePassPipeline";
    PipelineDesc.VertexShader = VertexShader;
    PipelineDesc.PixelShader = PixelShader;

    LE::FRHIVertexInputBinding Binding;
    Binding.Binding = 0;
    Binding.Stride = sizeof(FBasePassVertex);
    Binding.bPerInstance = false;
    PipelineDesc.VertexBindings.PushBack(Binding);

    LE::FRHIVertexInputAttribute PosAttr;
    PosAttr.Location = 0;
    PosAttr.Binding = 0;
    PosAttr.Format = LE::EPixelFormat::R32G32B32_FLOAT;
    PosAttr.Offset = offsetof(FBasePassVertex, Position);
    PipelineDesc.VertexAttributes.PushBack(PosAttr);

    LE::FRHIVertexInputAttribute ColorAttr;
    ColorAttr.Location = 1;
    ColorAttr.Binding = 0;
    ColorAttr.Format = LE::EPixelFormat::R32G32B32_FLOAT;
    ColorAttr.Offset = offsetof(FBasePassVertex, Color);
    PipelineDesc.VertexAttributes.PushBack(ColorAttr);

    LE::FRHIVertexInputAttribute UVAttr;
    UVAttr.Location = 2;
    UVAttr.Binding = 0;
    UVAttr.Format = LE::EPixelFormat::R32G32_FLOAT;
    UVAttr.Offset = offsetof(FBasePassVertex, UV);
    PipelineDesc.VertexAttributes.PushBack(UVAttr);
    PipelineDesc.BindGroupLayouts.PushBack(MaterialLayout);

    PipelineDesc.RenderTargetCount = 1;
    PipelineDesc.RenderTargetFormats[0] = LE::EPixelFormat::B8G8R8A8_SRGB;
    PipelineDesc.DepthStencilFormat = LE::EPixelFormat::D32_FLOAT;
    PipelineDesc.DepthStencilState.bDepthTestEnable = true;
    PipelineDesc.DepthStencilState.bDepthWriteEnable = true;
    PipelineDesc.DepthStencilState.DepthFunc = LE::ECompareFunction::Less;
    PipelineDesc.RasterizerState.CullMode = LE::ECullMode::None;
    PipelineDesc.RasterizerState.FillMode = LE::EFillMode::Solid;
    PipelineDesc.BlendState.bEnable = false;
    PipelineDesc.PushConstants.StageFlags =
        LE::EShaderStage::Vertex | LE::EShaderStage::Pixel;
    PipelineDesc.PushConstants.Size = sizeof(LE::FBasePassObjectConstants);
    return Device->CreateGraphicsPipeline(PipelineDesc);
}

struct FDemoSceneAssets final
{
    explicit FDemoSceneAssets(const FAssetCatalog& Catalog) noexcept
        : Session(Catalog, {{&Provider, &FRenderAssetProvider::CreateMesh, &FRenderAssetProvider::DestroyMesh},
            {&Provider, &FRenderAssetProvider::CreateMaterial, &FRenderAssetProvider::DestroyMaterial}}) {}

    const FAssetRegistry& GetRegistry() const noexcept { return Session.GetRegistry(); }

    FRenderAssetProvider Provider;
    FAssetLoadSession Session;
};

static LE::FRHIPipeline_Graphics* CreateCompositePipeline(
    LE::FRHIDevice* Device,
    LE::FRHIShader* VertexShader,
    LE::FRHIShader* PixelShader,
    LE::FRHIBindGroupLayout* BindGroupLayout,
    LE::EPixelFormat RenderTargetFormat)
{
    LE::FRHIPipelineDesc_Graphics PipelineDesc;
    PipelineDesc.Name = "CompositePipeline";
    PipelineDesc.VertexShader = VertexShader;
    PipelineDesc.PixelShader = PixelShader;
    PipelineDesc.RenderTargetCount = 1;
    PipelineDesc.RenderTargetFormats[0] = RenderTargetFormat;
    PipelineDesc.DepthStencilFormat = LE::EPixelFormat::Unknown;
    PipelineDesc.DepthStencilState.bDepthTestEnable = false;
    PipelineDesc.DepthStencilState.bDepthWriteEnable = false;
    PipelineDesc.RasterizerState.CullMode = LE::ECullMode::None;
    PipelineDesc.RasterizerState.FillMode = LE::EFillMode::Solid;
    PipelineDesc.BlendState.bEnable = false;
    PipelineDesc.BindGroupLayouts.PushBack(BindGroupLayout);

    return Device->CreateGraphicsPipeline(PipelineDesc);
}

static bool TryCreateDemoRenderResources(
    LE::FRHIDevice* Device,
    LE::FRHIShader* CompositeVertexShader,
    LE::FRHIShader* CompositePixelShader,
    uint32 Width,
    uint32 Height,
    FDemoRenderResources& OutResources)
{
    if (Device == nullptr || Width == 0 || Height == 0 ||
        OutResources.SceneColorTexture != nullptr ||
        OutResources.SceneDepthTexture != nullptr ||
        OutResources.CompositePipeline != nullptr)
    {
        return false;
    }

    LE::FRHITextureDesc TextureDesc;
    TextureDesc.Name = "SceneColor";
    TextureDesc.Width = Width;
    TextureDesc.Height = Height;
    TextureDesc.Depth = 1;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArrayLayers = 1;
    TextureDesc.Format = LE::EPixelFormat::B8G8R8A8_SRGB;
    TextureDesc.bIsRenderTarget = true;
    TextureDesc.bIsShaderResource = true;

    OutResources.SceneColorTexture = Device->CreateTexture(TextureDesc);
    if (OutResources.SceneColorTexture == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create offscreen texture.");
        return false;
    }

    LE::FRHITextureViewDesc TextureViewDesc;
    TextureViewDesc.Texture = OutResources.SceneColorTexture;
    TextureViewDesc.Format = TextureDesc.Format;
    TextureViewDesc.MipSlice = 0;
    TextureViewDesc.ArraySlice = 0;
    TextureViewDesc.MipLevels = 1;
    TextureViewDesc.ArrayLayers = 1;
    OutResources.SceneColorView = Device->CreateTextureView(TextureViewDesc);
    if (OutResources.SceneColorView == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create offscreen texture view.");
        DestroyDemoRenderResources(OutResources);
        return false;
    }

    LE::FRHITextureDesc DepthDesc;
    DepthDesc.Name = "SceneDepth";
    DepthDesc.Width = Width;
    DepthDesc.Height = Height;
    DepthDesc.Depth = 1;
    DepthDesc.MipLevels = 1;
    DepthDesc.ArrayLayers = 1;
    DepthDesc.Format = LE::EPixelFormat::D32_FLOAT;
    DepthDesc.bIsDepthStencil = true;
    OutResources.SceneDepthTexture = Device->CreateTexture(DepthDesc);
    if (OutResources.SceneDepthTexture == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create SceneDepth texture.");
        DestroyDemoRenderResources(OutResources);
        return false;
    }
    LE::FRHITextureViewDesc DepthViewDesc;
    DepthViewDesc.Texture = OutResources.SceneDepthTexture;
    DepthViewDesc.Format = DepthDesc.Format;
    OutResources.SceneDepthView = Device->CreateTextureView(DepthViewDesc);
    if (OutResources.SceneDepthView == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create SceneDepth texture view.");
        DestroyDemoRenderResources(OutResources);
        return false;
    }

    LE::FRHISamplerDesc SamplerDesc;
    SamplerDesc.Name = "RFGSceneColorSampler";
    SamplerDesc.AddressU = LE::ESamplerAddressMode::ClampToEdge;
    SamplerDesc.AddressV = LE::ESamplerAddressMode::ClampToEdge;
    SamplerDesc.AddressW = LE::ESamplerAddressMode::ClampToEdge;
    OutResources.Sampler = Device->CreateSampler(SamplerDesc);
    if (OutResources.Sampler == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create offscreen sampler.");
        DestroyDemoRenderResources(OutResources);
        return false;
    }

    LE::FRHIBindGroupLayoutDesc BindGroupLayoutDesc;
    BindGroupLayoutDesc.Name = "CompositeSceneColorLayout";
    BindGroupLayoutDesc.SetIndex = 0;

    LE::FRHIBindGroupLayoutItem LayoutItem;
    LayoutItem.Binding = 0;
    LayoutItem.Count = 1;
    LayoutItem.Type = LE::ERHIBindGroupItemType::SampledImage;
    LayoutItem.StageFlags = LE::EShaderStage::Pixel;
    BindGroupLayoutDesc.Bindings.PushBack(LayoutItem);

    LayoutItem.Binding = 1;
    LayoutItem.Type = LE::ERHIBindGroupItemType::Sampler;
    BindGroupLayoutDesc.Bindings.PushBack(LayoutItem);

    OutResources.BindGroupLayout = Device->CreateBindGroupLayout(BindGroupLayoutDesc);
    if (OutResources.BindGroupLayout == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create composite bind group layout.");
        DestroyDemoRenderResources(OutResources);
        return false;
    }

    LE::FRHIBindGroupDesc BindGroupDesc;
    BindGroupDesc.Name = "CompositeSceneColorBindGroup";
    BindGroupDesc.Layout = OutResources.BindGroupLayout;

    LE::FRHIBindGroupItem BindGroupItem;
    BindGroupItem.Binding = 0;
    BindGroupItem.TextureView = OutResources.SceneColorView;
    BindGroupDesc.Items.PushBack(BindGroupItem);

    BindGroupItem = {};
    BindGroupItem.Binding = 1;
    BindGroupItem.Sampler = OutResources.Sampler;
    BindGroupDesc.Items.PushBack(BindGroupItem);

    OutResources.BindGroup = Device->CreateBindGroup(BindGroupDesc);
    if (OutResources.BindGroup == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create composite bind group.");
        DestroyDemoRenderResources(OutResources);
        return false;
    }

    OutResources.CompositePipeline = CreateCompositePipeline(
        Device,
        CompositeVertexShader,
        CompositePixelShader,
        OutResources.BindGroupLayout,
        LE::EPixelFormat::B8G8R8A8_SRGB);
    if (OutResources.CompositePipeline == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create Composite pipeline.");
        DestroyDemoRenderResources(OutResources);
        return false;
    }

    return true;
}

static void GetBackBufferExtent(
    LE::FRHISwapchain* Swapchain,
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

    LE::FRHITextureView* BackBufferView = Swapchain->GetCurrentBackBufferView();
    if (BackBufferView == nullptr || BackBufferView->GetTexture() == nullptr)
    {
        return;
    }

    const LE::FRHITextureDesc& BackBufferDesc = BackBufferView->GetTexture()->GetDesc();
    OutWidth = BackBufferDesc.Width;
    OutHeight = BackBufferDesc.Height;
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
        LE_LOG(LogXBD, Info, "=== Limitless Engine Phase 6: BasePass Vertical Slice ===")

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

        Device = LE::RHI::CreateDevice();
        if (Device == nullptr)
        {
            LE_LOG(LogXBD, Error, "Failed to create RHI Device");
            return false;
        }

        LE::FRHISwapchainDesc SwapchainDesc;
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

        LE::Array<char> BasePassVertBytecode;
        LE::Array<char> BasePassFragBytecode;
        LE::Array<char> CompositeVertBytecode;
        LE::Array<char> CompositeFragBytecode;
        if (!Demo::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/BasePassVS.lsf", ERuntimeShaderStage::Vertex, BasePassVertBytecode, "MainVS") ||
            !Demo::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/BasePassPS.lsf", ERuntimeShaderStage::Fragment, BasePassFragBytecode, "MainPS") ||
            !Demo::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/CompositeVS.lsf", ERuntimeShaderStage::Vertex, CompositeVertBytecode, "MainVS") ||
            !Demo::ShaderRuntimeCompiler::CompileHlslToSpirv("Engine/Content/Shaders/CompositePS.lsf", ERuntimeShaderStage::Fragment, CompositeFragBytecode, "MainPS"))
        {
            return false;
        }

        BasePassVertexShader = Device->CreateShaderFromFile(
            LE::EShaderStage::Vertex, BasePassVertBytecode.Data(), BasePassVertBytecode.Size(), "MainVS");
        BasePassPixelShader = Device->CreateShaderFromFile(
            LE::EShaderStage::Pixel, BasePassFragBytecode.Data(), BasePassFragBytecode.Size(), "MainPS");
        CompositeVertexShader = Device->CreateShaderFromFile(
            LE::EShaderStage::Vertex, CompositeVertBytecode.Data(), CompositeVertBytecode.Size(), "MainVS");
        CompositePixelShader = Device->CreateShaderFromFile(
            LE::EShaderStage::Pixel, CompositeFragBytecode.Data(), CompositeFragBytecode.Size(), "MainPS");
        if (BasePassVertexShader == nullptr || BasePassPixelShader == nullptr ||
            CompositeVertexShader == nullptr || CompositePixelShader == nullptr)
        {
            LE_LOG(LogXBD, Error, "Failed to create shader modules.");
            return false;
        }

        MaterialLayout = CreateBasePassMaterialLayout(*Device);
        if (MaterialLayout == nullptr)
        {
            return false;
        }
        BasePassPipeline = CreateBasePassPipeline(Device, BasePassVertexShader, BasePassPixelShader, MaterialLayout);
        if (BasePassPipeline == nullptr)
        {
            LE_LOG(LogXBD, Error, "Failed to create BasePass pipeline.");
            return false;
        }

        uint32 InitialRenderWidth = DefaultWindowWidth;
        uint32 InitialRenderHeight = DefaultWindowHeight;
        GetBackBufferExtent(Swapchain, DefaultWindowWidth, DefaultWindowHeight, InitialRenderWidth, InitialRenderHeight);
        if (!TryCreateDemoRenderResources(
                Device,
                CompositeVertexShader,
                CompositePixelShader,
                InitialRenderWidth,
                InitialRenderHeight,
                RenderResources))
        {
            return false;
        }

        if (!FrameScheduler.Initialize(Device))
        {
            LE_LOG(LogXBD, Error, "Failed to initialize demo frame scheduler.");
            return false;
        }
        if (!InitializeContent())
        {
            return false;
        }

        Renderer.Initialize();
        bRendererInitialized = true;
        CachedWindowWidth = InitialRenderWidth;
        CachedWindowHeight = InitialRenderHeight;
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
        Scene.Reset();
        Assets.Reset();
        if (bRendererInitialized)
        {
            Renderer.Shutdown();
            bRendererInitialized = false;
        }

        DestroyDemoRenderResources(RenderResources);
        RHI::DestroyResource(BasePassPipeline);
        BasePassPipeline = nullptr;
        RHI::DestroyResource(MaterialLayout);
        MaterialLayout = nullptr;
        RHI::DestroyResource(CompositePixelShader);
        CompositePixelShader = nullptr;
        RHI::DestroyResource(CompositeVertexShader);
        CompositeVertexShader = nullptr;
        RHI::DestroyResource(BasePassPixelShader);
        BasePassPixelShader = nullptr;
        RHI::DestroyResource(BasePassVertexShader);
        BasePassVertexShader = nullptr;
        RHI::DestroyResource(Swapchain);
        Swapchain = nullptr;
        RHI::DestroyResource(Device);
        Device = nullptr;
    }

    EApplicationTickResult UpdateRenderResources()
    {
        FPlatformWindow* const Window = WindowModule.GetWindow();
        const FPlatformWindowSize WindowSize = Window->GetSize();
        if (Window->IsMinimized() || WindowSize.Width == 0 || WindowSize.Height == 0)
        {
            if (!bRenderingSuspended)
            {
                LE_LOG(LogXBD, Info, "Window minimized; rendering suspended.");
                bRenderingSuspended = true;
            }
            bCanRender = false;
            FPlatformTime::SleepForMilliseconds(16);
            return EApplicationTickResult::Continue;
        }
        if (bRenderingSuspended)
        {
            LE_LOG(LogXBD, Info, "Window restored; resuming rendering.");
            bRenderingSuspended = false;
        }

        const bool bResizeSwapchain = bSwapchainRecreateRequested ||
            WindowSize.Width != CachedWindowWidth ||
            WindowSize.Height != CachedWindowHeight;
        if (bResizeSwapchain)
        {
            FrameScheduler.WaitForAllFrames();
            LE_LOG(LogXBD, Info, "Explicitly recreating swapchain at {}x{}.",
                WindowSize.Width, WindowSize.Height);
            LE_LOG(LogXBD, Info, "Scene texture uploads at resize: {}.",
                Assets->Provider.GetTextureUploadCount());
            if (!Swapchain->Resize(WindowSize.Width, WindowSize.Height))
            {
                bCanRender = false;
                return EApplicationTickResult::Continue;
            }
            CachedWindowWidth = WindowSize.Width;
            CachedWindowHeight = WindowSize.Height;
            bSwapchainRecreateRequested = false;
        }

        uint32 TargetRenderWidth = CachedWindowWidth;
        uint32 TargetRenderHeight = CachedWindowHeight;
        GetBackBufferExtent(Swapchain, CachedWindowWidth, CachedWindowHeight, TargetRenderWidth, TargetRenderHeight);
        if (RenderResources.SceneColorTexture != nullptr &&
            RenderResources.SceneColorTexture->GetDesc().Width == TargetRenderWidth &&
            RenderResources.SceneColorTexture->GetDesc().Height == TargetRenderHeight)
        {
            bCanRender = true;
            return EApplicationTickResult::Continue;
        }

        if (!bResizeSwapchain)
        {
            FrameScheduler.WaitForAllFrames();
        }
        FDemoRenderResources Candidate;
        if (!TryCreateDemoRenderResources(
            Device,
            CompositeVertexShader,
            CompositePixelShader,
            TargetRenderWidth,
            TargetRenderHeight,
            Candidate))
        {
            LE_LOG(LogXBD, Error,
                "Resize candidate creation failed; preserving the previous render resources.");
            bCanRender = true;
            return EApplicationTickResult::Continue;
        }
        FDemoRenderResources Previous = RenderResources;
        RenderResources = Candidate;
        DestroyDemoRenderResources(Previous);
        bCanRender = true;
        return EApplicationTickResult::Continue;
    }

    EApplicationTickResult Update(const FApplicationTickContext& Context)
    {
        FWorld& World = Scene->GetWorld();
        const FWorldTickContext WorldContext{Context.FrameIndex, Context.DeltaSeconds, Context.ElapsedSeconds};
        const EWorldTickResult Begin = World.BeginTick(WorldContext);
        if (Begin != EWorldTickResult::Success && Begin != EWorldTickResult::CompletedWithCommandFailures)
        {
            LE_LOG(LogXBD, Error, "Demo World could not begin tick: {}.", static_cast<uint32>(Begin));
            return EApplicationTickResult::Exit;
        }
        const auto UpdateResult = Begin == EWorldTickResult::Success
            ? World.UpdateNativeGameplaySystems() : ENativeGameplayUpdateResult::InvalidState;
        const auto End = World.EndTick();
        if (UpdateResult != ENativeGameplayUpdateResult::Success || End != EWorldTickResult::Success)
        {
            LE_LOG(LogXBD, Error, "Demo World update failed: {} / {}.",
                static_cast<uint32>(UpdateResult), static_cast<uint32>(End));
            return EApplicationTickResult::Exit;
        }
        return UpdateRenderResources();
    }

    EApplicationTickResult Render()
    {
        if (!bCanRender)
        {
            return EApplicationTickResult::Continue;
        }

        auto RecordFrame = [this](LE::FRenderFrameScope& ScheduledFrame)
        {
            LE::FWorldRenderSnapshot RenderSnapshot;
            LE::FWorldRenderSnapshotBuildReport SnapshotReport;
            LE::FWorldRenderSnapshotBuildContext SnapshotContext;
            SnapshotContext.FrameIndex = ScheduledFrame.GetFrameIndex();
            SnapshotContext.ViewportWidth =
                RenderResources.SceneColorTexture->GetDesc().Width;
            SnapshotContext.ViewportHeight =
                RenderResources.SceneColorTexture->GetDesc().Height;
            if (WorldRenderer.BuildSnapshot(
                    Scene->GetWorld(),
                    Assets->GetRegistry(),
                    SnapshotContext,
                    RenderSnapshot,
                    &SnapshotReport) !=
                LE::EWorldRenderSnapshotBuildResult::Success)
            {
                LE_LOG(LogXBD, Error,
                    "Demo World snapshot build failed: Result={} MeshIndex={} AssetResolve={}.",
                    static_cast<uint32>(SnapshotReport.Result),
                    SnapshotReport.MeshIndex,
                    static_cast<uint32>(SnapshotReport.AssetResolveResult));
                return LE::ERenderFrameRecordResult::Error;
            }

            LE::FRendererFrameContext FrameContext;
            FrameContext.FrameIndex = ScheduledFrame.GetFrameIndex();
            FrameContext.Device = Device;
            FrameContext.Swapchain = Swapchain;
            FrameContext.CommandList = ScheduledFrame.GetCommandList();
            FrameContext.FrameScope = &ScheduledFrame;
            FrameContext.ViewFamily.PrimarySwapchain = Swapchain;
            LE::FBasePassCompositePipelineDesc RenderPipelineDesc;
            RenderPipelineDesc.BasePassResources.SceneColorTexture =
                RenderResources.SceneColorTexture;
            RenderPipelineDesc.BasePassResources.SceneColorView =
                RenderResources.SceneColorView;
            RenderPipelineDesc.BasePassResources.SceneDepthTexture =
                RenderResources.SceneDepthTexture;
            RenderPipelineDesc.BasePassResources.SceneDepthView =
                RenderResources.SceneDepthView;
            RenderPipelineDesc.BackBufferView = ScheduledFrame.GetBackBufferView();
            RenderPipelineDesc.CompositePipeline = RenderResources.CompositePipeline;
            RenderPipelineDesc.CompositeBindGroup = RenderResources.BindGroup;
            LE::FBasePassCompositePipeline RenderPipeline(RenderPipelineDesc);
            FrameContext.Pipeline = &RenderPipeline;
            return Renderer.RenderFrame(
                FrameContext, &RenderSnapshot.GetRenderScene());
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
        if (!bSceneSwitchAttempted && FrameResult.PresentStatus == ERHISwapchainStatus::Success)
        {
            bSceneSwitchAttempted = true;
            const char* const NextScene = std::getenv("LIMITLESS_DEMO_NEXT_SCENE");
            if (NextScene != nullptr && !PublishScene(NextScene))
            {
                LE_LOG(LogXBD, Info, "Keeping the active scene after replacement failed.");
                bReportPreservedScene = true;
            }
        }
        else if (bReportPreservedScene && FrameResult.PresentStatus == ERHISwapchainStatus::Success)
        {
            bReportPreservedScene = false;
            LE_LOG(LogXBD, Info, "Presented the preserved scene with {} texture uploads.",
                Assets->Provider.GetTextureUploadCount());
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
    static bool ResolveMesh(void* Context, FAssetId Id, FMeshAssetHandle& Out) noexcept
    {
        FAssetLoadDiagnostic Diagnostic;
        const auto Result = static_cast<FDemoSceneAssets*>(Context)->Session.Load(Id, Out, &Diagnostic);
        if (Result != EAssetLoadResult::Success)
        {
            LogAssetFailure(Diagnostic);
        }
        return Result == EAssetLoadResult::Success;
    }

    static bool ResolveMaterial(void* Context, FAssetId Id, FMaterialAssetHandle& Out) noexcept
    {
        FAssetLoadDiagnostic Diagnostic;
        auto& Assets = *static_cast<FDemoSceneAssets*>(Context);
        const auto Result = Assets.Session.Load(Id, Out, &Diagnostic);
        if (Result != EAssetLoadResult::Success)
        {
            LogAssetFailure(Diagnostic);
            if (Result == EAssetLoadResult::ProviderFailed)
            {
                LE_LOG(LogXBD, Error, "Material GPU construction: {}.",
                    DescribeTextureUploadResult(Assets.Provider.GetLastTextureUploadResult()));
            }
        }
        return Result == EAssetLoadResult::Success;
    }

    static void LogAssetFailure(const FAssetLoadDiagnostic& Diagnostic) noexcept
    {
        LE_LOG(LogXBD, Error, "Content asset '{}' ({}), dependency {}: load={}, file={}, format={}.",
            Diagnostic.Path.Data(), Diagnostic.Id.ToString().Data(),
            Diagnostic.DependencyId.ToString().Data(),
            static_cast<uint32>(Diagnostic.Result), DescribeContentIOResult(Diagnostic.FileResult),
            static_cast<uint32>(Diagnostic.FormatResult));
    }

    bool InitializeContent() noexcept
    {
        const String ExecutableDirectory = Demo::ShaderRuntimeCompiler::GetExecutableDirectory();
        if (ExecutableDirectory.IsEmpty())
        {
            LE_LOG(LogXBD, Error, "Could not locate the executable's content directory.");
            return false;
        }
        const char* RootOverride = std::getenv("LIMITLESS_DEMO_CONTENT");
        const String ContentRoot = RootOverride != nullptr
            ? String(RootOverride) : Demo::ShaderRuntimeCompiler::JoinPath(ExecutableDirectory.View(), "DemoContent");
        const String AssetRoot = Demo::ShaderRuntimeCompiler::JoinPath(ContentRoot.View(), "Assets");
        const FAssetMount Mount{"Game", AssetRoot.View()};
        FAssetLoadDiagnostic Diagnostic;
        if (Catalog.Rebuild({&Mount, 1}, &Diagnostic) != EAssetLoadResult::Success)
        {
            LogAssetFailure(Diagnostic);
            return false;
        }
        const char* InputOverride = std::getenv("LIMITLESS_DEMO_SCENE");
        const String ScenePath = InputOverride != nullptr
            ? String(InputOverride) : Demo::ShaderRuntimeCompiler::JoinPath(ContentRoot.View(), "Scene.json");
        return PublishScene(ScenePath.View()) && ApplyRequestedSceneEdit();
    }

    bool ApplyRequestedSceneEdit() noexcept
    {
        const char* EditId = std::getenv("LIMITLESS_DEMO_EDIT_ENTITY");
        const char* Translation = std::getenv("LIMITLESS_DEMO_EDIT_X");
        const char* SavePath = std::getenv("LIMITLESS_DEMO_SAVE_SCENE");
        if (EditId == nullptr && Translation == nullptr && SavePath == nullptr)
        {
            return true;
        }

        FWorld& World = Scene->GetWorld();
        if (EditId != nullptr || Translation != nullptr)
        {
            FSceneEntityId Id;
            FEntity Entity;
            FTransform Transform;
            float X = 0.0f;
            const StringView Text(Translation);
            if (EditId == nullptr || Text.IsEmpty() || !FSceneEntityId::TryParse(EditId, Id) ||
                !Scene->TryFindEntity(Id, Entity) || !World.TryGetLocalTransform(Entity, Transform))
            {
                LE_LOG(LogXBD, Error, "Scene edit requires a known entity UUID and local X translation.");
                return false;
            }
            char* End = nullptr;
#if PLATFORM_WINDOWS
            const auto Locale = _create_locale(LC_NUMERIC, "C");
            if (Locale != nullptr)
            {
                X = _strtof_l(Translation, &End, Locale);
                _free_locale(Locale);
            }
#else
            const auto Locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
            if (Locale != nullptr)
            {
                X = strtof_l(Translation, &End, Locale);
                freelocale(Locale);
            }
#endif
            if (End != Text.Data() + Text.Size() || !std::isfinite(X))
            {
                LE_LOG(LogXBD, Error, "Scene edit X translation must be a finite decimal number.");
                return false;
            }
            Transform.Translation[0] = X;
            const auto Result = World.SetLocalTransform(Entity, Transform);
            if (Result != ETransformUpdateResult::Updated && Result != ETransformUpdateResult::Unchanged)
            {
                LE_LOG(LogXBD, Error, "Could not edit scene entity '{}': {}.", EditId, static_cast<uint32>(Result));
                return false;
            }
            LE_LOG(LogXBD, Info, "Edited scene entity '{}' local X to {}.", EditId, X);
        }
        if (SavePath != nullptr)
        {
            FSceneOperationDiagnostic Diagnostic;
            const auto Result = SaveScene(SavePath, *Scene, Assets->GetRegistry(), &Diagnostic);
            if (Result != ESceneOperationResult::Success)
            {
                LE_LOG(LogXBD, Error, "Scene save '{}': {} ({}).", SavePath,
                    DescribeSceneOperationResult(Result), DescribeContentIOResult(Diagnostic.FileResult));
                return false;
            }
            LE_LOG(LogXBD, Info, "Saved scene '{}'.", SavePath);
        }
        return true;
    }

    bool PublishScene(StringView Path) noexcept
    {
        auto CandidateAssets = TryMakeUnique<FDemoSceneAssets>(Catalog);
        UniquePtr<FScene> Candidate;
        FSceneOperationDiagnostic Diagnostic;
        const String PrintablePath(Path);
        if (!CandidateAssets)
        {
            LE_LOG(LogXBD, Error, "Could not allocate a scene asset session for '{}'.", PrintablePath.Data());
            return false;
        }
        if (!CandidateAssets->Provider.Initialize(Device, BasePassPipeline, MaterialLayout))
        {
            LE_LOG(LogXBD, Error, "Could not initialize material GPU resources for '{}'.", PrintablePath.Data());
            return false;
        }
        const FSceneAssetResolver Resolver{CandidateAssets.Get(), &CandidateAssets->GetRegistry(),
            &ResolveMesh, &ResolveMaterial};
        const auto Result = LoadScene(Path, Resolver, Candidate, &Diagnostic);
        if (Result != ESceneOperationResult::Success)
        {
            LE_LOG(LogXBD, Error, "Scene '{}': {} ({}:{} {}, entity {}, asset {}, file {}).",
                PrintablePath.Data(), DescribeSceneOperationResult(Result), Diagnostic.Document.Line,
                Diagnostic.Document.Column, DescribeSceneDocumentResult(Diagnostic.Document.Result),
                Diagnostic.Entity.ToString().Data(), Diagnostic.Asset.ToString().Data(),
                DescribeContentIOResult(Diagnostic.FileResult));
            return false;
        }
        FWorld& CandidateWorld = Candidate->GetWorld();
        if (CandidateWorld.CompileNativeGameplaySystemSchedule() != ENativeGameplayScheduleResult::Success)
        {
            LE_LOG(LogXBD, Error, "Could not compile gameplay schedule for '{}'.", PrintablePath.Data());
            return false;
        }

        FWorldRenderSnapshot Preview;
        FWorldRenderSnapshotBuildReport PreviewReport;
        FWorldRenderSnapshotBuildContext PreviewContext;
        PreviewContext.ViewportWidth = RenderResources.SceneColorTexture->GetDesc().Width;
        PreviewContext.ViewportHeight = RenderResources.SceneColorTexture->GetDesc().Height;
        if (WorldRenderer.BuildSnapshot(CandidateWorld, CandidateAssets->GetRegistry(),
                PreviewContext, Preview, &PreviewReport) != EWorldRenderSnapshotBuildResult::Success)
        {
            if (PreviewReport.Result == EWorldRenderSnapshotBuildResult::MissingMeshUV)
            {
                const auto* Meshes = CandidateWorld.GetMeshComponents();
                if (Meshes != nullptr && PreviewReport.MeshIndex < Meshes->GetEntities().Size())
                {
                    FSceneEntityId EntityId;
                    FAssetId MeshId;
                    FAssetId MaterialId;
                    const auto& Mesh = Meshes->GetComponents()[PreviewReport.MeshIndex];
                    Candidate->TryGetEntityId(Meshes->GetEntities()[PreviewReport.MeshIndex], EntityId);
                    CandidateAssets->GetRegistry().TryGetAssetId(Mesh.Mesh, MeshId);
                    CandidateAssets->GetRegistry().TryGetAssetId(Mesh.Material, MaterialId);
                    LE_LOG(LogXBD, Error, "Scene '{}': entity {} mesh {} has no UVs for material {}.",
                        PrintablePath.Data(), EntityId.ToString().Data(), MeshId.ToString().Data(),
                        MaterialId.ToString().Data());
                    return false;
                }
            }
            LE_LOG(LogXBD, Error, "Scene '{}' is not renderable: {} (mesh {}).",
                PrintablePath.Data(), static_cast<uint32>(PreviewReport.Result), PreviewReport.MeshIndex);
            return false;
        }

        if (Scene)
        {
            if (Scene->GetWorld().GetTickState() != EWorldTickState::Idle)
            {
                return false;
            }
            FrameScheduler.WaitForAllFrames();
        }
        Scene = std::move(Candidate);
        Assets = std::move(CandidateAssets);
        LE_LOG(LogXBD, Info, "Loaded scene '{}' with {} entities and {} visible meshes.",
            PrintablePath.Data(), Scene->GetWorld().GetEntityCount(), Preview.GetRenderScene().Meshes.Size());
        LE_LOG(LogXBD, Info, "Scene texture uploads at publication: {}.", Assets->Provider.GetTextureUploadCount());
        return true;
    }

    static constexpr uint32 DefaultWindowWidth = 1280;
    static constexpr uint32 DefaultWindowHeight = 720;

    FDemoWindowRuntimeModule& WindowModule;
    FAssetCatalog Catalog;
    UniquePtr<FDemoSceneAssets> Assets;
    UniquePtr<FScene> Scene;
    FWorldRenderer WorldRenderer;
    FRHIDevice* Device = nullptr;
    FRHISwapchain* Swapchain = nullptr;
    FRHIShader* BasePassVertexShader = nullptr;
    FRHIShader* BasePassPixelShader = nullptr;
    FRHIShader* CompositeVertexShader = nullptr;
    FRHIShader* CompositePixelShader = nullptr;
    FRHIPipeline_Graphics* BasePassPipeline = nullptr;
    FRHIBindGroupLayout* MaterialLayout = nullptr;
    FRenderFrameScheduler FrameScheduler;
    FDemoRenderResources RenderResources;
    FRenderer Renderer;
    uint32 CachedWindowWidth = DefaultWindowWidth;
    uint32 CachedWindowHeight = DefaultWindowHeight;
    bool bCanRender = false;
    bool bSceneSwitchAttempted = false;
    bool bReportPreservedScene = false;
    bool bRenderingSuspended = false;
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
        LE_LOG(LogXBD, Info, "Entering BasePass composite main loop...");
        return true;
    }

    EApplicationTickResult Tick(const FApplicationTickContext& Context) override
    {
        switch (Context.Phase)
        {
        case EApplicationFramePhase::ProcessPlatformEvents:
            return WindowModule.ProcessPlatformEvents();
        case EApplicationFramePhase::Update:
            return RenderModule.Update(Context);
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
