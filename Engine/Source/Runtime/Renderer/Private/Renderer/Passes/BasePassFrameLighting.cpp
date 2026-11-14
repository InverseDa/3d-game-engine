#include "Renderer/BasePass.h"

#include "RHI/RHIBindGroup.h"
#include "RHI/RHIBuffer.h"
#include "RHI/RHIDevice.h"

#include <cmath>
#include <cstring>

namespace LE
{

FBasePassFrameLighting::~FBasePassFrameLighting() noexcept
{
    Shutdown();
}

bool FBasePassFrameLighting::Initialize(FRHIDevice& Device) noexcept
{
    if (Layout != nullptr)
    {
        return false;
    }
    FRHIBindGroupLayoutDesc LayoutDesc;
    LayoutDesc.Name = "BasePassLightLayout";
    LayoutDesc.SetIndex = SetIndex;
    LayoutDesc.Bindings.PushBack({BindingIndex, 1, ERHIBindGroupItemType::UniformBuffer, EShaderStage::Pixel});
    Layout = Device.CreateBindGroupLayout(LayoutDesc);
    if (Layout == nullptr)
    {
        return false;
    }

    FRHIBufferDesc BufferDesc;
    BufferDesc.Name = "BasePassSlotLightBuffer";
    BufferDesc.Size = sizeof(FBasePassLightConstants);
    BufferDesc.Usage = static_cast<uint32>(EResourceUsage::Upload);
    BufferDesc.UsageFlag = static_cast<uint32>(EBufferUsageFlags::UniformBuffer);
    for (FBasePassLightBinding& Slot : Slots)
    {
        Slot.Buffer = Device.CreateBuffer(BufferDesc);
        if (Slot.Buffer == nullptr)
        {
            Shutdown();
            return false;
        }
        FRHIBindGroupDesc GroupDesc;
        GroupDesc.Name = "BasePassSlotLightBindings";
        GroupDesc.Layout = Layout;
        FRHIBindGroupItem& Item = GroupDesc.Items.EmplaceBack();
        Item.Binding = BindingIndex;
        Item.Buffer = Slot.Buffer;
        Item.Range = BufferDesc.Size;
        Slot.BindGroup = Device.CreateBindGroup(GroupDesc);
        if (Slot.BindGroup == nullptr)
        {
            Shutdown();
            return false;
        }
    }
    return true;
}

void FBasePassFrameLighting::Shutdown() noexcept
{
    for (FBasePassLightBinding& Slot : Slots)
    {
        RHI::DestroyResource(Slot.BindGroup);
        RHI::DestroyResource(Slot.Buffer);
        Slot = {};
    }
    RHI::DestroyResource(Layout);
    Layout = nullptr;
}

bool FBasePassFrameLighting::WriteSlot(const uint32 SlotIndex, const Span<const FRenderLightProxy> Lights,
    FBasePassLightBinding& OutBinding) noexcept
{
    if (Layout == nullptr || SlotIndex >= FRenderFrameScheduler::FrameSlotCount || Lights.Size() > 1)
    {
        return false;
    }
    FBasePassLightConstants Constants{};
    if (!Lights.IsEmpty())
    {
        const FRenderLightProxy& Light = Lights[0];
        const float LengthSquared = Light.DirectionToLight.LengthSquared();
        if (!Light.PrimitiveId.IsValid() || !std::isfinite(LengthSquared) ||
            std::abs(LengthSquared - 1.0f) > 1e-4f)
        {
            return false;
        }
        for (const float Component : Light.Radiance.Values)
        {
            if (!std::isfinite(Component) || Component < 0.0f || Component > 16.0f)
            {
                return false;
            }
        }
        Constants.DirectionAndEnabled = Math::Vector4f(
            Light.DirectionToLight[0], Light.DirectionToLight[1], Light.DirectionToLight[2], 1.0f);
        Constants.Radiance = Math::Vector4f(Light.Radiance[0], Light.Radiance[1], Light.Radiance[2], 0.0f);
    }

    const FBasePassLightBinding& Slot = Slots[SlotIndex];
    void* const Mapping = Slot.Buffer->Map(0, sizeof(Constants));
    if (Mapping == nullptr)
    {
        return false;
    }
    std::memcpy(Mapping, &Constants, sizeof(Constants));
    Slot.Buffer->Unmap();
    OutBinding = Slot;
    return true;
}

} // namespace LE
