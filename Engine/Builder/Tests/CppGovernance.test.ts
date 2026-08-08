import assert from "node:assert/strict";
import test from "node:test";
import { scanCppSource } from "../Source/Governance/CppGovernance.ts";

const scan = (file: string, source: string) => scanCppSource(file, source);

test("comments, ordinary strings, raw strings, and anonymous namespaces are safe", () => {
    const diagnostics = scan("Engine/Source/Runtime/Demo/Private/Safe.cpp", String.raw`
// std::vector<int> Commented;
namespace LE {
const char* Text = "namespace Limitless { glm::vec3 V; }";
const char* Raw = R"tag(std::shared_ptr<int> and using namespace glm)tag";
namespace { struct FLocal {}; }
}
`);
    assert.deepEqual(diagnostics, []);
});

test("each banned owner is rejected with a located symbol", () => {
    for (const owner of ["array", "vector", "deque", "list", "map", "set", "unordered_map", "unordered_set", "basic_string", "string", "unique_ptr", "shared_ptr", "weak_ptr", "optional", "function", "any", "variant", "fstream"]) {
        const diagnostics = scan("Engine/Source/Runtime/Demo/Private/Owner.cpp", `namespace LE { void Bad${owner}() { std::${owner}<int> Value; } }`);
        assert.equal(diagnostics.some((value) => value.rule === "banned-std-owner" && value.fragment === `std::${owner}`), true, owner);
    }
});

test("scope tracking clears declarations at every semicolon", () => {
    const diagnostics = scan("Engine/Source/Runtime/Demo/Private/Scope.cpp", "namespace LE { class Forward; void F() { std::vector<int> Bad; } }");
    const owner = diagnostics.find((value) => value.rule === "banned-std-owner");
    assert.equal(owner?.symbol, "F");
});

test("GLM include and use are rejected but literals are ignored", () => {
    const diagnostics = scan("Engine/Source/Runtime/Demo/Public/Glm.h", `
#include <glm/vec3.hpp>
namespace LE { void Bad() { glm::vec3 Value; const char* Text = "glm::vec4"; } }
`);
    assert.equal(diagnostics.filter((value) => value.rule === "banned-glm").length, 2);
});

test("GLM preprocessor configuration is tokenized without comment or literal false positives", () => {
    const diagnostics = scan("Engine/Source/Runtime/Demo/Private/Glm.cpp", `
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#if LE_USE_GLM
#endif
#define SAFE_TEXT "glm"
#define SAFE_VALUE 1 // GLM_FORCE_LEFT_HANDED
namespace LE { void Good() {} }
`);
    assert.equal(diagnostics.filter((value) => value.rule === "banned-glm").length, 2);
    assert.equal(diagnostics[0]?.line, 2);
});

test("global declarations, aliases, Limitless and public using namespace are rejected", () => {
    const diagnostics = scan("Engine/Source/Runtime/Demo/Public/Bad.h", `
struct FGlobal {};
class DEMO_API FExportedGlobal {};
using FGlobalAlias = int;
namespace Limitless { class FOld {}; }
namespace LE { using namespace Math; class FGood {}; }
`);
    assert.equal(diagnostics.some((value) => value.rule === "global-engine-declaration" && value.symbol === "FGlobal"), true);
    assert.equal(diagnostics.some((value) => value.rule === "global-engine-declaration" && value.symbol === "FExportedGlobal"), true);
    assert.equal(diagnostics.some((value) => value.rule === "global-engine-alias" && value.symbol === "FGlobalAlias"), true);
    assert.equal(diagnostics.some((value) => value.rule === "namespace-limitless"), true);
    assert.equal(diagnostics.some((value) => value.rule === "public-using-namespace"), true);
});

test("approved namespace scopes pass and an unapproved shallow namespace fails", () => {
    for (const namespace of ["LE", "LE::Detail", "LE::Math", "LE::RAL", "LE::RAL::Vulkan", "LE::RendererDemoPasses", "LE::Demo::ShaderRuntimeCompiler"]) {
        assert.deepEqual(scan("Engine/Source/Runtime/Demo/Private/Scope.cpp", `namespace ${namespace} { struct FValue {}; }`), [], namespace);
    }
    assert.equal(scan("Engine/Source/Runtime/Demo/Private/Scope.cpp", "namespace LE::Deep { struct FValue {}; }").some((value) => value.rule === "namespace-scope"), true);

    const logHeader = "Engine/Source/Runtime/Core/Public/Logger/Log.h";
    assert.deepEqual(scan(logHeader, "namespace spdlog { class logger; } namespace LE { class FLog; }"), []);
    assert.equal(scan(logHeader, "namespace spdlog { class engine_type; }").some((value) => value.rule === "namespace-scope"), true);
    assert.equal(scan("Engine/Source/Runtime/Core/Public/Logger/Other.h", "namespace spdlog { class logger; }").some((value) => value.rule === "namespace-scope"), true);
});

test("allowlist is exact to file and symbol", () => {
    const logFile = "Engine/Source/Runtime/Core/Private/Logger/Log.cpp";
    assert.deepEqual(scan(logFile, "namespace LE { void Log::Init() { std::shared_ptr<int> Value; } }"), []);
    assert.equal(scan(logFile, "namespace LE { void NotInit() { std::shared_ptr<int> Value; } }").some((value) => value.rule === "banned-std-owner"), true);
    assert.equal(scan("Engine/Source/Runtime/Core/Private/Logger/Other.cpp", "namespace LE { void Log::Init() { std::shared_ptr<int> Value; } }").some((value) => value.rule === "banned-std-owner"), true);

    const shaderCompiler = "Engine/Source/Runtime/DemoApplication/Private/ShaderRuntimeCompiler.h";
    assert.deepEqual(scan(shaderCompiler, "namespace LE::Demo::ShaderRuntimeCompiler { void ReadBinaryFile() { std::ifstream File; } }"), []);
    assert.equal(scan(shaderCompiler, "namespace LE::Demo::ShaderRuntimeCompiler { void KeepFileOpen() { std::ifstream File; } }").some((value) => value.rule === "banned-std-owner"), true);

    assert.deepEqual(scan(shaderCompiler, "namespace LE::Demo::ShaderRuntimeCompiler { void CompileHlslToSpirv() { std::filesystem::path P; std::error_code E; P.string(); } }"), []);
    assert.equal(scan(shaderCompiler, "namespace LE::Demo::ShaderRuntimeCompiler { void Other() { std::filesystem::path P; std::error_code E; P.string(); } }").filter((value) => value.rule === "banned-std-owner").length, 3);
});

test("the only approved global entry points remain exact", () => {
    assert.deepEqual(scan("Engine/Source/Runtime/Launch/Private/Launch.cpp", "int main() { return 0; }"), []);
    assert.equal(scan("Engine/Source/Runtime/Launch/Private/Launch.cpp", "int AnotherMain() { return 0; }").some((value) => value.rule === "global-engine-declaration"), true);
});
