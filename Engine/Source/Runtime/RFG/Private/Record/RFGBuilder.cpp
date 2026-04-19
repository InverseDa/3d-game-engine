#include "Record/RFGBuilder.h"

#include "RAL/RALBuffer.h"
#include "RAL/RALTexture.h"

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

void HashString(uint64& Hash, const FString& Value)
{
    HashBytes(Hash, Value.GetData(), static_cast<size_t>(Value.Length()));
}

bool QueueWasDefault(ERFGQueueType QueueType)
{
    return QueueType == ERFGQueueType::Graphics;
}
}

FRFGBuilder::FRFGBuilder(const FRFGBuilderCreateInfo& InCreateInfo)
    : PassRegistry(InCreateInfo.PassRegistry)
{
}

FRFGBuilder::~FRFGBuilder() = default;

void FRFGBuilder::SetPassRegistry(IRFGPassRegistry* InPassRegistry)
{
    PassRegistry = InPassRegistry;
}

IRFGPassRegistry* FRFGBuilder::GetPassRegistry() const
{
    return PassRegistry;
}

FRFGResourceHandle FRFGBuilder::CreateTexture(const FString& ResourceName, const FRFGTextureDesc& Desc)
{
    FRFGResourceNode ResourceNode;
    ResourceNode.Name = ResourceName;
    ResourceNode.Desc.Type = ERFGResourceType::Texture;
    ResourceNode.Desc.Texture = Desc;

    const FRFGResourceHandle Handle = RecordedGraph.AddResourceNode(ResourceNode);
    Blackboard.SetResourceHandle(ResourceName, Handle);
    return Handle;
}

FRFGResourceHandle FRFGBuilder::CreateBuffer(const FString& ResourceName, const FRFGBufferDesc& Desc)
{
    FRFGResourceNode ResourceNode;
    ResourceNode.Name = ResourceName;
    ResourceNode.Desc.Type = ERFGResourceType::Buffer;
    ResourceNode.Desc.Buffer = Desc;

    const FRFGResourceHandle Handle = RecordedGraph.AddResourceNode(ResourceNode);
    Blackboard.SetResourceHandle(ResourceName, Handle);
    return Handle;
}

FRFGResourceHandle FRFGBuilder::ImportTexture(const FString& ResourceName, FRALTexture* ExternalTexture)
{
    FRFGResourceNode ResourceNode;
    ResourceNode.Name = ResourceName;
    ResourceNode.Desc.Type = ERFGResourceType::Texture;
    ResourceNode.Flags = ERFGResourceFlags::Imported | ERFGResourceFlags::External;
    ResourceNode.ImportedTexture = ExternalTexture;
    if (ExternalTexture != nullptr)
    {
        const FRALTextureDesc& TextureDesc = ExternalTexture->GetDesc();
        ResourceNode.Desc.Texture.Width = TextureDesc.Width;
        ResourceNode.Desc.Texture.Height = TextureDesc.Height;
        ResourceNode.Desc.Texture.Depth = TextureDesc.Depth;
        ResourceNode.Desc.Texture.MipLevels = TextureDesc.MipLevels;
        ResourceNode.Desc.Texture.ArrayLayers = TextureDesc.ArrayLayers;
        ResourceNode.Desc.Texture.Format = TextureDesc.Format;
        ResourceNode.Desc.Texture.UsageMask =
            (TextureDesc.bIsUAV ? (1u << 0) : 0u) |
            (TextureDesc.bIsRenderTarget ? (1u << 1) : 0u) |
            (TextureDesc.bIsDepthStencil ? (1u << 2) : 0u) |
            (TextureDesc.bIsShaderResource ? (1u << 3) : 0u);
    }

    const FRFGResourceHandle Handle = RecordedGraph.AddResourceNode(ResourceNode);
    Blackboard.SetResourceHandle(ResourceName, Handle);
    return Handle;
}

FRFGResourceHandle FRFGBuilder::ImportBuffer(const FString& ResourceName, FRALBuffer* ExternalBuffer)
{
    FRFGResourceNode ResourceNode;
    ResourceNode.Name = ResourceName;
    ResourceNode.Desc.Type = ERFGResourceType::Buffer;
    ResourceNode.Flags = ERFGResourceFlags::Imported | ERFGResourceFlags::External;
    ResourceNode.ImportedBuffer = ExternalBuffer;
    if (ExternalBuffer != nullptr)
    {
        const FRALBufferDesc& BufferDesc = ExternalBuffer->GetDesc();
        ResourceNode.Desc.Buffer.Size = BufferDesc.Size;
        ResourceNode.Desc.Buffer.Usage = static_cast<EResourceUsage>(BufferDesc.Usage);
        ResourceNode.Desc.Buffer.UsageMask = BufferDesc.UsageFlag;
    }

    const FRFGResourceHandle Handle = RecordedGraph.AddResourceNode(ResourceNode);
    Blackboard.SetResourceHandle(ResourceName, Handle);
    return Handle;
}

FRFGPassHandle FRFGBuilder::AddPass(
    const FString& PassName,
    const FString& PassTypeName,
    const FRFGPassParameterBlock& ParameterBlock,
    ERFGPassFlags Flags,
    ERFGQueueType QueueType,
    const FRFGSourceLocation& SourceLocation)
{
    FRFGPassNode PassNode;
    PassNode.Name = PassName;
    PassNode.PassTypeName = PassTypeName;
    PassNode.Queue = QueueType;
    PassNode.Flags = Flags;
    PassNode.Parameters = ParameterBlock;
    PassNode.SourceLocation = SourceLocation;

    if (PassRegistry != nullptr)
    {
        if (const FRFGRegisteredPassType* RegisteredPassType = PassRegistry->FindPassType(PassTypeName))
        {
            if (QueueWasDefault(QueueType))
            {
                PassNode.Queue = RegisteredPassType->Schema.PreferredQueue;
            }

            PassNode.Flags = PassNode.Flags | RegisteredPassType->Schema.DefaultFlags;
            PassNode.Executor = RegisteredPassType->Executor;
        }
    }

    const FRFGPassHandle Handle = RecordedGraph.AddPassNode(PassNode);
    Blackboard.SetPassHandle(PassName, Handle);
    return Handle;
}

void FRFGBuilder::SetPassCallback(FRFGPassHandle PassHandle, const FRFGPassCallback& PassCallback)
{
    RecordedGraph.GetPassNode(PassHandle).ExecuteCallback = PassCallback;
}

void FRFGBuilder::Read(FRFGPassHandle PassHandle, FRFGResourceHandle ResourceHandle, const FRFGAccessDesc& AccessDesc)
{
    FRFGPassResourceAccess ResourceAccess;
    ResourceAccess.Resource = ResourceHandle;
    ResourceAccess.Access = AccessDesc;
    ResourceAccess.Access.Access = ERFGAccessType::Read;

    RecordedGraph.GetPassNode(PassHandle).ResourceAccesses.push_back(ResourceAccess);
}

void FRFGBuilder::Write(FRFGPassHandle PassHandle, FRFGResourceHandle ResourceHandle, const FRFGAccessDesc& AccessDesc)
{
    FRFGPassResourceAccess ResourceAccess;
    ResourceAccess.Resource = ResourceHandle;
    ResourceAccess.Access = AccessDesc;
    ResourceAccess.Access.Access = ERFGAccessType::Write;

    RecordedGraph.GetPassNode(PassHandle).ResourceAccesses.push_back(ResourceAccess);
}

void FRFGBuilder::AddDependency(FRFGPassHandle BeforePass, FRFGPassHandle AfterPass)
{
    RecordedGraph.GetPassNode(AfterPass).ExplicitDependencies.push_back(BeforePass);
}

void FRFGBuilder::MarkOutput(FRFGResourceHandle ResourceHandle)
{
    RecordedGraph.MarkOutput(ResourceHandle);
}

FRFGGraphSignature FRFGBuilder::BuildSignature() const
{
    uint64 Hash = GFNVOffsetBasis;

    for (const FRFGResourceNode& ResourceNode : RecordedGraph.GetResourceNodes())
    {
        HashValue(Hash, ResourceNode.Handle.Id);
        HashString(Hash, ResourceNode.Name);
        HashValue(Hash, static_cast<uint32>(ResourceNode.Desc.Type));
        HashValue(Hash, static_cast<uint32>(ResourceNode.Flags));

        if (ResourceNode.Desc.Type == ERFGResourceType::Texture)
        {
            HashValue(Hash, ResourceNode.Desc.Texture.Width);
            HashValue(Hash, ResourceNode.Desc.Texture.Height);
            HashValue(Hash, ResourceNode.Desc.Texture.Depth);
            HashValue(Hash, ResourceNode.Desc.Texture.MipLevels);
            HashValue(Hash, ResourceNode.Desc.Texture.ArrayLayers);
            HashValue(Hash, static_cast<uint32>(ResourceNode.Desc.Texture.Format));
            HashValue(Hash, ResourceNode.Desc.Texture.UsageMask);
        }
        else
        {
            HashValue(Hash, ResourceNode.Desc.Buffer.Size);
            HashValue(Hash, ResourceNode.Desc.Buffer.Stride);
            HashValue(Hash, static_cast<uint32>(ResourceNode.Desc.Buffer.Usage));
            HashValue(Hash, ResourceNode.Desc.Buffer.UsageMask);
        }

        const bool bIsOutput = RecordedGraph.IsOutputResource(ResourceNode.Handle);
        HashValue(Hash, bIsOutput);
    }

    for (const FRFGPassHandle PassHandle : RecordedGraph.GetPassOrder())
    {
        const FRFGPassNode& PassNode = RecordedGraph.GetPassNode(PassHandle);
        HashValue(Hash, PassHandle.Id);
        HashString(Hash, PassNode.Name);
        HashString(Hash, PassNode.PassTypeName);
        HashValue(Hash, static_cast<uint32>(PassNode.Queue));
        HashValue(Hash, static_cast<uint32>(PassNode.Flags));
        HashValue(Hash, PassNode.Parameters.DataSize);
        HashValue(Hash, PassNode.Parameters.Revision);

        for (const FRFGPassResourceAccess& ResourceAccess : PassNode.ResourceAccesses)
        {
            HashValue(Hash, ResourceAccess.Resource.Id);
            HashValue(Hash, static_cast<uint32>(ResourceAccess.Access.Access));
            HashValue(Hash, static_cast<uint32>(ResourceAccess.Access.ShaderStage));
            HashValue(Hash, static_cast<uint32>(ResourceAccess.Access.PipelineStage));
            HashValue(Hash, ResourceAccess.Access.BaseMipLevel);
            HashValue(Hash, ResourceAccess.Access.MipCount);
            HashValue(Hash, ResourceAccess.Access.BaseArrayLayer);
            HashValue(Hash, ResourceAccess.Access.LayerCount);
        }

        for (const FRFGPassHandle Dependency : PassNode.ExplicitDependencies)
        {
            HashValue(Hash, Dependency.Id);
        }
    }

    FRFGGraphSignature Signature;
    Signature.Value = Hash;
    return Signature;
}

FRFGRecordedGraph& FRFGBuilder::GetRecordedGraph()
{
    return RecordedGraph;
}

const FRFGRecordedGraph& FRFGBuilder::GetRecordedGraph() const
{
    return RecordedGraph;
}

FRFGBlackboard& FRFGBuilder::GetBlackboard()
{
    return Blackboard;
}

const FRFGBlackboard& FRFGBuilder::GetBlackboard() const
{
    return Blackboard;
}

void FRFGBuilder::Reset()
{
    RecordedGraph.Reset();
    Blackboard.Clear();
}
