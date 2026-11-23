#include "DemoCameraNavigation.h"
#include "Scene/Scene.h"
#include "WorldRenderer/WorldRenderer.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace LE
{
namespace
{

bool ExpectNavigation(const bool bPassed, const char* Label)
{
    if (!bPassed)
    {
        std::fprintf(stderr, "FAILED: Demo camera navigation: %s\n", Label);
    }
    return bPassed;
}

constexpr FPlatformWindowId TestWindow = 1;
using Math::Matrix4f;
using Math::Vector3f;

void HoldKey(FPlatformWindowInput& Input, const EPlatformKey Key)
{
    FPlatformEvent Press;
    Press.Type = EPlatformEventType::KeyPressed;
    Press.WindowId = TestWindow;
    Press.Key = Key;
    Input.Apply(TestWindow, Press);
}

Vector3f GetPosition(const Matrix4f& Pose)
{
    return Vector3f(Pose(0, 3), Pose(1, 3), Pose(2, 3));
}

bool Near(const float A, const float B)
{
    return std::abs(A - B) < 1.0e-5f;
}

bool TestMovement()
{
    struct FMoveCase
    {
        EPlatformKey Key;
        Vector3f Direction;
    };
    const FMoveCase Cases[] = {
        {EPlatformKey::W, Vector3f(0.0f, 0.0f, -1.0f)},
        {EPlatformKey::S, Vector3f(0.0f, 0.0f, 1.0f)},
        {EPlatformKey::A, Vector3f(-1.0f, 0.0f, 0.0f)},
        {EPlatformKey::D, Vector3f(1.0f, 0.0f, 0.0f)},
    };
    bool bPassed = true;
    FDemoCameraNavigation Navigation;
    FPlatformWindowInput Input;
    for (const auto& Move : Cases)
    {
        Input.Reset();
        Navigation.Start(Matrix4f::Identity());
        HoldKey(Input, Move.Key);
        bPassed &= ExpectNavigation(Navigation.Update(Input.GetFrame(), 0.01) && (GetPosition(Navigation.GetCameraToWorld()) - Move.Direction * 0.03f).Length() < 1.0e-5f, "WASD each moves along its camera axis without capture");
    }

    Navigation.Start(Matrix4f::Identity());
    Input.Reset();
    HoldKey(Input, EPlatformKey::W);
    HoldKey(Input, EPlatformKey::D);
    Navigation.Update(Input.GetFrame(), 30.0);
    const Vector3f Diagonal = GetPosition(Navigation.GetCameraToWorld());
    bPassed &= ExpectNavigation(Diagonal[0] > 0.0f && Diagonal[2] < 0.0f && Near(Diagonal.Length(), 0.15f), "normalized diagonal with a capped long frame");

    HoldKey(Input, EPlatformKey::A);
    HoldKey(Input, EPlatformKey::S);
    bPassed &= ExpectNavigation(!Navigation.Update(Input.GetFrame(), 0.01) && GetPosition(Navigation.GetCameraToWorld()) == Diagonal, "opposite keys cancel");
    return bPassed;
}

bool TestLook()
{
    FDemoCameraNavigation Navigation;
    Matrix4f Authored = Matrix4f::Identity();
    Authored(2, 3) = 3.0f;
    Navigation.Start(Authored);
    bool bPassed = ExpectNavigation(!Navigation.Update({}, 0.05) && Navigation.GetCameraToWorld() == Authored, "idle keeps the authored matrix");

    FPlatformInputFrame Input;
    Input.MouseDeltaX = 150;
    Input.MouseDeltaY = -80;
    Navigation.Update(Input, 0.0);
    const Matrix4f Looked = Navigation.GetCameraToWorld();
    Navigation.Start(Authored);
    Navigation.Update(Input, 0.04);
    bPassed &= ExpectNavigation(Looked == Navigation.GetCameraToWorld() && Looked(0, 2) < 0.0f && Looked(1, 2) < 0.0f && GetPosition(Looked) == GetPosition(Authored), "mouse yaw and pitch are independent of frame time");

    FPlatformWindowInput Keys;
    HoldKey(Keys, EPlatformKey::W);
    Navigation.Update(Keys.GetFrame(), 0.01);
    const Vector3f Travel = GetPosition(Navigation.GetCameraToWorld()) - GetPosition(Looked);
    bPassed &= ExpectNavigation(Travel[0] > 0.0f && Travel[1] > 0.0f && Travel[2] < 0.0f && Near(Travel.Length(), 0.03f), "movement follows the new view");

    Input.MouseDeltaX = 0;
    Input.MouseDeltaY = -1000000;
    Navigation.Update(Input, 0.01);
    const float PitchUp = -Navigation.GetCameraToWorld()(1, 2);
    Navigation.Update(Input, 0.01);
    bPassed &= ExpectNavigation(PitchUp > 0.99f && PitchUp < 1.0f && Near(-Navigation.GetCameraToWorld()(1, 2), PitchUp), "pitch is clamped before the upper pole");
    Input.MouseDeltaY = 1000000;
    Navigation.Update(Input, 0.01);
    bPassed &= ExpectNavigation(Near(-Navigation.GetCameraToWorld()(1, 2), -PitchUp), "pitch is clamped before the lower pole");
    return bPassed;
}

struct FNavigationSceneFiles
{
    String Before;
    String After;

    ~FNavigationSceneFiles()
    {
        if (!Before.IsEmpty())
        {
            static_cast<void>(std::remove(Before.Data()));
            static_cast<void>(std::remove(After.Data()));
        }
    }
};

bool TestNavigationAndSceneSave()
{
    using ESceneResult = ESceneOperationResult;
    FUuid FileId;
    FSceneEntityId ParentId;
    FSceneEntityId CameraId;
    if (!ExpectNavigation(FUuid::TryGenerate(FileId) && FSceneEntityId::TryParse("10000000-0000-4000-8000-000000000001", ParentId) && FSceneEntityId::TryParse("10000000-0000-4000-8000-000000000002", CameraId), "fixture IDs"))
    {
        return false;
    }

    FNavigationSceneFiles Files;
    Files.Before = FileId.ToString();
    Files.After = Files.Before;
    Files.Before.Append(".before.scene.json");
    Files.After.Append(".after.scene.json");

    FSceneDocument Document;
    auto& Parent = Document.Entities.EmplaceBack();
    Parent.Id = ParentId;
    Parent.bHasTransform = true;
    Parent.Transform.Translation[0] = 2.0f;
    auto& Camera = Document.Entities.EmplaceBack();
    Camera.Id = CameraId;
    Camera.Parent = ParentId;
    Camera.bHasTransform = true;
    Camera.bHasCamera = true;
    Camera.Camera.bActive = true;
    Camera.Camera.VerticalFieldOfViewRadians = 0.9f;
    Camera.Transform.Translation[2] = 4.0f;

    FAssetRegistry Registry;
    const FSceneAssetResolver Assets{nullptr, &Registry, nullptr, nullptr};
    UniquePtr<FScene> Scene;
    FWorldRenderer Renderer;
    FWorldRenderSnapshotBuildContext Context;
    Context.ViewportWidth = 640;
    Context.ViewportHeight = 480;
    FWorldRenderSnapshot Snapshot;
    if (!ExpectNavigation(RestoreScene(Document, Assets, Scene) == ESceneResult::Success && SaveScene(Files.Before.View(), *Scene, Registry) == ESceneResult::Success && Renderer.BuildSnapshot(Scene->GetWorld(), Registry, Context, Snapshot) == EWorldRenderSnapshotBuildResult::Success, "save and snapshot the authored scene"))
    {
        return false;
    }

    FDemoCameraNavigation Navigation;
    const auto Authored = Snapshot.GetAuthoredPrimaryCameraPose();
    Navigation.Start(Authored.CameraToWorld);
    FPlatformWindowInput Input;
    HoldKey(Input, EPlatformKey::W);
    Navigation.Update(Input.GetFrame(), 0.05);
    Context.CameraOverride = {Authored.PrimitiveId, Navigation.GetCameraToWorld()};
    if (!ExpectNavigation(Context.CameraOverride.CameraToWorld != Authored.CameraToWorld && Renderer.BuildSnapshot(Scene->GetWorld(), Registry, Context, Snapshot) == EWorldRenderSnapshotBuildResult::Success && SaveScene(Files.After.View(), *Scene, Registry) == ESceneResult::Success, "save through the Scene entry point after using the navigated pose"))
    {
        return false;
    }
    Array<uint8> Before;
    Array<uint8> After;
    bool bPassed = ExpectNavigation(ReadContentFile(Files.Before.View(), MaxSceneDocumentBytes, Before) == EFileSystemResult::Success && ReadContentFile(Files.After.View(), MaxSceneDocumentBytes, After) == EFileSystemResult::Success && Before.Size() == After.Size() && std::memcmp(Before.Data(), After.Data(), Before.Size()) == 0, "explicit saves before and after navigation are byte-identical");

    UniquePtr<FScene> Reloaded;
    Context.CameraOverride = {};
    if (!ExpectNavigation(LoadScene(Files.After.View(), Assets, Reloaded) == ESceneResult::Success && Renderer.BuildSnapshot(Reloaded->GetWorld(), Registry, Context, Snapshot) == EWorldRenderSnapshotBuildResult::Success && Snapshot.GetAuthoredPrimaryCameraPose().CameraToWorld == Authored.CameraToWorld, "scene reload reconstructs the authored pose, including the parent transform"))
    {
        return false;
    }
    const FScene* const Previous = Reloaded.Get();
    Document.Entities[1].Camera.VerticalFieldOfViewRadians = 0.0f;
    bPassed &= ExpectNavigation(RestoreScene(Document, Assets, Reloaded) == ESceneResult::InvalidDocument && Reloaded.Get() == Previous, "invalid candidate leaves the previous scene available");
    Document.Entities[1].Camera.VerticalFieldOfViewRadians = 0.9f;
    Document.Entities[1].Transform.Translation[2] = 7.0f;
    bPassed &= ExpectNavigation(RestoreScene(Document, Assets, Reloaded) == ESceneResult::Success && Renderer.BuildSnapshot(Reloaded->GetWorld(), Registry, Context, Snapshot) == EWorldRenderSnapshotBuildResult::Success && Near(Snapshot.GetAuthoredPrimaryCameraPose().CameraToWorld(2, 3), 7.0f), "valid candidate supplies its own camera seed");
    return bPassed;
}

} // namespace

bool RunDemoCameraNavigationTests()
{
    const bool bMovement = TestMovement();
    const bool bLook = TestLook();
    const bool bSave = TestNavigationAndSceneSave();
    return bMovement && bLook && bSave;
}

} // namespace LE
