#pragma once

#include "Containers/Span.h"
#include "Containers/StringView.h"

namespace LE::Detail
{

int RunGltfImportCommand(Span<const StringView> Arguments) noexcept;

} // namespace LE::Detail
