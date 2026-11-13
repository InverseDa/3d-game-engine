#include "DemoApplication/DemoApplication.h"

#include "Application/RuntimeModule.h"
#include "Asset/AssetMinimal.h"
#include "CoreMinimal.h"
#include "DemoSettings.h"

#include "Platform/Platform.h"
#include "RHIMinimal.h"
#include "RFGMinimal.h"
#include "Renderer/RendererMinimal.h"
#include "ShaderRuntimeCompiler.h"
#include "World/WorldMinimal.h"
#include "WorldRenderer/WorldRendererMinimal.h"

#include <cmath>
#include <cstdint>

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

struct FDemoRenderResources
{
    LE::FRHITexture* SceneColorTexture = nullptr;
    LE::FRHITextureView* SceneColorView = nullptr;
    LE::FRHITexture* SceneDepthTexture = nullptr;
    LE::FRHITextureView* SceneDepthView = nullptr;
    LE::FRHISampler* Sampler = nullptr;
    LE::FRHIBindGroupLayout* BindGroupLayout = nullptr;
    LE::FRHIBindGroup* BindGroup = nullptr;
    LE::FRHIPipeline_Graphics* BasePassPipeline = nullptr;
    LE::FRHIPipeline_Graphics* CompositePipeline = nullptr;
};

static void DestroyDemoRenderResources(FDemoRenderResources& Resources)
{
    RHI::DestroyResource(Resources.CompositePipeline);
    RHI::DestroyResource(Resources.BasePassPipeline);
    RHI::DestroyResource(Resources.BindGroup);
    RHI::DestroyResource(Resources.BindGroupLayout);
    RHI::DestroyResource(Resources.Sampler);
    RHI::DestroyResource(Resources.SceneDepthView);
    RHI::DestroyResource(Resources.SceneDepthTexture);
    RHI::DestroyResource(Resources.SceneColorView);
    RHI::DestroyResource(Resources.SceneColorTexture);

    Resources.CompositePipeline = nullptr;
    Resources.BasePassPipeline = nullptr;
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
    LE::FRHIShader* PixelShader)
{
    LE::FRHIPipelineDesc_Graphics PipelineDesc;
    PipelineDesc.Name = "BasePassPipeline";
    PipelineDesc.VertexShader = VertexShader;
    PipelineDesc.PixelShader = PixelShader;

    LE::FRHIVertexInputBinding Binding;
    Binding.Binding = 0;
    Binding.Stride = sizeof(FSimpleVertex);
    Binding.bPerInstance = false;
    PipelineDesc.VertexBindings.PushBack(Binding);

    LE::FRHIVertexInputAttribute PosAttr;
    PosAttr.Location = 0;
    PosAttr.Binding = 0;
    PosAttr.Format = LE::EPixelFormat::R32G32B32_FLOAT;
    PosAttr.Offset = offsetof(FSimpleVertex, Position);
    PipelineDesc.VertexAttributes.PushBack(PosAttr);

    LE::FRHIVertexInputAttribute ColorAttr;
    ColorAttr.Location = 1;
    ColorAttr.Binding = 0;
    ColorAttr.Format = LE::EPixelFormat::R32G32B32_FLOAT;
    ColorAttr.Offset = offsetof(FSimpleVertex, Color);
    PipelineDesc.VertexAttributes.PushBack(ColorAttr);

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
    LE::FRHIShader* BasePassVertexShader,
    LE::FRHIShader* BasePassPixelShader,
    LE::FRHIShader* CompositeVertexShader,
    LE::FRHIShader* CompositePixelShader,
    uint32 Width,
    uint32 Height,
    FDemoRenderResources& OutResources)
{
    if (Device == nullptr || Width == 0 || Height == 0 ||
        OutResources.SceneColorTexture != nullptr ||
        OutResources.SceneDepthTexture != nullptr ||
        OutResources.BasePassPipeline != nullptr ||
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

    OutResources.BasePassPipeline = CreateBasePassPipeline(
        Device, BasePassVertexShader, BasePassPixelShader);
    OutResources.CompositePipeline = CreateCompositePipeline(
        Device,
        CompositeVertexShader,
        CompositePixelShader,
        OutResources.BindGroupLayout,
        LE::EPixelFormat::B8G8R8A8_SRGB);
    if (OutResources.BasePassPipeline == nullptr ||
        OutResources.CompositePipeline == nullptr)
    {
        LE_LOG(LogXBD, Error, "Failed to create BasePass/Composite pipelines.");
        DestroyDemoRenderResources(OutResources);
        return false;
    }

    return true;
}

// Create triangle vertex buffer
LE::FRHIBuffer* CreateTriangleVertexBuffer(LE::FRHIDevice* Device)
{
    FSimpleVertex Vertices[3] = {
        { LE::Math::Vector3f( 0.0f,  0.5f, 0.0f), LE::Math::Vector3f(1.0f, 0.0f, 0.0f) }, // Top - Red
        { LE::Math::Vector3f( 0.5f, -0.5f, 0.0f), LE::Math::Vector3f(0.0f, 1.0f, 0.0f) }, // Right - Green
        { LE::Math::Vector3f(-0.5f, -0.5f, 0.0f), LE::Math::Vector3f(0.0f, 0.0f, 1.0f) }, // Left - Blue
    };

    const uint64 BufferSize = sizeof(Vertices);

    LE::FRHIBufferDesc BufferDesc;
    BufferDesc.Name = "TriangleVertexBuffer";
    BufferDesc.Size = BufferSize;
    BufferDesc.Usage = static_cast<uint32>(LE::EResourceUsage::Upload);
    BufferDesc.UsageFlag = static_cast<uint32>(LE::EBufferUsageFlags::VertexBuffer);

    LE::FRHIBuffer* Buffer = Device == nullptr ? nullptr : Device->CreateBuffer(BufferDesc);
    if (Buffer == nullptr)
    {
        return nullptr;
    }
    void* MappedData = Buffer->Map(0, BufferSize);
    if (MappedData == nullptr)
    {
        RHI::DestroyResource(Buffer);
        return nullptr;
    }
    memcpy(MappedData, Vertices, BufferSize);
    Buffer->Unmap();

    LE_LOG(LogXBD, Info, "Created triangle vertex buffer");
    return Buffer;
}

LE::FRHIBuffer* CreateTriangleIndexBuffer(LE::FRHIDevice* Device)
{
    const std::uint16_t Indices[3] = {0, 1, 2};
    LE::FRHIBufferDesc BufferDesc;
    BufferDesc.Name = "TriangleIndexBuffer";
    BufferDesc.Size = sizeof(Indices);
    BufferDesc.Usage = static_cast<uint32>(LE::EResourceUsage::Upload);
    BufferDesc.UsageFlag = static_cast<uint32>(LE::EBufferUsageFlags::IndexBuffer);
    LE::FRHIBuffer* Buffer = Device == nullptr ? nullptr : Device->CreateBuffer(BufferDesc);
    if (Buffer == nullptr)
    {
        return nullptr;
    }
    void* const MappedData = Buffer->Map(0, sizeof(Indices));
    if (MappedData == nullptr)
    {
        RHI::DestroyResource(Buffer);
        return nullptr;
    }
    memcpy(MappedData, Indices, sizeof(Indices));
    Buffer->Unmap();
    LE_LOG(LogXBD, Info, "Created triangle index buffer");
    return Buffer;
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

        FNativeGameplaySystemDescriptor RotateSystem;
        RotateSystem.Name = "Demo.RotateTriangle";
        RotateSystem.ProviderContext = this;
        RotateSystem.Update = &RotateTriangleSystem;
        RotateSystem.Phase = ENativeGameplaySystemPhase::Variable;
        const ENativeGameplaySystemRegisterResult RegisterResult =
            World.RegisterNativeGameplaySystem(RotateSystem);
        if (RegisterResult != ENativeGameplaySystemRegisterResult::Success)
        {
            LE_LOG(LogXBD, Error, "Failed to register demo native gameplay system: {}.",
                static_cast<uint32>(RegisterResult));
            return false;
        }
        bGameplaySystemRegistered = true;
        const ENativeGameplayScheduleResult ScheduleResult =
            World.CompileNativeGameplaySystemSchedule();
        if (ScheduleResult != ENativeGameplayScheduleResult::Success)
        {
            LE_LOG(LogXBD, Error, "Failed to compile demo native gameplay schedule: {}.",
                static_cast<uint32>(ScheduleResult));
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

        uint32 InitialRenderWidth = DefaultWindowWidth;
        uint32 InitialRenderHeight = DefaultWindowHeight;
        GetBackBufferExtent(Swapchain, DefaultWindowWidth, DefaultWindowHeight, InitialRenderWidth, InitialRenderHeight);
        if (!TryCreateDemoRenderResources(
                Device,
                BasePassVertexShader,
                BasePassPixelShader,
                CompositeVertexShader,
                CompositePixelShader,
                InitialRenderWidth,
                InitialRenderHeight,
                RenderResources))
        {
            return false;
        }

        VertexBuffer = CreateTriangleVertexBuffer(Device);
        IndexBuffer = CreateTriangleIndexBuffer(Device);
        if (VertexBuffer == nullptr || IndexBuffer == nullptr ||
            !FrameScheduler.Initialize(Device))
        {
            LE_LOG(LogXBD, Error, "Failed to create demo frame scheduler resources.");
            return false;
        }

        if (!RegisterRenderAssets())
        {
            LE_LOG(LogXBD, Error, "Failed to register demo render assets.");
            Shutdown();
            return false;
        }

        FWorldCommandBuffer* const Commands = World.GetCommandBuffer();
        TComponentStorage<FWorldMeshComponent>* const MeshComponents =
            World.GetMeshComponents();
        TComponentStorage<FWorldCameraComponent>* const CameraComponents =
            World.GetCameraComponents();
        FWorldMeshComponent MeshComponent;
        MeshComponent.DebugName = "TriangleMesh";
        MeshComponent.Mesh = MeshAssetHandle;
        MeshComponent.Material = MaterialAssetHandle;
        MeshComponent.SortKey = 0;
        FWorldCameraComponent CameraComponent;
        CameraComponent.DebugName = "PrimaryCamera";
        CameraComponent.Priority = 100;
        CameraComponent.bActive = true;
        FTransform CameraTransform;
        CameraTransform.Translation = Math::Vector3f(0.0f, 0.0f, 2.0f);
        if (!World.IsValid() || Commands == nullptr || MeshComponents == nullptr ||
            CameraComponents == nullptr ||
            Commands->TryCreateEntity(BootstrapMeshToken) != EWorldCommandRecordResult::Recorded ||
            Commands->TryAddComponent(*MeshComponents, BootstrapMeshToken, std::move(MeshComponent)) !=
                EWorldCommandRecordResult::Recorded ||
            Commands->TryAddTransform(BootstrapMeshToken) != EWorldCommandRecordResult::Recorded ||
            Commands->TryCreateEntity(BootstrapCameraToken) != EWorldCommandRecordResult::Recorded ||
            Commands->TryAddComponent(
                *CameraComponents, BootstrapCameraToken, std::move(CameraComponent)) !=
                EWorldCommandRecordResult::Recorded ||
            Commands->TryAddTransform(BootstrapCameraToken, CameraTransform) !=
                EWorldCommandRecordResult::Recorded)
        {
            LE_LOG(LogXBD, Error, "Failed to record the demo World bootstrap commands.");
            Shutdown();
            return false;
        }
        bWorldBootstrapPending = true;

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
        bool bWorldIdle = World.GetTickState() == EWorldTickState::Idle;
        if (!bWorldIdle && World.GetTickState() == EWorldTickState::Updating)
        {
            static_cast<void>(World.EndTick());
            bWorldIdle = World.GetTickState() == EWorldTickState::Idle;
        }
        if (bGameplaySystemRegistered && bWorldIdle)
        {
            if (World.UnregisterNativeGameplaySystem("Demo.RotateTriangle") !=
                ENativeGameplaySystemUnregisterResult::Success)
            {
                LE_LOG(LogXBD, Error, "Failed to unregister demo native gameplay system.");
            }
            bGameplaySystemRegistered = false;
        }
        const bool bWorldReset = bWorldIdle && World.Reset();
        if (!bWorldReset)
        {
            LE_LOG(LogXBD, Error, "Demo World was not idle during render runtime shutdown.");
        }
        else
        {
            bWorldBootstrapPending = false;
            BootstrapMeshToken.Reset();
            BootstrapCameraToken.Reset();
            MeshEntity.Reset();
            CameraEntity.Reset();
            if (!UnregisterRenderAssets())
            {
                LE_LOG(LogXBD, Error, "Failed to unregister demo render assets.");
            }
        }
        FrameScheduler.Shutdown();
        if (bRendererInitialized)
        {
            Renderer.Shutdown();
            bRendererInitialized = false;
        }

        RHI::DestroyResource(VertexBuffer);
        VertexBuffer = nullptr;
        RHI::DestroyResource(IndexBuffer);
        IndexBuffer = nullptr;
        DestroyDemoRenderResources(RenderResources);
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
            bCanRender = false;
            FPlatformTime::SleepForMilliseconds(16);
            return EApplicationTickResult::Continue;
        }

        const bool bResizeSwapchain = bSwapchainRecreateRequested ||
            WindowSize.Width != CachedWindowWidth ||
            WindowSize.Height != CachedWindowHeight;
        if (bResizeSwapchain)
        {
            FrameScheduler.WaitForAllFrames();
            LE_LOG(LogXBD, Info, "Explicitly recreating swapchain at {}x{}.",
                WindowSize.Width, WindowSize.Height);
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
            BasePassVertexShader,
            BasePassPixelShader,
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
        if (!ReplaceBasePassMaterial(Candidate.BasePassPipeline))
        {
            DestroyDemoRenderResources(Candidate);
            LE_LOG(LogXBD, Error,
                "Resize candidate publication failed; preserving the previous render resources.");
            bCanRender = true;
            return EApplicationTickResult::Continue;
        }

        FDemoRenderResources Previous = RenderResources;
        RenderResources = Candidate;
        Candidate = {};
        MaterialAssets[ActiveMaterialSlot == 0 ? 1 : 0] = {};
        DestroyDemoRenderResources(Previous);
        bCanRender = true;
        return EApplicationTickResult::Continue;
    }

    EApplicationTickResult Update(const FApplicationTickContext& Context)
    {
        FWorldTickContext WorldContext;
        WorldContext.FrameIndex = Context.FrameIndex;
        WorldContext.DeltaSeconds = Context.DeltaSeconds;
        WorldContext.ElapsedSeconds = Context.ElapsedSeconds;
        FWorldCommandFlushResult BeginFlush;
        const EWorldTickResult BeginResult = World.BeginTick(WorldContext, &BeginFlush);
        if (BeginResult != EWorldTickResult::Success &&
            BeginResult != EWorldTickResult::CompletedWithCommandFailures)
        {
            LE_LOG(LogXBD, Error, "Demo World BeginTick failed: {}.",
                static_cast<uint32>(BeginResult));
            return EApplicationTickResult::Exit;
        }

        bool bBootstrapFailed = false;
        if (bWorldBootstrapPending)
        {
            FWorldCommandBuffer* const Commands = World.GetCommandBuffer();
            const EDeferredEntityResolveResult MeshResolveResult = Commands == nullptr
                ? EDeferredEntityResolveResult::InvalidState
                : Commands->TryResolveDeferredEntity(BootstrapMeshToken, MeshEntity);
            const EDeferredEntityResolveResult CameraResolveResult = Commands == nullptr
                ? EDeferredEntityResolveResult::InvalidState
                : Commands->TryResolveDeferredEntity(BootstrapCameraToken, CameraEntity);
            BootstrapMeshToken.Reset();
            BootstrapCameraToken.Reset();
            bWorldBootstrapPending = false;
            bBootstrapFailed = MeshResolveResult != EDeferredEntityResolveResult::Resolved ||
                CameraResolveResult != EDeferredEntityResolveResult::Resolved;
        }
        const bool bBeginCommandsFailed =
            BeginResult == EWorldTickResult::CompletedWithCommandFailures;
        const ENativeGameplayUpdateResult GameplayResult =
            bBeginCommandsFailed || bBootstrapFailed
            ? ENativeGameplayUpdateResult::InvalidState
            : World.UpdateNativeGameplaySystems();
        FWorldCommandFlushResult EndFlush;
        const EWorldTickResult EndResult = World.EndTick(&EndFlush);
        if (bBeginCommandsFailed || bBootstrapFailed ||
            GameplayResult != ENativeGameplayUpdateResult::Success ||
            EndResult != EWorldTickResult::Success)
        {
            LE_LOG(LogXBD, Error,
                "Demo World update failed: Gameplay={} BeginFirst={} BeginReason={} EndFirst={} EndReason={}.",
                static_cast<uint32>(GameplayResult),
                BeginFlush.FirstFailureIndex,
                static_cast<uint32>(BeginFlush.FirstFailureReason),
                EndFlush.FirstFailureIndex,
                static_cast<uint32>(EndFlush.FirstFailureReason));
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
                    World,
                    AssetRegistry,
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
    bool RegisterRenderAssets() noexcept
    {
        if (!FAssetId::TryParse(
                "a2100000-0000-4000-8000-000000000001", MeshAssetId) ||
            !FAssetId::TryParse(
                "a2100000-0000-4000-8000-000000000002", MaterialAssetId))
        {
            return false;
        }

        MeshAsset.VertexBuffer = VertexBuffer;
        MeshAsset.VertexCount = 3;
        MeshAsset.IndexBuffer = IndexBuffer;
        MeshAsset.IndexCount = 3;
        MeshAsset.IndexFormat = EPixelFormat::R16_UINT;
        MeshAsset.LocalBounds = FRenderBounds(
            Math::Vector3f(-0.5f, -0.5f, 0.0f),
            Math::Vector3f(0.5f, 0.5f, 0.0f));
        TAssetRegistration<FMeshAsset> MeshRegistration;
        MeshRegistration.Id = MeshAssetId;
        MeshRegistration.SoftPath = "Engine/Demo/Triangle.mesh";
        MeshRegistration.DisplayName = "TriangleMesh";
        MeshRegistration.ProviderName = "Demo.Render";
        MeshRegistration.Object = &MeshAsset;
        if (AssetRegistry.Register(MeshRegistration, MeshAssetHandle) !=
            EAssetRegisterResult::Success)
        {
            return false;
        }
        bMeshAssetRegistered = true;

        ActiveMaterialSlot = 0;
        MaterialAssets[ActiveMaterialSlot].GraphicsPipeline =
            RenderResources.BasePassPipeline;
        MaterialAssets[ActiveMaterialSlot].PassMask = ERenderMeshPassMask::SceneColor;
        MaterialAssets[ActiveMaterialSlot].BaseColor =
            Math::Vector4f(1.0f, 0.9f, 0.8f, 1.0f);
        TAssetRegistration<FMaterialAsset> MaterialRegistration;
        MaterialRegistration.Id = MaterialAssetId;
        MaterialRegistration.SoftPath = "Engine/Demo/Triangle.material";
        MaterialRegistration.DisplayName = "TriangleMaterial";
        MaterialRegistration.ProviderName = "Demo.Render";
        MaterialRegistration.Object = &MaterialAssets[ActiveMaterialSlot];
        if (AssetRegistry.Register(MaterialRegistration, MaterialAssetHandle) !=
            EAssetRegisterResult::Success)
        {
            static_cast<void>(UnregisterRenderAssets());
            return false;
        }
        bMaterialAssetRegistered = true;
        return true;
    }

    bool ReplaceBasePassMaterial(FRHIPipeline_Graphics* const NewPipeline) noexcept
    {
        if (NewPipeline == nullptr || !bMaterialAssetRegistered ||
            World.GetTickState() != EWorldTickState::Idle)
        {
            return false;
        }
        TComponentStorage<FWorldMeshComponent>* const MeshComponents =
            World.GetMeshComponents();
        FWorldMeshComponent* const MeshComponent = MeshComponents == nullptr
            ? nullptr
            : World.GetComponent(*MeshComponents, MeshEntity);
        if (MeshComponent == nullptr || MeshComponent->Material != MaterialAssetHandle)
        {
            return false;
        }

        const uint32 NewSlot = ActiveMaterialSlot == 0 ? 1 : 0;
        MaterialAssets[NewSlot].GraphicsPipeline = NewPipeline;
        MaterialAssets[NewSlot].PassMask = ERenderMeshPassMask::SceneColor;
        MaterialAssets[NewSlot].BaseColor = MaterialAssets[ActiveMaterialSlot].BaseColor;
        TAssetRegistration<FMaterialAsset> Registration;
        Registration.Id = MaterialAssetId;
        Registration.SoftPath = "Engine/Demo/Triangle.material";
        Registration.DisplayName = "TriangleMaterial";
        Registration.ProviderName = "Demo.Render";
        Registration.Object = &MaterialAssets[NewSlot];

        FMaterialAssetHandle NewHandle;
        if (AssetRegistry.Replace(MaterialAssetHandle, Registration, NewHandle) !=
            EAssetReplaceResult::Success)
        {
            MaterialAssets[NewSlot] = {};
            return false;
        }

        MeshComponent->Material = NewHandle;
        MaterialAssetHandle = NewHandle;
        ActiveMaterialSlot = NewSlot;
        return true;
    }

    bool UnregisterRenderAssets() noexcept
    {
        bool bSuccess = true;
        if (bMaterialAssetRegistered)
        {
            if (AssetRegistry.Unregister(MaterialAssetHandle) ==
                EAssetUnregisterResult::Success)
            {
                MaterialAssetHandle.Reset();
                bMaterialAssetRegistered = false;
            }
            else
            {
                bSuccess = false;
            }
        }
        if (bMeshAssetRegistered)
        {
            if (AssetRegistry.Unregister(MeshAssetHandle) ==
                EAssetUnregisterResult::Success)
            {
                MeshAssetHandle.Reset();
                bMeshAssetRegistered = false;
            }
            else
            {
                bSuccess = false;
            }
        }
        if (bSuccess)
        {
            MeshAsset = {};
            MaterialAssets[0] = {};
            MaterialAssets[1] = {};
        }
        return bSuccess;
    }

    static ENativeGameplaySystemResult RotateTriangleSystem(
        void* const ProviderContext,
        const FNativeGameplaySystemUpdateContext& Context) noexcept
    {
        FDemoRenderRuntimeModule& Module =
            *static_cast<FDemoRenderRuntimeModule*>(ProviderContext);
        if (Context.World == nullptr || Context.Commands == nullptr ||
            !Context.World->IsAlive(Module.MeshEntity))
        {
            return ENativeGameplaySystemResult::Failure;
        }
        FTransform LocalTransform;
        if (!Context.World->TryGetLocalTransform(Module.MeshEntity, LocalTransform))
        {
            return ENativeGameplaySystemResult::Failure;
        }
        constexpr double TwoPi = 6.28318530717958647692;
        const float Angle = static_cast<float>(
            std::fmod(Context.FrameElapsedSeconds * 0.75, TwoPi));
        LocalTransform.Rotation = Math::Quaternionf(
            Math::Vector3f(0.0f, 0.0f, 1.0f), Angle);
        return Context.Commands->TrySetLocalTransform(Module.MeshEntity, LocalTransform) ==
            EWorldCommandRecordResult::Recorded
            ? ENativeGameplaySystemResult::Success
            : ENativeGameplaySystemResult::Failure;
    }

    static constexpr uint32 DefaultWindowWidth = 1280;
    static constexpr uint32 DefaultWindowHeight = 720;

    FDemoWindowRuntimeModule& WindowModule;
    // Declaration order is a fallback lifetime guarantee: reverse destruction
    // releases World before Registry, and Registry before its borrowed payloads.
    FMeshAsset MeshAsset;
    FMaterialAsset MaterialAssets[2];
    FAssetRegistry AssetRegistry;
    FWorld World;
    FWorldRenderer WorldRenderer;
    FAssetId MeshAssetId;
    FAssetId MaterialAssetId;
    FMeshAssetHandle MeshAssetHandle;
    FMaterialAssetHandle MaterialAssetHandle;
    FDeferredEntityToken BootstrapMeshToken;
    FDeferredEntityToken BootstrapCameraToken;
    FEntity MeshEntity;
    FEntity CameraEntity;
    FRHIDevice* Device = nullptr;
    FRHISwapchain* Swapchain = nullptr;
    FRHIShader* BasePassVertexShader = nullptr;
    FRHIShader* BasePassPixelShader = nullptr;
    FRHIShader* CompositeVertexShader = nullptr;
    FRHIShader* CompositePixelShader = nullptr;
    FRHIBuffer* VertexBuffer = nullptr;
    FRHIBuffer* IndexBuffer = nullptr;
    FRenderFrameScheduler FrameScheduler;
    FDemoRenderResources RenderResources;
    FRenderer Renderer;
    uint32 CachedWindowWidth = DefaultWindowWidth;
    uint32 CachedWindowHeight = DefaultWindowHeight;
    bool bCanRender = false;
    bool bSwapchainRecreateRequested = false;
    bool bRendererInitialized = false;
    bool bWorldBootstrapPending = false;
    bool bGameplaySystemRegistered = false;
    bool bMeshAssetRegistered = false;
    bool bMaterialAssetRegistered = false;
    uint32 ActiveMaterialSlot = 0;
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
