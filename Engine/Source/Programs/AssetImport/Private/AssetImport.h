#pragma once

#include "Containers/Span.h"
#include "Containers/StringView.h"

namespace LE
{

int RunAssetImport(Span<const StringView> Arguments) noexcept;

} // namespace LE
