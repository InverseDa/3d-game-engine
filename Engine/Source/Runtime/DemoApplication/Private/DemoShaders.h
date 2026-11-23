#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Renderer/SceneRenderer.h"

namespace LE
{

String GetDemoExecutableDirectory() noexcept;
String JoinDemoPath(StringView Directory, StringView Name) noexcept;

// Keep shader words aligned and alive for the RHI descriptors that borrow them.
class FDemoShaderBundle final
{
public:
    bool Load(StringView ExecutableDirectory) noexcept;
    FSceneRenderShaders GetShaders() const noexcept;

private:
    Array<uint32> BasePassVertex;
    Array<uint32> BasePassPixel;
    Array<uint32> CompositeVertex;
    Array<uint32> CompositePixel;
    Array<uint32> ShadowDepthVertex;
};

} // namespace LE
