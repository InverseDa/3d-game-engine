#include "Authoring/RFGTemplateInstancer.h"

#include "Core/RFGInstance.h"

namespace LE
{

namespace
{
constexpr uint64 GFNVOffsetBasis = 1469598103934665603ull;
constexpr uint64 GFNVPrime = 1099511628211ull;

void HashBytes(uint64& Hash, const void* Data, size_t Size)
{
    const uint8* Bytes = static_cast<const uint8*>(Data);
    for (size_t Index = 0; Index < Size; ++Index)
    {
        Hash ^= static_cast<uint64>(Bytes[Index]);
        Hash *= GFNVPrime;
    }
}

template<typename T>
void HashValue(uint64& Hash, const T& Value)
{
    HashBytes(Hash, &Value, sizeof(T));
}

void HashString(uint64& Hash, const LE::String& Value)
{
    HashBytes(Hash, Value.GetData(), static_cast<size_t>(Value.Length()));
}
}

FRFGTemplateInstantiateResult FRFGTemplateInstancer::Instantiate(
    const FRFGTemplate& Template,
    const FRFGParameterStore& /*ParameterStore*/,
    FRFGInstance& /*Instance*/,
    const FRFGTemplateInstantiateOptions& /*Options*/) const
{
    FRFGTemplateInstantiateResult Result;

    uint64 Hash = GFNVOffsetBasis;
    for (const FRFGTemplateResource& Resource : Template.GetResources())
    {
        HashValue(Hash, Resource.ResourceId);
        HashString(Hash, Resource.Name);
        HashValue(Hash, static_cast<uint32>(Resource.Desc.Type));
        HashValue(Hash, static_cast<uint32>(Resource.Flags));
    }

    for (const FRFGTemplateNode& Node : Template.GetNodes())
    {
        HashValue(Hash, Node.NodeId);
        HashString(Hash, Node.Name);
        HashString(Hash, Node.PassTypeName);
        HashValue(Hash, static_cast<uint32>(Node.Queue));
        HashValue(Hash, static_cast<uint32>(Node.Flags));

        for (uint32 ResourceId : Node.ReadResources)
        {
            HashValue(Hash, ResourceId);
        }

        for (uint32 ResourceId : Node.WriteResources)
        {
            HashValue(Hash, ResourceId);
        }
    }

    for (const FRFGTemplateEdge& Edge : Template.GetEdges())
    {
        HashValue(Hash, Edge.SourceNodeId);
        HashValue(Hash, Edge.TargetNodeId);
    }

    Result.Signature.Value = Hash;
    Result.bSucceeded = !Template.GetNodes().IsEmpty() || !Template.GetResources().IsEmpty();
    if (!Result.bSucceeded)
    {
        FRFGValidationIssue Issue;
        Issue.Severity = ERFGValidationSeverity::Warning;
        Issue.Message = "Template is empty.";
        Result.Validation.AddIssue(Issue);
    }

    return Result;
}

} // namespace LE
