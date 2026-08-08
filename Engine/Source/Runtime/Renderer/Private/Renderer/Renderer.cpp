#include "Renderer/Renderer.h"

#include "Renderer/RenderGraphBuilderBridge.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/RenderScene.h"
#include "RAL/RALDevice.h"
#include "RAL/RALQueue.h"
#include "RAL/RALSwapchain.h"

namespace LE
{

namespace
{
FRenderView BuildDefaultRenderView()
{
    FRenderView View;
    View.ViewId = 0;
    return View;
}
}

FRenderer::FRenderer() = default;

FRenderer::~FRenderer()
{
    Shutdown();
}

void FRenderer::Initialize()
{
    if (bInitialized)
    {
        return;
    }

    GraphRuntime.Initialize(&PassRegistry);
    GraphInstance.Initialize(&GraphRuntime);
    bInitialized = true;
}

void FRenderer::Shutdown()
{
    if (!bInitialized)
    {
        return;
    }

    GraphInstance.Shutdown();
    GraphRuntime.Shutdown();
    bInitialized = false;
}

bool FRenderer::IsInitialized() const
{
    return bInitialized;
}

void FRenderer::RenderFrame(const FRendererFrameContext& FrameContext, const FRenderScene* RenderScene)
{
    static const FRenderScene EmptyRenderScene;

    if (!bInitialized)
    {
        return;
    }

    if (FrameContext.Device == nullptr || FrameContext.CommandList == nullptr || FrameContext.Pipeline == nullptr)
    {
        return;
    }

    const FRenderScene& EffectiveRenderScene = RenderScene != nullptr ? *RenderScene : EmptyRenderScene;
    const FRenderView DefaultView = BuildDefaultRenderView();
    const FRenderView& EffectiveView = !FrameContext.ViewFamily.Views.IsEmpty() ? FrameContext.ViewFamily.Views.Front() : DefaultView;

    FRenderPipelinePlan PipelinePlan;
    FrameContext.Pipeline->BuildPasses(FrameContext, EffectiveRenderScene, EffectiveView, PipelinePlan);
    if (PipelinePlan.Passes.IsEmpty())
    {
        return;
    }

    LE::FRFGBuilder Builder = GraphInstance.CreateBuilder();
    FRenderGraphBuilderBridge GraphBridge(Builder);

    for (IRenderPass* Pass : PipelinePlan.Passes)
    {
        if (Pass == nullptr)
        {
            continue;
        }

        const LE::FRFGPassHandle PassHandle = Builder.AddPass(
            Pass->GetPassName(),
            Pass->GetPassName(),
            {},
            LE::ERFGPassFlags::None,
            Pass->GetQueueType());

        FRenderPassSetupContext SetupContext;
        SetupContext.GraphBridge = &GraphBridge;
        SetupContext.PassHandle = PassHandle;
        SetupContext.FrameContext = &FrameContext;
        SetupContext.RenderScene = &EffectiveRenderScene;
        SetupContext.RenderView = &EffectiveView;
        Pass->Setup(SetupContext);

        Builder.SetPassCallback(
            PassHandle,
            [Pass, &FrameContext, &EffectiveRenderScene, &EffectiveView](LE::FRFGPassContext& Context)
            {
                FRenderPassRecordContext RecordContext;
                RecordContext.PassContext = &Context;
                RecordContext.FrameContext = &FrameContext;
                RecordContext.RenderScene = &EffectiveRenderScene;
                RecordContext.RenderView = &EffectiveView;
                Pass->Record(RecordContext);
            });
    }

    const LE::FRFGGraphSignature Signature = Builder.BuildSignature();
    const LE::FRFGCompileResult CompileResult = GraphInstance.Compile(Builder.GetRecordedGraph(), Signature);

    LE::FRFGExecutionContext ExecutionContext;
    ExecutionContext.Device = FrameContext.Device;
    ExecutionContext.GraphicsQueue = FrameContext.Device->GetGraphicsQueue();
    ExecutionContext.CommandList = FrameContext.CommandList;

    LE::FRFGExecuteOptions ExecuteOptions;
    ExecuteOptions.bSubmitImmediately = true;
    ExecuteOptions.bWaitForCompletion = true;
    GraphInstance.Execute(CompileResult, Builder.GetRecordedGraph(), ExecutionContext, ExecuteOptions);

    if (FrameContext.bPresentAfterRender && FrameContext.Swapchain != nullptr)
    {
        FrameContext.Swapchain->Present();
    }
}

LE::FRFGInstance& FRenderer::GetGraphInstance()
{
    return GraphInstance;
}

const LE::FRFGInstance& FRenderer::GetGraphInstance() const
{
    return GraphInstance;
}

} // namespace LE
