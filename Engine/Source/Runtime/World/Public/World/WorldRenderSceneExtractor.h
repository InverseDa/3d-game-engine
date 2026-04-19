#pragma once

#include "CoreMinimal.h"

class FRenderScene;
class FWorld;

class WORLD_API FWorldRenderSceneExtractor
{
public:
    static void ExtractRenderScene(const FWorld& World, FRenderScene& OutRenderScene);
};
