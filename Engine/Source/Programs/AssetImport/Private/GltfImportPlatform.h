#pragma once

#include "Containers/Span.h"
#include "Containers/StringView.h"
#include "Types/EngineTypes.h"

namespace LE::Detail
{

struct FGltfDiagnostic;

inline constexpr std::size_t DocumentDigestHexLength = 64;
bool HashDocument(Span<const uint8> Bytes, char (&Hex)[DocumentDigestHexLength]) noexcept;
bool SetBinaryInput() noexcept;
bool PublishGeneration(StringView OutputRoot, StringView Baseline, StringView Candidate, FGltfDiagnostic& Diagnostic) noexcept;

} // namespace LE::Detail
