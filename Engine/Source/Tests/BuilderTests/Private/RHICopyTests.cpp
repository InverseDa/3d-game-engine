#include "RHIMinimal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace LE
{
namespace
{
int Expect(bool Condition, const char* Description)
{
    if (Condition)
    {
        return 0;
    }
    std::fprintf(stderr, "FAILED: RHI upload: %s\n", Description);
    return 1;
}

FRHITextureDesc MakeTextureDesc()
{
    FRHITextureDesc Desc;
    Desc.Width = 2;
    Desc.Height = 2;
    Desc.Format = EPixelFormat::R8G8B8A8_SRGB;
    Desc.bIsShaderResource = true;
    Desc.bIsTransferDestination = true;
    return Desc;
}

FRHIBufferDesc MakeBufferDesc()
{
    FRHIBufferDesc Desc;
    Desc.Size = 16;
    Desc.Usage = static_cast<uint32>(EResourceUsage::Upload);
    Desc.UsageFlag = static_cast<uint32>(EBufferUsageFlags::TransferSrc);
    return Desc;
}

int RunCopyDescriptorTests()
{
    const FRHIBufferDesc Buffer = MakeBufferDesc();
    const FRHITextureDesc Texture = MakeTextureDesc();
    int Failures = Expect(IsBufferToTexture2DCopyValid(Buffer, Texture), "whole tightly packed RGBA8 copy");
    Failures += Expect(!IsBufferToTexture2DCopyValid(Buffer, Texture, 1), "offset must be texel aligned");
    Failures += Expect(!IsBufferToTexture2DCopyValid(Buffer, Texture, 4), "remaining bytes must fit extent");
    Failures += Expect(!IsBufferToTexture2DCopyValid(Buffer, Texture, 20), "offset cannot exceed source size");
    FRHIBufferDesc NoTransfer = Buffer;
    NoTransfer.UsageFlag = static_cast<uint32>(EBufferUsageFlags::VertexBuffer);
    Failures += Expect(!IsBufferToTexture2DCopyValid(NoTransfer, Texture), "source requires TransferSrc");
    FRHITextureDesc Bad = Texture;
    Bad.bIsTransferDestination = false;
    Failures += Expect(!IsBufferToTexture2DCopyValid(Buffer, Bad), "destination requires TransferDst");
    Bad = Texture;
    Bad.bIsShaderResource = false;
    Failures += Expect(!IsBufferToTexture2DCopyValid(Buffer, Bad), "destination requires sampled usage");
    Bad = Texture;
    Bad.Format = EPixelFormat::B8G8R8A8_SRGB;
    Failures += Expect(!IsBufferToTexture2DCopyValid(Buffer, Bad), "reject other channel layouts");
    Bad = Texture;
    Bad.MipLevels = 2;
    Failures += Expect(!IsBufferToTexture2DCopyValid(Buffer, Bad), "reject additional mips");
    Bad = Texture;
    Bad.ArrayLayers = 2;
    Failures += Expect(!IsBufferToTexture2DCopyValid(Buffer, Bad), "reject array layers");
    Bad = Texture;
    Bad.Depth = 2;
    Failures += Expect(!IsBufferToTexture2DCopyValid(Buffer, Bad), "reject a volume");
    Bad = Texture;
    Bad.Width = 0;
    Failures += Expect(!IsBufferToTexture2DCopyValid(Buffer, Bad), "reject an empty extent");
    Bad = Texture;
    Bad.Width = UINT32_MAX;
    Bad.Height = UINT32_MAX;
    FRHIBufferDesc Huge = Buffer;
    Huge.Size = UINT64_MAX;
    return Failures + Expect(!IsBufferToTexture2DCopyValid(Huge, Bad),
        "texel byte check does not wrap");
}

struct FUploadFixture
{
    ~FUploadFixture()
    {
        // A failed completion query alone cannot authorize destruction.
        if (bPending)
        {
            const auto Idle = Device->GetGraphicsQueue()->WaitIdleChecked();
            if (Idle != ERHIFenceWaitResult::Success && Idle != ERHIFenceWaitResult::DeviceLost)
            {
                std::fprintf(stderr, "RHI upload test could not retire a submitted copy.\n");
                return;
            }
        }
        RHI::DestroyResource(Fence);
        RHI::DestroyResource(Commands);
        RHI::DestroyResource(Allocator);
        RHI::DestroyResource(AttachmentView);
        RHI::DestroyResource(Attachment);
        RHI::DestroyResource(Texture);
        RHI::DestroyResource(Buffer);
        RHI::DestroyResource(ForeignBuffer);
        RHI::DestroyResource(ForeignDevice);
        RHI::DestroyResource(Device);
    }

    FRHIDevice* Device = nullptr;
    FRHIBuffer* Buffer = nullptr;
    FRHITexture* Texture = nullptr;
    FRHICommandAllocator* Allocator = nullptr;
    FRHICommandList* Commands = nullptr;
    FRHIFence* Fence = nullptr;
    FRHITexture* Attachment = nullptr;
    FRHITextureView* AttachmentView = nullptr;
    FRHIDevice* ForeignDevice = nullptr;
    FRHIBuffer* ForeignBuffer = nullptr;
    bool bPending = false;
};

int RunVulkanCopyTests()
{
    FUploadFixture Fix;
    Fix.Device = RHI::CreateDevice();
    if (Fix.Device == nullptr || Fix.Device->GetGraphicsQueue() == nullptr)
    {
        return Expect(false, "create optional Vulkan upload test device");
    }
    Fix.Buffer = Fix.Device->CreateBuffer(MakeBufferDesc());
    Fix.Texture = Fix.Device->CreateTexture(MakeTextureDesc());
    Fix.Allocator = Fix.Device->CreateCommandAllocator();
    Fix.Commands = Fix.Allocator == nullptr ? nullptr : Fix.Device->CreateCommandList(Fix.Allocator);
    Fix.Fence = Fix.Device->CreateFence();
    auto AttachmentDesc = MakeTextureDesc();
    AttachmentDesc.bIsTransferDestination = false;
    AttachmentDesc.bIsRenderTarget = true;
    Fix.Attachment = Fix.Device->CreateTexture(AttachmentDesc);
    FRHITextureViewDesc ViewDesc;
    ViewDesc.Texture = Fix.Attachment;
    Fix.AttachmentView = Fix.Attachment == nullptr ? nullptr : Fix.Device->CreateTextureView(ViewDesc);
    Fix.ForeignDevice = RHI::CreateDevice();
    Fix.ForeignBuffer = Fix.ForeignDevice == nullptr ? nullptr :
        Fix.ForeignDevice->CreateBuffer(MakeBufferDesc());
    if (Fix.Buffer == nullptr || Fix.Texture == nullptr || Fix.Commands == nullptr || Fix.Fence == nullptr ||
        Fix.AttachmentView == nullptr || Fix.ForeignBuffer == nullptr)
    {
        return Expect(false, "construct required Vulkan upload resources");
    }

    FRHISubmitInfo Submit;
    Submit.CmdList = Fix.Commands;
    Submit.FenceToSignal = Fix.Fence;
    auto* const Queue = Fix.Device->GetGraphicsQueue();
    int Failures = Expect(Fix.Fence->WaitChecked(0) == ERHIFenceWaitResult::Timeout,
        "new unsignaled fence reports timeout");
    Failures += Expect(Queue->Submit(Submit) == ERHIQueueSubmitResult::InvalidArguments,
        "reject non-executable command list");
    Failures += Expect(!Fix.Commands->CopyBufferToTexture2D(Fix.Buffer, Fix.Texture),
        "reject recording outside an active command list");
    if (!Fix.Commands->BeginChecked())
    {
        return Failures + Expect(false, "begin checked recording");
    }
    Failures += Expect(!Fix.Commands->CopyBufferToTexture2D(Fix.Buffer, Fix.Texture),
        "reject copying before destination transition");
    Failures += Expect(!Fix.Commands->TextureUploadBarrier(
        Fix.Texture, ERHIResourceState::CopyDestination, ERHIResourceState::ShaderResource),
        "reject finishing before copying");
    FRHIBarrierBatch Barriers;
    FRHITextureBarrierDesc ColorBarrier;
    ColorBarrier.Texture = Fix.Attachment;
    ColorBarrier.BeforeState = ERHIResourceState::Undefined;
    ColorBarrier.AfterState = ERHIResourceState::RenderTarget;
    Barriers.TextureBarriers.PushBack(ColorBarrier);
    Fix.Commands->ResourceBarriers(Barriers);
    FRHIRenderPassDesc Render;
    Render.ColorAttachmentCount = 1;
    Render.ColorAttachments[0].RenderTarget = Fix.AttachmentView;
    Render.ColorAttachments[0].LoadOp = EAttachmentLoadOp::Clear;
    Render.ColorAttachments[0].StoreOp = EAttachmentStoreOp::Store;
    Fix.Commands->BeginRenderPass(Render);
    Failures += Expect(!Fix.Commands->TextureUploadBarrier(
        Fix.Texture, ERHIResourceState::Undefined, ERHIResourceState::CopyDestination),
        "reject starting upload in dynamic rendering");
    Fix.Commands->EndRenderPass();
    if (!Fix.Commands->TextureUploadBarrier(
        Fix.Texture, ERHIResourceState::Undefined, ERHIResourceState::CopyDestination))
    {
        return Failures + Expect(false, "record first texture transition");
    }
    Failures += Expect(!Fix.Commands->EndChecked(), "reject unfinished upload");
    Failures += Expect(!Fix.Commands->CopyBufferToTexture2D(Fix.ForeignBuffer, Fix.Texture),
        "reject staging buffer from a different device");
    constexpr uint8 Pixels[] = {255, 0, 0, 255, 0, 128, 0, 255, 0, 0, 64, 255, 255, 255, 0, 255};
    void* const Mapped = Fix.Buffer->Map(0, sizeof(Pixels));
    if (Mapped == nullptr)
    {
        return Failures + Expect(false, "map coherent staging memory");
    }
    std::memcpy(Mapped, Pixels, sizeof(Pixels));
    Failures += Expect(!Fix.Commands->CopyBufferToTexture2D(Fix.Buffer, Fix.Texture),
        "reject a staging buffer still mapped by caller");
    Fix.Buffer->Unmap();
    if (!Fix.Commands->CopyBufferToTexture2D(Fix.Buffer, Fix.Texture) ||
        !Fix.Commands->TextureUploadBarrier(
            Fix.Texture, ERHIResourceState::CopyDestination, ERHIResourceState::ShaderResource) ||
        !Fix.Commands->EndChecked() || Queue->Submit(Submit) != ERHIQueueSubmitResult::Success)
    {
        return Failures + Expect(false, "record and submit a valid full-image copy");
    }
    Fix.bPending = true;
    const auto Completion = Fix.Fence->WaitChecked();
    Fix.bPending = Completion != ERHIFenceWaitResult::Success && Completion != ERHIFenceWaitResult::DeviceLost;
    return Failures + Expect(Completion == ERHIFenceWaitResult::Success, "copy completed on Vulkan queue");
}
} // namespace

int RunRHICopyTests()
{
    const char* const Vulkan = std::getenv("LIMITLESS_TEST_VULKAN_UPLOAD");
    const int DescriptorFailures = RunCopyDescriptorTests();
    if (Vulkan == nullptr || std::strcmp(Vulkan, "1") != 0)
    {
        return DescriptorFailures;
    }
    LE_INIT()
    return DescriptorFailures + RunVulkanCopyTests();
}
} // namespace LE
