#include "Scene/SceneDocument.h"

#include <cstdio>
#include <limits>
#include <utility>

namespace LE
{
namespace
{
using EResult = ESceneDocumentResult;

constexpr char SceneText[] = R"json({
  "entities": [
    {
      "id": "20000000-0000-4000-8000-000000000002",
      "parent": "20000000-0000-4000-8000-000000000001",
      "transform": {"translation": [0.1,-0.25,1.5e1], "rotation": [0,0,0,1], "scale": [1,1,1]},
      "mesh": {
        "mesh": "30000000-0000-4000-8000-000000000001",
        "material": "30000000-0000-4000-8000-000000000002",
        "debugName": "\u4e2d\ud83d\ude42\n\"\\",
        "sortKey": "18446744073709551615"
      }
    },
    {
      "id": "20000000-0000-4000-8000-000000000001",
      "transform": {"translation": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1]},
      "camera": {
        "debugName": "", "verticalFieldOfViewRadians": 0.8, "nearPlane": 0.1,
        "farPlane": 100, "priority": -2147483648, "active": false
      }
    },
    {"id": "20000000-0000-4000-8000-000000000003"}
  ],
  "version": 1
})json";

int Check(const bool Condition, const char* Description)
{
    if (Condition)
    {
        return 0;
    }
    std::fprintf(stderr, "FAILED: scene document: %s\n", Description);
    return 1;
}

bool ReplaceOnce(const StringView Source, const StringView Old, const StringView Replacement, String& Out) noexcept
{
    for (std::size_t Index = 0; Index + Old.Size() <= Source.Size(); ++Index)
    {
        if (Source.Substr(Index, Old.Size()) == Old)
        {
            return Out.TryAssign(Source.Substr(0, Index)) && Out.TryAppend(Replacement) && Out.TryAppend(Source.Substr(Index + Old.Size(), Source.Size()));
        }
    }
    return false;
}

struct FInvalidEdit
{
    StringView Old;
    StringView Replacement;
    EResult Result;
    StringView Field;
};

class FSceneTestAllocator final : public IAllocator
{
public:
    void* Allocate(const std::size_t Size, const std::size_t Alignment) noexcept override { return bFail ? nullptr : GetDefaultAllocator().Allocate(Size, Alignment); }

    void Deallocate(void* Address, const std::size_t Size, const std::size_t Alignment) noexcept override { GetDefaultAllocator().Deallocate(Address, Size, Alignment); }

    bool bFail = false;
};

int TestDirectionalLightDocument()
{
    constexpr char LightScene[] = R"json({
  "entities": [
    {
      "directionalLight": { "active": true, "intensity": 0, "color": [1, 0.5, 0], "direction": [0, 0, -4] },
      "id": "20000000-0000-4000-8000-000000000001"
    },
    {
      "id": "20000000-0000-4000-8000-000000000002",
      "directionalLight": { "direction": [0, 2, 0], "color": [0, 1, 1], "intensity": 16, "active": false }
    }
  ],
  "version": 2
})json";
    FSceneDocument Document;
    FSceneDocumentDiagnostic Diagnostic;
    if (DecodeSceneDocument(LightScene, Document, &Diagnostic) != EResult::Success || Document.Entities.Size() != 2)
    {
        return Check(false, "decode v2 with version last and unordered light fields");
    }

    const auto& Active = Document.Entities[0];
    const auto& Inactive = Document.Entities[1];
    int Failures = Check(Active.bHasDirectionalLight && Active.DirectionalLight.bActive && Active.DirectionalLight.Direction == Math::Vector3f(0, 0, -4) && Active.DirectionalLight.Intensity == 0 && Inactive.bHasDirectionalLight && !Inactive.DirectionalLight.bActive && Inactive.DirectionalLight.Color == Math::Vector3f(0, 1, 1) && Inactive.DirectionalLight.Intensity == 16, "parse both light states and preserve authored magnitude and range boundaries");
    const FInvalidEdit InvalidCases[] = {
        {R"("version": 2)", R"("version": 1)", EResult::UnknownField, "directionalLight"},
        {R"("active": false)", R"("active": true)", EResult::MultipleActiveDirectionalLights, "active"},
        {R"("direction": [0, 0, -4])", R"("direction": [0, 0, 0])", EResult::InvalidValue, "direction"},
        {R"("color": [0, 1, 1])", R"("color": [0, 2, 1])", EResult::InvalidValue, "color"},
        {R"("intensity": 0)", R"("intensity": -1)", EResult::InvalidValue, "intensity"},
        {R"("intensity": 0)", R"("intensity": "wrong type")", EResult::InvalidJson, "intensity"},
        {R"("intensity": 0)", R"("intensity": {"id": "30000000-0000-4000-8000-000000000003", "quoted": "}],\""})", EResult::InvalidJson, "intensity"},
        {R"("intensity": 0, )", "", EResult::MissingField, "intensity"},
    };
    for (const FInvalidEdit& Edit : InvalidCases)
    {
        const FSceneEntityId Expected = Edit.Old == StringView(R"("active": false)") || Edit.Field == StringView("color") ? Inactive.Id : Active.Id;
        String Edited;
        Failures += Check(ReplaceOnce(LightScene, Edit.Old, Edit.Replacement, Edited) && DecodeSceneDocument(Edited.View(), Document, &Diagnostic) == Edit.Result && Diagnostic.Field == Edit.Field && Diagnostic.Entity == Expected && Diagnostic.Line != 0 && Diagnostic.Column != 0, "locate invalid light at its authoring field and entity");
    }
    return Failures;
}

int TestLegacyCanonicalDocument()
{
    constexpr char Legacy[] = "{\n  \"version\": 1,\n  \"entities\": [\n"
                              "    {\n      \"id\": \"20000000-0000-4000-8000-000000000001\"\n    }\n  ]\n}\n";
    FSceneDocument Document;
    String Encoded;
    return Check(DecodeSceneDocument(Legacy, Document) == EResult::Success && EncodeSceneDocument(Document, Encoded) == EResult::Success && Encoded.View() == StringView(Legacy), "write a legacy scene with exactly the canonical v1 bytes");
}
} // namespace

int RunSceneDocumentTests()
{
    FSceneDocument Document;
    FSceneDocumentDiagnostic Diagnostic;
    if (DecodeSceneDocument(SceneText, Document, &Diagnostic) != EResult::Success)
    {
        std::fprintf(stderr, "scene parser returned %s at %zu:%zu\n", DescribeSceneDocumentResult(Diagnostic.Result), Diagnostic.Line, Diagnostic.Column);
        return Check(false, "decode editable scene");
    }
    if (Document.Entities.Size() != 3)
    {
        return Check(false, "read all entities, including empty");
    }
    const FSceneEntityRecord& Parent = Document.Entities[0];
    const FSceneEntityRecord& Child = Document.Entities[1];
    int Failures = Check(Parent.bHasCamera && !Parent.Camera.bActive && Parent.Camera.Priority == (std::numeric_limits<int32>::min)() && Child.Parent == Parent.Id && Child.bHasTransform && Child.Transform.Translation == Math::Vector3f(0.1f, -0.25f, 15.0f) && Child.Mesh.SortKey == (std::numeric_limits<uint64>::max)() && Child.Mesh.DebugName.View() == StringView("\xe4\xb8\xad\xf0\x9f\x99\x82\n\"\\", 10) && !Document.Entities[2].bHasTransform, "sort IDs and preserve authored values and full u64");

    String Encoded;
    FSceneDocument Roundtrip;
    String EncodedAgain;
    Failures += Check(EncodeSceneDocument(Document, Encoded) == EResult::Success && DecodeSceneDocument(Encoded.View(), Roundtrip) == EResult::Success && EncodeSceneDocument(Roundtrip, EncodedAgain) == EResult::Success && Encoded.View() == EncodedAgain.View() && Roundtrip.Entities.Size() == 3 && Roundtrip.Entities[1].Transform == Child.Transform, "encode canonical UTF-8 and round-trip the values exactly");
    std::swap(Document.Entities[0], Document.Entities[2]);
    Failures += Check(EncodeSceneDocument(Document, EncodedAgain) == EResult::Success && Encoded.View() == EncodedAgain.View(), "encode in ID order regardless of input order");
    std::swap(Document.Entities[0], Document.Entities[2]);

    const FInvalidEdit Cases[] = {
        {R"("version": 1)", R"("version": 3)", EResult::UnsupportedVersion, "version"},
        {R"("version": 1)", R"("version": 1, "vers\u0069on": 1)", EResult::DuplicateField, "version"},
        {R"(, "scale": [1,1,1])", "", EResult::MissingField, "scale"},
        {R"("scale": [1,1,1])", R"("scale": [1,1,1], "unknown": false)", EResult::UnknownField, {}},
        {R"("sortKey": "18446744073709551615")", R"("sortKey": "18446744073709551616")", EResult::InvalidValue, "sortKey"},
        {R"("sortKey": "18446744073709551615")", R"("sortKey": 2)", EResult::InvalidJson, "sortKey"},
        {R"("debugName": "\u4e2d\ud83d\ude42\n\"\\")", R"("debugName": "\ud800")", EResult::InvalidJson, "debugName"},
        {R"("debugName": "\u4e2d\ud83d\ude42\n\"\\")", R"("debugName": "\u0000")", EResult::InvalidValue, "debugName"},
        {R"("debugName": "")", "\"debugName\": \"\xc0\xaf\"", EResult::InvalidJson, "debugName"},
        {R"("id": "20000000-0000-4000-8000-000000000003")", R"("id": "20000000-0000-4000-8000-000000000002")", EResult::DuplicateEntityId, "id"},
        {R"("parent": "20000000-0000-4000-8000-000000000001")", R"("parent": "20000000-0000-4000-8000-000000000004")", EResult::InvalidParent, "parent"},
        {R"("parent": "20000000-0000-4000-8000-000000000001")", R"("parent": "20000000-0000-4000-8000-000000000003")", EResult::InvalidParent, "parent"},
        {R"("id": "20000000-0000-4000-8000-000000000001")", R"("id": "20000000-0000-4000-8000-000000000001", "parent": "20000000-0000-4000-8000-000000000002")", EResult::ParentCycle, "parent"},
        {R"("rotation": [0,0,0,1])", R"("rotation": [0,0,0,0])", EResult::InvalidValue, "rotation"},
        {R"("translation": [0.1,-0.25,1.5e1])", R"("translation": [1e1000,0,0])", EResult::InvalidValue, "translation"},
        {R"("nearPlane": 0.1)", R"("nearPlane": 100)", EResult::InvalidValue, "farPlane"},
        {R"("active": false)", R"("active": false,)", EResult::InvalidJson, {}},
    };
    const FSceneEntityId FirstId = Document.Entities[0].Id;
    for (const FInvalidEdit& Edit : Cases)
    {
        String Edited;
        if (!ReplaceOnce(SceneText, Edit.Old, Edit.Replacement, Edited))
        {
            return Failures + Check(false, "construct malformed scene fixture");
        }
        const EResult Result = DecodeSceneDocument(Edited.View(), Document, &Diagnostic);
        if (Result != Edit.Result || Diagnostic.Field != Edit.Field || Diagnostic.Line == 0 || Diagnostic.Column == 0)
        {
            std::fprintf(stderr, "invalid edit [%.*s], got %s at %zu:%zu [%.*s]\n", static_cast<int>(Edit.Old.Size()), Edit.Old.Data(), DescribeSceneDocumentResult(Result), Diagnostic.Line, Diagnostic.Column, static_cast<int>(Diagnostic.Field.Size()), Diagnostic.Field.IsEmpty() ? "" : Diagnostic.Field.Data());
            Failures += Check(false, "reject invalid document with field and source location");
        }
        Failures += Check(Document.Entities.Size() == 3 && Document.Entities[0].Id == FirstId, "failed decode preserves previous output");
    }

    String OversizedName;
    String Replacement("\"debugName\": \"");
    for (std::size_t Index = 0; Index <= MaxSceneNameBytes; ++Index)
    {
        Replacement.Append("x");
    }
    Replacement.Append("\"");
    Failures += Check(ReplaceOnce(SceneText, R"("debugName": "")", Replacement.View(), OversizedName) && DecodeSceneDocument(OversizedName.View(), Document, &Diagnostic) == EResult::LimitExceeded && Diagnostic.Field == StringView("debugName"), "limit decoded name length");

    String LargeScene("{\"version\":1,\"entities\":[");
    constexpr char EmptyEntity[] = R"({"id":"20000000-0000-4000-8000-000000000003"})";
    for (std::size_t Index = 0; Index <= MaxSceneEntities; ++Index)
    {
        if (Index != 0)
        {
            LargeScene.Append(",");
        }
        LargeScene.Append(EmptyEntity);
    }
    LargeScene.Append("]}");
    Failures += Check(DecodeSceneDocument(LargeScene.View(), Document) == EResult::LimitExceeded, "reject entity count above limit before graph validation");

    FSceneTestAllocator Failing;
    FSceneDocument AllocationOutput(Failing);
    Failures += Check(DecodeSceneDocument(SceneText, AllocationOutput) == EResult::Success, "populate transactional allocator fixture");
    String AllocationEncoded(Failing);
    AllocationEncoded.Assign("previous output");
    Failing.bFail = true;
    Failures += Check(DecodeSceneDocument(SceneText, AllocationOutput) == EResult::AllocationFailed && AllocationOutput.Entities.Size() == 3 && EncodeSceneDocument(Document, AllocationEncoded) == EResult::AllocationFailed && AllocationEncoded.View() == StringView("previous output"), "allocator failure preserves both outputs");
    return Failures + TestDirectionalLightDocument() + TestLegacyCanonicalDocument();
}
} // namespace LE
