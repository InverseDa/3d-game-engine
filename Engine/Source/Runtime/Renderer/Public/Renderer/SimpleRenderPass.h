#pragma once

#include "RenderPass.h"


namespace LE
{

class RENDERER_API FSimpleRenderPass final : public IRenderPass
{
public:
    using FSetupCallback = LE::Function<void(FRenderPassSetupContext&)>;
    using FRecordCallback = LE::Function<void(FRenderPassRecordContext&)>;

public:
    FSimpleRenderPass(
        const char* InPassName,
        LE::ERFGQueueType InQueueType,
        FSetupCallback InSetupCallback,
        FRecordCallback InRecordCallback)
        : PassName(InPassName)
        , QueueType(InQueueType)
        , SetupCallback(std::move(InSetupCallback))
        , RecordCallback(std::move(InRecordCallback))
    {
    }

public:
    const char* GetPassName() const override
    {
        return PassName;
    }

    LE::ERFGQueueType GetQueueType() const override
    {
        return QueueType;
    }

    void Setup(FRenderPassSetupContext& Context) override
    {
        if (SetupCallback)
        {
            SetupCallback(Context);
        }
    }

    void Record(FRenderPassRecordContext& Context) override
    {
        if (RecordCallback)
        {
            RecordCallback(Context);
        }
    }

private:
    const char* PassName = "";
    LE::ERFGQueueType QueueType = LE::ERFGQueueType::Graphics;
    FSetupCallback SetupCallback;
    FRecordCallback RecordCallback;
};

} // namespace LE
