#pragma once

#include "CoreMinimal.h"
#include "RFGMinimal.h"
#include "RendererFrameContext.h"

namespace LE
{

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
    LE::FRFGInstance& GetGraphInstance();
    const LE::FRFGInstance& GetGraphInstance() const;

private:
    bool bInitialized = false;
    LE::FRFGPassRegistry PassRegistry;
    LE::FRFGRuntime GraphRuntime;
    LE::FRFGInstance GraphInstance;
};

} // namespace LE
