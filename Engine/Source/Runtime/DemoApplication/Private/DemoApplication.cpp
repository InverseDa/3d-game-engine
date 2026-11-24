#include "DemoApplication/DemoApplication.h"

#include "Application/RuntimeModule.h"
#include "AssetLoading/AssetLoading.h"
#include "Camera/DemoCameraNavigation.h"
#include "CoreMinimal.h"
#include "DemoSettings.h"
#include "DemoShaders.h"
#if WITH_EDITOR
#include "DemoContentGeneration.h"
#endif
#if WITH_EDITOR && PLATFORM_WINDOWS
#include "EditorShaderPreview.h"
#endif

#include "Platform/Platform.h"
#include "RHIMinimal.h"
#include "Renderer/RenderAssetProvider.h"
#include "Renderer/SceneRenderer.h"
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

struct FDemoSceneAssets final
{
    explicit FDemoSceneAssets(const SharedPtr<FAssetCatalog>& InCatalog) noexcept : Catalog(InCatalog), Session(*Catalog, {{&Provider, &FRenderAssetProvider::CreateMesh, &FRenderAssetProvider::DestroyMesh}, {&Provider, &FRenderAssetProvider::CreateMaterial, &FRenderAssetProvider::DestroyMaterial}}) {}

    const FAssetRegistry& GetRegistry() const noexcept { return Session.GetRegistry(); }

    // The session borrows its immutable catalog. It must destruct before both
    // the provider and the catalog, including when a candidate is rejected.
    SharedPtr<FAssetCatalog> Catalog;
    FRenderAssetProvider Provider;
    FAssetLoadSession Session;
};

static constexpr EPixelFormat DemoBackBufferFormat = EPixelFormat::B8G8R8A8_SRGB;

static FSceneRenderTargetDesc GetBackBufferTarget(const FRHISwapchain& Swapchain, const FPlatformWindowSize FallbackSize)
{
    const FRHITextureView* const View = Swapchain.GetCurrentBackBufferView();
    if (View == nullptr || View->GetTexture() == nullptr)
    {
        return {FallbackSize.Width, FallbackSize.Height, DemoBackBufferFormat};
    }
    const FRHITextureDesc& Desc = View->GetTexture()->GetDesc();
    const EPixelFormat ViewFormat = View->GetDesc().Format;
    return {Desc.Width, Desc.Height, ViewFormat == EPixelFormat::Unknown ? Desc.Format : ViewFormat};
}

namespace
{
class FDemoWindowRuntimeModule final
{
public:
    ~FDemoWindowRuntimeModule() { Shutdown(); }

    bool Startup() noexcept
    {
        LE_LOG(LogXBD, Info, "=== Limitless Engine Phase 6: BasePass Vertical Slice ===")

        if (RegisterDemoApplicationReflection(ReflectionRegistry, ReflectionHandle) != EReflectionRegisterResult::Success)
        {
            LE_LOG(LogXBD, Error, "Failed to register demo settings reflection");
            return false;
        }
        bReflectionRegistered = true;

        const FTypeInfo* const SettingsType = ReflectionRegistry.FindType("LE.FDemoSettings");
        FDemoSettings Defaults;
        FPropertyBag Captured;
        Array<uint8> Encoded;
        if (SettingsType == nullptr || TryCapturePropertyBag(ReflectionRegistry, SettingsType->Id, SettingsSchemaVersion, &Defaults, Captured) != EPropertySerializationResult::Success || TryEncodePropertyBag(Captured, Encoded) != EPropertySerializationResult::Success || TryDecodePropertyBag(Span<const uint8>(Encoded.Data(), Encoded.Size()), DecodedSettings) != EPropertySerializationResult::Success || ValidatePropertyBag(ReflectionRegistry, DecodedSettings) != EPropertySerializationResult::Success)
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
        const FPropertyValue* const WidthValue = WidthProperty == nullptr ? nullptr : DecodedSettings.Find(WidthProperty->Id);
        const FPropertyValue* const HeightValue = HeightProperty == nullptr ? nullptr : DecodedSettings.Find(HeightProperty->Id);
        const FPropertyValue* const TitleValue = TitleProperty == nullptr ? nullptr : DecodedSettings.Find(TitleProperty->Id);
        if (WidthValue == nullptr || HeightValue == nullptr || TitleValue == nullptr || !WidthValue->TryGetUInt32(Width) || !HeightValue->TryGetUInt32(Height) || !TitleValue->TryGetString(Title) || Width == 0 || Height == 0)
        {
            LE_LOG(LogXBD, Error, "Decoded demo settings are incomplete or invalid");
            CleanupReflection();
            return false;
        }

        FPlatformWindowDesc WindowDesc;
        WindowDesc.Width = Width;
        WindowDesc.Height = Height;
        WindowDesc.Title = Title;
        WindowDesc.bEnableInput = true;
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
        Input.Reset();
    }

    EApplicationTickResult ProcessPlatformEvents()
    {
        Input.BeginFrame();
        Window->PumpEvents(PlatformEvents);
        FPlatformEvent PlatformEvent;
        while (PlatformEvents.Poll(PlatformEvent))
        {
            Input.Apply(Window->GetId(), PlatformEvent);
            if (PlatformEvent.WindowId == Window->GetId() && PlatformEvent.Type == EPlatformEventType::CloseRequested)
            {
                return EApplicationTickResult::Exit;
            }
        }
        return Window->IsCloseRequested() ? EApplicationTickResult::Exit : EApplicationTickResult::Continue;
    }

    FPlatformWindow* GetWindow() const noexcept { return Window.Get(); }
    const FPlatformInputFrame& GetInputFrame() const noexcept { return Input.GetFrame(); }
    void ResetInputForScene() noexcept { Input.ResetKeysAndMotion(); }

    static bool StartupThunk(void* Context) noexcept { return static_cast<FDemoWindowRuntimeModule*>(Context)->Startup(); }

    static void ShutdownThunk(void* Context) noexcept { static_cast<FDemoWindowRuntimeModule*>(Context)->Shutdown(); }

private:
    void CleanupReflection() noexcept
    {
        if (bReflectionRegistered)
        {
            static_cast<void>(UnregisterDemoApplicationReflection(ReflectionRegistry, ReflectionHandle));
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
    FPlatformWindowInput Input;
    bool bReflectionRegistered = false;
    bool bShutdown = false;
};

class FDemoRenderRuntimeModule final
{
public:
    explicit FDemoRenderRuntimeModule(FDemoWindowRuntimeModule& InWindowModule) noexcept : WindowModule(InWindowModule) {}

    ~FDemoRenderRuntimeModule() { Shutdown(); }

    bool Startup() noexcept
    {
        FPlatformWindow* const Window = WindowModule.GetWindow();
        if (Window == nullptr)
        {
            LE_LOG(LogXBD, Error, "Render module requires an active window module");
            return false;
        }

        const String ExecutableDirectory = GetDemoExecutableDirectory();
        if (ExecutableDirectory.IsEmpty())
        {
            LE_LOG(LogXBD, Error, "Could not locate the executable's packaged data directory.");
            return false;
        }
        if (!ShaderBundle.Load(ExecutableDirectory.View()))
        {
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
        SwapchainDesc.BackBufferFormat = DemoBackBufferFormat;
        SwapchainDesc.bEnableVsync = true;
        Swapchain = Device->CreateSwapchain(SwapchainDesc);
        if (Swapchain == nullptr)
        {
            LE_LOG(LogXBD, Error, "Failed to create swapchain");
            return false;
        }

        const FSceneRenderTargetDesc InitialTarget = GetBackBufferTarget(*Swapchain, {DefaultWindowWidth, DefaultWindowHeight});
        if (!Renderer.Initialize(*Device, ShaderBundle.GetShaders(), InitialTarget))
        {
            LE_LOG(LogXBD, Error, "Failed to initialize scene renderer.");
            return false;
        }

        if (!FrameScheduler.Initialize(Device))
        {
            LE_LOG(LogXBD, Error, "Failed to initialize demo frame scheduler.");
            return false;
        }
        if (!InitializeContent(ExecutableDirectory.View()))
        {
            return false;
        }

        CachedWindowSize = {InitialTarget.Width, InitialTarget.Height};
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
#if WITH_EDITOR && PLATFORM_WINDOWS
        ShaderPreview.Shutdown();
#endif
        FrameScheduler.Shutdown();
        Scene.Reset();
        Assets.Reset();
        Renderer.Shutdown();
        ShaderBundle = FDemoShaderBundle{};
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

        const bool bResizeSwapchain = bSwapchainRecreateRequested || WindowSize.Width != CachedWindowSize.Width || WindowSize.Height != CachedWindowSize.Height;
        if (bResizeSwapchain)
        {
            FrameScheduler.WaitForAllFrames();
            LE_LOG(LogXBD, Info, "Explicitly recreating swapchain at {}x{}.", WindowSize.Width, WindowSize.Height);
            LE_LOG(LogXBD, Info, "Scene texture uploads at resize: {}.", Assets->Provider.GetTextureUploadCount());
            if (!Swapchain->Resize(WindowSize.Width, WindowSize.Height))
            {
                bCanRender = false;
                return EApplicationTickResult::Continue;
            }
            CachedWindowSize = WindowSize;
            bSwapchainRecreateRequested = false;
        }

        const FSceneRenderTargetDesc Target = GetBackBufferTarget(*Swapchain, CachedWindowSize);
        if (!Renderer.Resize(FrameScheduler, Target))
        {
            LE_LOG(LogXBD, Error, "Resize candidate creation failed; preserving the previous render resources.");
        }
        bCanRender = Renderer.GetTargetDesc().Format == Target.Format;
        return EApplicationTickResult::Continue;
    }

    EApplicationTickResult Update(const FApplicationTickContext& Context)
    {
#if WITH_EDITOR && PLATFORM_WINDOWS
        // Scene requests may clear queued input, so capture the independent
        // shader edge before applying any of their changes.
        const bool bShaderRequested = WindowModule.GetInputFrame().WasKeyPressed(EPlatformKey::F7);
#endif
        if (!ProcessSceneRequests())
        {
            return EApplicationTickResult::Exit;
        }
#if WITH_EDITOR && PLATFORM_WINDOWS
        ShaderPreview.Update(bShaderRequested, ShaderBundle.GetShaders().BasePassPixel.Code, FrameScheduler, Renderer);
#endif

        FWorld& World = Scene->GetWorld();
        const FWorldTickContext WorldContext{Context.FrameIndex, Context.DeltaSeconds, Context.ElapsedSeconds};
        const EWorldTickResult Begin = World.BeginTick(WorldContext);
        if (Begin != EWorldTickResult::Success && Begin != EWorldTickResult::CompletedWithCommandFailures)
        {
            LE_LOG(LogXBD, Error, "Demo World could not begin tick: {}.", static_cast<uint32>(Begin));
            return EApplicationTickResult::Exit;
        }
        const auto UpdateResult = Begin == EWorldTickResult::Success ? World.UpdateNativeGameplaySystems() : ENativeGameplayUpdateResult::InvalidState;
        const auto End = World.EndTick();
        if (UpdateResult != ENativeGameplayUpdateResult::Success || End != EWorldTickResult::Success)
        {
            LE_LOG(LogXBD, Error, "Demo World update failed: {} / {}.", static_cast<uint32>(UpdateResult), static_cast<uint32>(End));
            return EApplicationTickResult::Exit;
        }
        if (CameraNavigation.Update(WindowModule.GetInputFrame(), Context.DeltaSeconds))
        {
            bNavigationUpdated = true;
        }
        return UpdateRenderResources();
    }

    EApplicationTickResult Render()
    {
        if (!bCanRender)
        {
            return EApplicationTickResult::Continue;
        }

#if WITH_EDITOR && PLATFORM_WINDOWS
        uint64 RecordedShaderRequest = 0;
        auto RecordFrame = [this, &RecordedShaderRequest](LE::FRenderFrameScope& ScheduledFrame) {
#else
        auto RecordFrame = [this](LE::FRenderFrameScope& ScheduledFrame) {
#endif
            LE::FWorldRenderSnapshot RenderSnapshot;
            LE::FWorldRenderSnapshotBuildReport SnapshotReport;
            LE::FWorldRenderSnapshotBuildContext SnapshotContext;
            SnapshotContext.FrameIndex = ScheduledFrame.GetFrameIndex();
            const FSceneRenderTargetDesc Target = Renderer.GetTargetDesc();
            SnapshotContext.ViewportWidth = Target.Width;
            SnapshotContext.ViewportHeight = Target.Height;
            SnapshotContext.CameraOverride = {NavigationCameraId, CameraNavigation.GetCameraToWorld()};
            if (WorldRenderer.BuildSnapshot(Scene->GetWorld(), Assets->GetRegistry(), SnapshotContext, RenderSnapshot, &SnapshotReport) != LE::EWorldRenderSnapshotBuildResult::Success)
            {
                LE_LOG(LogXBD, Error, "Demo World snapshot build failed: Result={} MeshIndex={} AssetResolve={}.", static_cast<uint32>(SnapshotReport.Result), SnapshotReport.MeshIndex, static_cast<uint32>(SnapshotReport.AssetResolveResult));
                return LE::ERenderFrameRecordResult::Error;
            }

#if WITH_EDITOR && PLATFORM_WINDOWS
            RecordedShaderRequest = ShaderPreview.GetPendingPresentation();
#endif
            return Renderer.RecordFrame(ScheduledFrame, *Swapchain, RenderSnapshot.GetRenderScene());
        };

        const FRenderFrameResult FrameResult = FrameScheduler.ExecuteFrame(Swapchain, RecordFrame);
#if WITH_EDITOR && PLATFORM_WINDOWS
        if (FrameResult.PresentStatus == ERHISwapchainStatus::Success)
        {
            ShaderPreview.ConfirmPresentation(RecordedShaderRequest);
        }
#endif
        if (FrameResult.Action == ERenderFrameAction::RecreateSwapchain)
        {
            bSwapchainRecreateRequested = true;
            return EApplicationTickResult::Continue;
        }
        if (FrameResult.Action == ERenderFrameAction::Exit)
        {
            LE_LOG(LogXBD, Error, "Explicit frame failed. Acquire={}, Submit={}, Present={}.", static_cast<uint32>(FrameResult.AcquireStatus), static_cast<uint32>(FrameResult.SubmitResult), static_cast<uint32>(FrameResult.PresentStatus));
            return EApplicationTickResult::Exit;
        }
        bPreviousFramePresented = FrameResult.PresentStatus == ERHISwapchainStatus::Success;
        return EApplicationTickResult::Continue;
    }

    static bool StartupThunk(void* Context) noexcept { return static_cast<FDemoRenderRuntimeModule*>(Context)->Startup(); }

    static void ShutdownThunk(void* Context) noexcept { static_cast<FDemoRenderRuntimeModule*>(Context)->Shutdown(); }

private:
    enum class EScenePresentationReport : uint8
    {
        None,
        Reloaded,
        Preserved,
    };

    struct FScenePublicationResult
    {
        const char* Error = nullptr;
        bool bNavigationPreserved = false;
    };

    bool ProcessSceneRequests() noexcept
    {
        const bool bReloadRequested = WindowModule.GetInputFrame().WasKeyPressed(EPlatformKey::F5);
        if (bPreviousFramePresented)
        {
            bPreviousFramePresented = false;
            if (PendingPresentationReport == EScenePresentationReport::Reloaded)
            {
                LE_LOG(LogXBD, Info, "Presented reloaded scene '{}'.", ActiveScenePath.Data());
            }
            else if (PendingPresentationReport == EScenePresentationReport::Preserved)
            {
                LE_LOG(LogXBD, Info, "Presented the preserved scene with {} texture uploads.", Assets->Provider.GetTextureUploadCount());
            }
            PendingPresentationReport = EScenePresentationReport::None;
#if WITH_EDITOR
            if (PendingAssetPresentation)
            {
                LogEditorContentRefresh("presented", "ok", ActiveGeneration, PendingAssetNavigation);
                PendingAssetPresentation = false;
            }
#endif
            if (!RunRequestedSceneTest())
            {
                return false;
            }
        }

#if WITH_EDITOR
        if (WindowModule.GetInputFrame().WasKeyPressed(EPlatformKey::F6))
        {
            RefreshPublishedContent();
            return true;
        }
#endif
        if (bReloadRequested)
        {
            if (PublishScene(ActiveScenePath.View(), Assets->Catalog).Error == nullptr)
            {
                LE_LOG(LogXBD, Info, "Scene reload accepted '{}'.", ActiveScenePath.Data());
                PendingPresentationReport = EScenePresentationReport::Reloaded;
            }
            else
            {
                LE_LOG(LogXBD, Info, "Scene reload rejected '{}'; keeping the active scene.", ActiveScenePath.Data());
                PendingPresentationReport = EScenePresentationReport::Preserved;
            }
        }
        return true;
    }

    bool RunRequestedSceneTest() noexcept
    {
#if WITH_EDITOR
        if (!ContentPublicationRoot.IsEmpty())
        {
            return true;
        }
#endif
        if (bSceneSwitchAttempted || (bTestAfterNavigation && !bNavigationUpdated))
        {
            return true;
        }
        bSceneSwitchAttempted = true;
        if (bTestAfterNavigation)
        {
            const auto& Pose = CameraNavigation.GetCameraToWorld();
            LE_LOG(LogXBD, Info, "Presented navigated camera at ({}, {}, {}), forward ({}, {}, {}).", Pose(0, 3), Pose(1, 3), Pose(2, 3), -Pose(0, 2), -Pose(1, 2), -Pose(2, 2));
            if (!SaveRequestedScene())
            {
                return false;
            }
        }
        const char* const NextScene = std::getenv("LIMITLESS_DEMO_NEXT_SCENE");
        if (NextScene != nullptr && PublishScene(NextScene, Assets->Catalog).Error != nullptr)
        {
            LE_LOG(LogXBD, Info, "Keeping the active scene after replacement failed.");
            PendingPresentationReport = EScenePresentationReport::Preserved;
        }
        return true;
    }

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
                LE_LOG(LogXBD, Error, "Material GPU construction: {}.", DescribeTextureUploadResult(Assets.Provider.GetLastTextureUploadResult()));
            }
        }
        return Result == EAssetLoadResult::Success;
    }

    static void LogAssetFailure(const FAssetLoadDiagnostic& Diagnostic) noexcept { LE_LOG(LogXBD, Error, "Content asset '{}' ({}), dependency {}: load={}, file={}, format={}.", Diagnostic.Path.Data(), Diagnostic.Id.ToString().Data(), Diagnostic.DependencyId.ToString().Data(), static_cast<uint32>(Diagnostic.Result), DescribeFileSystemResult(Diagnostic.FileResult), static_cast<uint32>(Diagnostic.FormatResult)); }

    static SharedPtr<FAssetCatalog> BuildContentCatalog(StringView AssetRoot, const char*& Error) noexcept
    {
        auto Catalog = TryMakeShared<FAssetCatalog>();
        if (!Catalog)
        {
            LE_LOG(LogXBD, Error, "Could not allocate a content catalog.");
            Error = "catalog_allocation_failed";
            return {};
        }
        const FAssetMount Mount{"Game", AssetRoot};
        FAssetLoadDiagnostic Diagnostic;
        if (Catalog->Rebuild({&Mount, 1}, &Diagnostic) != EAssetLoadResult::Success)
        {
            LogAssetFailure(Diagnostic);
            Error = "asset_catalog_error";
            return {};
        }
        return Catalog;
    }

#if WITH_EDITOR
    void LogEditorContentRefresh(const char* Status, const char* Code, const FUuid Candidate, const char* Navigation = "preserved") noexcept
    {
        LE_LOG(LogXBD, Info, "Editor content refresh {{\"status\":\"{}\",\"code\":\"{}\",\"active\":\"{}\",\"candidate\":\"{}\",\"navigation\":\"{}\"}}",
               Status, Code, ActiveGeneration.ToString().Data(), Candidate.ToString().Data(), Navigation);
    }

    bool ReadPublishedGeneration(FDemoContentGeneration& Generation) noexcept
    {
        FDemoContentGenerationDiagnostic Diagnostic;
        if (ResolveDemoContentGeneration(ContentPublicationRoot.View(), Generation, Diagnostic))
        {
            return true;
        }
        LE_LOG(LogXBD, Error, "Editor content source '{}': {} ({}).", ContentPublicationRoot.Data(), Diagnostic.Message, DescribeFileSystemResult(Diagnostic.FileResult));
        LogEditorContentRefresh("rejected", Diagnostic.Code, Diagnostic.GenerationId);
        return false;
    }

    static EFileSystemResult ResolveAuthorScenePath(StringView Path, String& Out) noexcept
    {
        // ResolveContentFile verifies that the parent is absolute and that the
        // leaf is an ordinary file. Scene writes are left to the author.
        std::size_t NameOffset = Path.Size();
        while (NameOffset != 0)
        {
            const char Character = Path[NameOffset - 1];
#if PLATFORM_WINDOWS
            if (Character == '/' || Character == '\\')
#else
            if (Character == '/')
#endif
            {
                break;
            }
            --NameOffset;
        }
        if (NameOffset == 0 || NameOffset == Path.Size())
        {
            return EFileSystemResult::InvalidPath;
        }
        return ResolveContentFile(Path.Substr(0, NameOffset), Path.Substr(NameOffset, Path.Size() - NameOffset), Out);
    }

    bool InitializePublishedContent(StringView PublicationRoot) noexcept
    {
        const StringView AuthorScene(std::getenv("LIMITLESS_DEMO_SCENE"));
        if (AuthorScene.IsEmpty())
        {
            LE_LOG(LogXBD, Error, "Editor content requires LIMITLESS_DEMO_SCENE pointing to a separate absolute author scene path.");
            LogEditorContentRefresh("rejected", "author_scene_required", {});
            return false;
        }
        if (!ContentPublicationRoot.TryAssign(PublicationRoot))
        {
            LogEditorContentRefresh("rejected", "generation_out_of_memory", {});
            return false;
        }
        FDemoContentGeneration Generation;
        if (!ReadPublishedGeneration(Generation))
        {
            return false;
        }
        String ScenePath;
        const auto SceneFile = ResolveAuthorScenePath(AuthorScene, ScenePath);
        if (SceneFile != EFileSystemResult::Success || ScenePath.View() == Generation.ScenePath.View())
        {
            LE_LOG(LogXBD, Error, "Editor author scene '{}': expected a separate absolute author scene file ({}).", AuthorScene.Data(), DescribeFileSystemResult(SceneFile));
            LogEditorContentRefresh("rejected", "author_scene_invalid", Generation.Id);
            return false;
        }

        const char* CatalogError = nullptr;
        const auto Catalog = BuildContentCatalog(Generation.AssetRoot.View(), CatalogError);
        if (!Catalog)
        {
            LogEditorContentRefresh("rejected", CatalogError, Generation.Id);
            return false;
        }
        const auto Result = PublishScene(ScenePath.View(), Catalog);
        if (Result.Error != nullptr)
        {
            LogEditorContentRefresh("rejected", Result.Error, Generation.Id);
            return false;
        }
        ActiveGeneration = Generation.Id;
        LogEditorContentRefresh("mounted", "ok", ActiveGeneration, "reseeded");
        return true;
    }

    void RefreshPublishedContent() noexcept
    {
        if (ContentPublicationRoot.IsEmpty())
        {
            LogEditorContentRefresh("rejected", "publication_not_configured", {});
            return;
        }

        FDemoContentGeneration Candidate;
        if (!ReadPublishedGeneration(Candidate))
        {
            return;
        }
        if (Candidate.Id == ActiveGeneration)
        {
            LogEditorContentRefresh("unchanged", "same_generation", Candidate.Id);
            return;
        }
        if (PendingAssetPresentation)
        {
            LogEditorContentRefresh("rejected", "refresh_pending", Candidate.Id);
            return;
        }

        const char* CatalogError = nullptr;
        const auto Catalog = BuildContentCatalog(Candidate.AssetRoot.View(), CatalogError);
        if (!Catalog)
        {
            LogEditorContentRefresh("rejected", CatalogError, Candidate.Id);
            return;
        }
        const auto Result = PublishScene(ActiveScenePath.View(), Catalog, false, true);
        if (Result.Error != nullptr)
        {
            LogEditorContentRefresh("rejected", Result.Error, Candidate.Id);
            return;
        }
        ActiveGeneration = Candidate.Id;
        PendingAssetNavigation = Result.bNavigationPreserved ? "preserved" : "reseeded";
        PendingAssetPresentation = true;
    }
#endif

    bool InitializeContent(StringView ExecutableDirectory) noexcept
    {
#if WITH_EDITOR
        if (const char* EditorContent = std::getenv("LIMITLESS_EDITOR_CONTENT"))
        {
            return InitializePublishedContent(EditorContent);
        }
#endif
        const char* RootOverride = std::getenv("LIMITLESS_DEMO_CONTENT");
        const String ContentRoot = RootOverride != nullptr ? String(RootOverride) : JoinDemoPath(ExecutableDirectory, "DemoContent");
        const String AssetRoot = JoinDemoPath(ContentRoot.View(), "Assets");
        const char* CatalogError = nullptr;
        const auto Catalog = BuildContentCatalog(AssetRoot.View(), CatalogError);
        const char* InputOverride = std::getenv("LIMITLESS_DEMO_SCENE");
        const String ScenePath = InputOverride != nullptr ? String(InputOverride) : JoinDemoPath(ContentRoot.View(), "Scene.json");
        bTestAfterNavigation = StringView(std::getenv("LIMITLESS_DEMO_TEST_AFTER_NAVIGATION")) == StringView("1");
        return Catalog && PublishScene(ScenePath.View(), Catalog, true).Error == nullptr && (bTestAfterNavigation || SaveRequestedScene());
    }

    bool SaveRequestedScene() noexcept
    {
        const char* const SavePath = std::getenv("LIMITLESS_DEMO_SAVE_SCENE");
        if (SavePath == nullptr)
        {
            return true;
        }
        FSceneOperationDiagnostic SaveDiagnostic;
        const auto SaveResult = SaveScene(SavePath, *Scene, Assets->GetRegistry(), &SaveDiagnostic);
        if (SaveResult != ESceneOperationResult::Success)
        {
            LE_LOG(LogXBD, Error, "Scene save '{}': {} ({}).", SavePath, DescribeSceneOperationResult(SaveResult), DescribeFileSystemResult(SaveDiagnostic.FileResult));
            return false;
        }
        LE_LOG(LogXBD, Info, "Saved scene '{}'.", SavePath);
        return true;
    }

    static bool ApplyRequestedSceneEdit(FScene& Candidate) noexcept
    {
        const char* EditId = std::getenv("LIMITLESS_DEMO_EDIT_ENTITY");
        const char* Translation = std::getenv("LIMITLESS_DEMO_EDIT_X");
        if (EditId == nullptr && Translation == nullptr)
        {
            return true;
        }

        FWorld& World = Candidate.GetWorld();
        FSceneEntityId Id;
        FEntity Entity;
        FTransform Transform;
        float X = 0.0f;
        const StringView Text(Translation);
        if (EditId == nullptr || Text.IsEmpty() || !FSceneEntityId::TryParse(EditId, Id) || !Candidate.TryFindEntity(Id, Entity) || !World.TryGetLocalTransform(Entity, Transform))
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
        return true;
    }

    FScenePublicationResult PublishScene(StringView Path, const SharedPtr<FAssetCatalog>& Catalog, const bool bApplyStartupEdit = false,
                                         [[maybe_unused]] const bool bPreserveNavigation = false) noexcept
    {
        String CandidatePath;
        if (!CandidatePath.TryAssign(Path))
        {
            LE_LOG(LogXBD, Error, "Could not store the candidate scene path.");
            return {"scene_allocation_failed"};
        }
        auto CandidateAssets = TryMakeUnique<FDemoSceneAssets>(Catalog);
        UniquePtr<FScene> Candidate;
        FSceneOperationDiagnostic Diagnostic;
        const char* const PrintablePath = CandidatePath.Data();
        if (!CandidateAssets)
        {
            LE_LOG(LogXBD, Error, "Could not allocate a scene asset session for '{}'.", PrintablePath);
            return {"scene_allocation_failed"};
        }
        if (!Renderer.InitializeAssetProvider(CandidateAssets->Provider))
        {
            LE_LOG(LogXBD, Error, "Could not initialize material GPU resources for '{}'.", PrintablePath);
            return {"scene_asset_provider_failed"};
        }
        const FSceneAssetResolver Resolver{CandidateAssets.Get(), &CandidateAssets->GetRegistry(), &ResolveMesh, &ResolveMaterial};
        const auto Result = LoadScene(CandidatePath.View(), Resolver, Candidate, &Diagnostic);
        if (Result != ESceneOperationResult::Success)
        {
            LE_LOG(LogXBD, Error, "Scene '{}': {} ({}:{} {}, entity {}, field {}, asset {}, file {}).", PrintablePath, DescribeSceneOperationResult(Result), Diagnostic.Document.Line, Diagnostic.Document.Column, DescribeSceneDocumentResult(Diagnostic.Document.Result), Diagnostic.Entity.ToString().Data(), String(Diagnostic.Document.Field).Data(), Diagnostic.Asset.ToString().Data(), DescribeFileSystemResult(Diagnostic.FileResult));
            switch (Result)
            {
            case ESceneOperationResult::InvalidDocument:
                return {"scene_document_error"};
            case ESceneOperationResult::FileReadFailed:
                return {"scene_file_error"};
            case ESceneOperationResult::AssetResolutionFailed:
            case ESceneOperationResult::InvalidAssetHandle:
                return {"scene_asset_error"};
            default:
                return {"scene_load_failed"};
            }
        }
        if (bApplyStartupEdit && !ApplyRequestedSceneEdit(*Candidate))
        {
            return {"scene_startup_edit_failed"};
        }
        FWorld& CandidateWorld = Candidate->GetWorld();
        if (CandidateWorld.CompileNativeGameplaySystemSchedule() != ENativeGameplayScheduleResult::Success)
        {
            LE_LOG(LogXBD, Error, "Could not compile gameplay schedule for '{}'.", PrintablePath);
            return {"scene_schedule_failed"};
        }

        FWorldRenderSnapshot Preview;
        FWorldRenderSnapshotBuildReport PreviewReport;
        FWorldRenderSnapshotBuildContext PreviewContext;
        const FSceneRenderTargetDesc Target = Renderer.GetTargetDesc();
        PreviewContext.ViewportWidth = Target.Width;
        PreviewContext.ViewportHeight = Target.Height;
        if (WorldRenderer.BuildSnapshot(CandidateWorld, CandidateAssets->GetRegistry(), PreviewContext, Preview, &PreviewReport) != EWorldRenderSnapshotBuildResult::Success)
        {
            const bool bMissingUV = PreviewReport.Result == EWorldRenderSnapshotBuildResult::MissingMeshUV;
            if (bMissingUV || PreviewReport.Result == EWorldRenderSnapshotBuildResult::InvalidNormalTransform)
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
                    if (bMissingUV)
                    {
                        LE_LOG(LogXBD, Error, "Scene '{}': entity {} mesh {} has no UVs for material {}.", PrintablePath, EntityId.ToString().Data(), MeshId.ToString().Data(), MaterialId.ToString().Data());
                    }
                    else
                    {
                        LE_LOG(LogXBD, Error, "Scene '{}': entity {} mesh {} has an invalid normal transform.", PrintablePath, EntityId.ToString().Data(), MeshId.ToString().Data());
                    }
                    return {"scene_preflight_failed"};
                }
            }
            LE_LOG(LogXBD, Error, "Scene '{}' is not renderable: {} (mesh {}).", PrintablePath, static_cast<uint32>(PreviewReport.Result), PreviewReport.MeshIndex);
            return {"scene_preflight_failed"};
        }

        if (Scene)
        {
            if (Scene->GetWorld().GetTickState() != EWorldTickState::Idle)
            {
                return {"scene_world_busy"};
            }
            FrameScheduler.WaitForAllFrames();
        }
        const FWorldRenderCameraPose& CameraPose = Preview.GetAuthoredPrimaryCameraPose();
#if WITH_EDITOR
        FSceneEntityId CameraSceneId;
        (void)Candidate->TryGetEntityId(CameraPose.Entity, CameraSceneId);
        const bool bKeepNavigation = bPreserveNavigation && CameraSceneId.IsValid() && CameraSceneId == NavigationSceneCameraId;
        NavigationSceneCameraId = CameraSceneId;
#else
        constexpr bool bKeepNavigation = false;
#endif
        Scene = std::move(Candidate);
        Assets = std::move(CandidateAssets);
        ActiveScenePath.Swap(CandidatePath);
        NavigationCameraId = CameraPose.PrimitiveId;
        if (!bKeepNavigation)
        {
            CameraNavigation.Start(CameraPose.CameraToWorld);
            WindowModule.ResetInputForScene();
        }
        LE_LOG(LogXBD, Info, "Loaded scene '{}' with {} entities and {} visible meshes.", ActiveScenePath.Data(), Scene->GetWorld().GetEntityCount(), Preview.GetRenderScene().Meshes.Size());
        const auto& Pose = CameraPose.CameraToWorld;
        const auto& Projection = Preview.GetRenderScene().Cameras[0].Projection;
        LE_LOG(LogXBD, Info, "Authored camera position ({}, {}, {}), forward ({}, {}, {}), vertical FOV {} rad.", Pose(0, 3), Pose(1, 3), Pose(2, 3), -Pose(0, 2), -Pose(1, 2), -Pose(2, 2), 2.0f * std::atan(1.0f / std::abs(Projection(1, 1))));
        LE_LOG(LogXBD, Info, "Scene texture uploads at publication: {}.", Assets->Provider.GetTextureUploadCount());
        return {nullptr, bKeepNavigation};
    }

    static constexpr uint32 DefaultWindowWidth = 1280;
    static constexpr uint32 DefaultWindowHeight = 720;

    FDemoWindowRuntimeModule& WindowModule;
    UniquePtr<FDemoSceneAssets> Assets;
    UniquePtr<FScene> Scene;
    String ActiveScenePath;
    FWorldRenderer WorldRenderer;
    FDemoCameraNavigation CameraNavigation;
    FRenderPrimitiveId NavigationCameraId;
    FRHIDevice* Device = nullptr;
    FRHISwapchain* Swapchain = nullptr;
    FRenderFrameScheduler FrameScheduler;
    FDemoShaderBundle ShaderBundle;
    FSceneRenderer Renderer;
#if WITH_EDITOR && PLATFORM_WINDOWS
    FEditorShaderPreview ShaderPreview;
#endif
    FPlatformWindowSize CachedWindowSize{DefaultWindowWidth, DefaultWindowHeight};
    EScenePresentationReport PendingPresentationReport = EScenePresentationReport::None;
#if WITH_EDITOR
    String ContentPublicationRoot;
    FUuid ActiveGeneration;
    FSceneEntityId NavigationSceneCameraId;
    const char* PendingAssetNavigation = "preserved";
    bool PendingAssetPresentation = false;
#endif
    bool bCanRender = false;
    bool bNavigationUpdated = false;
    bool bTestAfterNavigation = false;
    bool bSceneSwitchAttempted = false;
    bool bPreviousFramePresented = false;
    bool bRenderingSuspended = false;
    bool bSwapchainRecreateRequested = false;
    bool bShutdown = false;
};

class FDemoApplication final : public IApplication
{
public:
    FDemoApplication() noexcept : RenderModule(WindowModule) {}

    ~FDemoApplication() override { Shutdown(); }

    void RegisterModules(FRuntimeModuleRegistry& Registry) override
    {
        FRuntimeModuleDescriptor WindowDescriptor;
        WindowDescriptor.Name = "Demo.Window";
        WindowDescriptor.Context = &WindowModule;
        WindowDescriptor.Startup = &FDemoWindowRuntimeModule::StartupThunk;
        WindowDescriptor.Shutdown = &FDemoWindowRuntimeModule::ShutdownThunk;
        static_cast<void>(Registry.RegisterModule(WindowDescriptor));

        const StringView RenderDependencies[] = {"Demo.Window"};
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
