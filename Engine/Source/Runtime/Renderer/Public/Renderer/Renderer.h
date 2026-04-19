#pragma once

#include "CoreMinimal.h"
#include "RFGMinimal.h"
#include "RendererFrameContext.h"

class FRenderScene;

class RENDERER_API FRenderer final : public FNonCopyable
{
public:
    FRenderer();
    ~FRenderer();

public:
    void Initialize();
    void Shutdown();
    bool IsInitialized() const;

public:
    void RenderFrame(const FRendererFrameContext& FrameContext, const FRenderScene* RenderScene = nullptr);

public:
    FRFGInstance& GetGraphInstance();
    const FRFGInstance& GetGraphInstance() const;

private:
    bool bInitialized = false;
    FRFGPassRegistry PassRegistry;
    FRFGRuntime GraphRuntime;
    FRFGInstance GraphInstance;
};
