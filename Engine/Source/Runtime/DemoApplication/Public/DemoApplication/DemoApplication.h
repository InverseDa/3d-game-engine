#pragma once

#include "Application/Application.h"

namespace LE
{

// The concrete demo and its native/render resources are allocated and destroyed
// inside DemoApplication, including when this module is a DLL.
DEMOAPPLICATION_API FApplicationPtr CreateDemoApplication();

} // namespace LE
