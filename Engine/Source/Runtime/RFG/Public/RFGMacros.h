#pragma once

#include "Core/RFGTypes.h"
#include "Record/RFGBuilder.h"

#define ENQUEUE_RENDER_PASS(BuilderRef, PassName, PassTypeName, ParameterBlock, ExecuteCallback) \
    do                                                                                             \
    {                                                                                              \
        const LE::FRFGPassHandle EnqueuedPassHandle =                                              \
            (BuilderRef).AddPass(                                                                  \
                (PassName),                                                                        \
                (PassTypeName),                                                                    \
                (ParameterBlock),                                                                  \
                LE::ERFGPassFlags::None,                                                           \
                LE::ERFGQueueType::Graphics,                                                       \
                LE::FRFGSourceLocation{ __FILE__, static_cast<LE::uint32>(__LINE__) });            \
        (BuilderRef).SetPassCallback(EnqueuedPassHandle, (ExecuteCallback));                       \
    } while (0)
