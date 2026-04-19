#pragma once

#include "RenderPass.h"

#include <functional>

class RENDERER_API FSimpleRenderPass final : public IRenderPass
{
public:
    using FSetupCallback = std::function<void(FRenderPassSetupContext&)>;
    using FRecordCallback = std::function<void(FRenderPassRecordContext&)>;

public:
    FSimpleRenderPass(
        const char* InPassName,
        ERFGQueueType InQueueType,
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

    ERFGQueueType GetQueueType() const override
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
    ERFGQueueType QueueType = ERFGQueueType::Graphics;
    FSetupCallback SetupCallback;
    FRecordCallback RecordCallback;
};
