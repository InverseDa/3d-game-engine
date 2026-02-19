#pragma once

#include "Core/RFGTypes.h"
#include "Record/RFGBuilder.h"

#define ENQUEUE_RENDER_PASS(BuilderRef, PassName, PassTypeName, ParameterBlock, ExecuteCallback) \
    do                                                                                             \
    {                                                                                              \
        const FRFGPassHandle EnqueuedPassHandle =                                                  \
            (BuilderRef).AddPass(                                                                  \
                (PassName),                                                                        \
                (PassTypeName),                                                                    \
                (ParameterBlock),                                                                  \
                ERFGPassFlags::None,                                                               \
                ERFGQueueType::Graphics,                                                           \
                FRFGSourceLocation{ __FILE__, static_cast<uint32>(__LINE__) });                   \
        (BuilderRef).SetPassCallback(EnqueuedPassHandle, (ExecuteCallback));                       \
    } while (0)
