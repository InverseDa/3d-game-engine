#pragma once

#include "Containers/Span.h"
#include "Containers/StringView.h"
#include "Types/EngineTypes.h"

namespace LE
{

enum class EAssetFormatResult : uint8;

int RunAssetImport(Span<const StringView> Arguments) noexcept;

namespace Detail
{
const char* DescribeImportFormatError(EAssetFormatResult Result) noexcept;
}

} // namespace LE
