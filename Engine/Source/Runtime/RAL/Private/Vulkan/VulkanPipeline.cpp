#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

namespace LE
{

static VkFormat ToVkFormat(EPixelFormat Format)
{
    switch (Format)
    {
    case EPixelFormat::R8_UNORM:         return VK_FORMAT_R8_UNORM;
    case EPixelFormat::R8_SNORM:         return VK_FORMAT_R8_SNORM;
    case EPixelFormat::R8_UINT:          return VK_FORMAT_R8_UINT;
    case EPixelFormat::R8_SINT:          return VK_FORMAT_R8_SINT;
    case EPixelFormat::R32_UINT:         return VK_FORMAT_R32_UINT;
    case EPixelFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case EPixelFormat::R8G8B8A8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
    case EPixelFormat::R8G8B8A8_UINT:  return VK_FORMAT_R8G8B8A8_UINT;
    case EPixelFormat::R8G8B8A8_SINT:  return VK_FORMAT_R8G8B8A8_SINT;
    case EPixelFormat::R8G8B8A8_SRGB:  return VK_FORMAT_R8G8B8A8_SRGB;
    case EPixelFormat::B8G8R8A8_SRGB:  return VK_FORMAT_B8G8R8A8_SRGB;
    case EPixelFormat::R16G16_FLOAT:      return VK_FORMAT_R16G16_SFLOAT;
    case EPixelFormat::R16G16B16A16_FLOAT:return VK_FORMAT_R16G16B16A16_SFLOAT;
    case EPixelFormat::R32_FLOAT:         return VK_FORMAT_R32_SFLOAT;
    case EPixelFormat::R32G32_FLOAT:      return VK_FORMAT_R32G32_SFLOAT;
    case EPixelFormat::R32G32B32_FLOAT:   return VK_FORMAT_R32G32B32_SFLOAT;
    case EPixelFormat::R32G32B32A32_FLOAT:return VK_FORMAT_R32G32B32A32_SFLOAT;
    case EPixelFormat::D32_FLOAT:      return VK_FORMAT_D32_SFLOAT;
    case EPixelFormat::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

static VkCompareOp ToVkCompareOp(ECompareFunction Func)
{
    switch (Func)
    {
    case ECompareFunction::Never:        return VK_COMPARE_OP_NEVER;
    case ECompareFunction::Less:         return VK_COMPARE_OP_LESS;
    case ECompareFunction::Equal:        return VK_COMPARE_OP_EQUAL;
    case ECompareFunction::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case ECompareFunction::Greater:      return VK_COMPARE_OP_GREATER;
    case ECompareFunction::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
    case ECompareFunction::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case ECompareFunction::Always:       return VK_COMPARE_OP_ALWAYS;
    default: return VK_COMPARE_OP_LESS_OR_EQUAL;
    }
}

static VkCullModeFlags ToVkCullMode(ECullMode Mode)
{
    switch (Mode)
    {
    case ECullMode::None:  return VK_CULL_MODE_NONE;
    case ECullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case ECullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    default: return VK_CULL_MODE_BACK_BIT;
    }
}

static VkPolygonMode ToVkPolygonMode(EFillMode Mode)
{
    return Mode == EFillMode::Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
}

FVulkanRALPipeline_Graphics::FVulkanRALPipeline_Graphics(FVulkanRALDevice* InDevice, const FRALPipelineDesc_Graphics& InDesc)
    : TVulkanResourceBase<FRALPipelineDesc_Graphics>(InDevice, InDesc)
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Pipeline creation failed: invalid device.");
        this->Pipeline = VK_NULL_HANDLE;
        this->PipelineLayout = VK_NULL_HANDLE;
        return;
    }

    LE::Array<VkDescriptorSetLayout> SetLayouts;
    SetLayouts.Reserve(Desc.BindGroupLayouts.Size());
    for (FRALBindGroupLayout* SetLayout : Desc.BindGroupLayouts)
    {
        if (SetLayout == nullptr)
        {
            LE_LOG(LogRAL, Error, "Pipeline creation failed: bind group layout is null.");
            this->Pipeline = VK_NULL_HANDLE;
            this->PipelineLayout = VK_NULL_HANDLE;
            return;
        }
        FVulkanRALBindGroupLayout* VkLayout = static_cast<FVulkanRALBindGroupLayout*>(SetLayout);
        SetLayouts.PushBack(VkLayout->Handle);
    }

    VkPipelineLayoutCreateInfo LayoutInfo{};
    {
        LayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        LayoutInfo.setLayoutCount = static_cast<uint32>(SetLayouts.Size());
        LayoutInfo.pSetLayouts = SetLayouts.Data();
    }
    VkResult Result = vkCreatePipelineLayout(this->Device->VkContext.LogicalDevice, &LayoutInfo, nullptr, &this->PipelineLayout);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreatePipelineLayout failed. VkResult={}", static_cast<int32>(Result));
        this->PipelineLayout = VK_NULL_HANDLE;
        return;
    }

    if (!this->Device->VkContext.bDynamicRenderingEnabled)
    {
        LE_LOG(LogRAL, Error, "Pipeline creation failed: dynamic rendering is unavailable.");
        return;
    }

    // Shader stages
    LE::Array<VkPipelineShaderStageCreateInfo> ShaderStages;
    ShaderStages.Reserve(2); // TODO: kodak vs and ps

    if (this->Desc.VertexShader)
    {
        FVulkanRALShader* VS = static_cast<FVulkanRALShader*>(this->Desc.VertexShader);
        if (VS->Module == VK_NULL_HANDLE)
        {
            LE_LOG(LogRAL, Error, "Pipeline creation failed: vertex shader module is null.");
            return;
        }
        VkPipelineShaderStageCreateInfo ShaderStage{};
        {
            ShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            ShaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
            ShaderStage.module = VS->Module;
            ShaderStage.pName = VS->GetEntryPoint().GetData();
            ShaderStages.EmplaceBack(ShaderStage);
        }
    }
    if (this->Desc.PixelShader)
    {
        FVulkanRALShader* PS = static_cast<FVulkanRALShader*>(this->Desc.PixelShader);
        if (PS->Module == VK_NULL_HANDLE)
        {
            LE_LOG(LogRAL, Error, "Pipeline creation failed: pixel shader module is null.");
            return;
        }
        VkPipelineShaderStageCreateInfo ShaderStage{};
        {
            ShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            ShaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            ShaderStage.module = PS->Module;
            ShaderStage.pName = PS->GetEntryPoint().GetData();
            ShaderStages.EmplaceBack(ShaderStage);
        }
    }

    // 将 RAL 顶点输入转换为 Vulkan
    LE::Array<VkVertexInputBindingDescription> VkBindings;
    for (const auto& Binding : Desc.VertexBindings)
    {
        VkVertexInputBindingDescription VkBinding{};
        VkBinding.binding = Binding.Binding;
        VkBinding.stride = Binding.Stride;
        VkBinding.inputRate = Binding.bPerInstance ?
            VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        VkBindings.PushBack(VkBinding);
    }

    LE::Array<VkVertexInputAttributeDescription> VkAttributes;
    for (const auto& Attr : Desc.VertexAttributes)
    {
        VkVertexInputAttributeDescription VkAttr{};
        VkAttr.location = Attr.Location;
        VkAttr.binding = Attr.Binding;
        VkAttr.format = ToVkFormat(Attr.Format);
        if (VkAttr.format == VK_FORMAT_UNDEFINED)
        {
            LE_LOG(LogRAL, Error, "Pipeline creation failed: invalid vertex attribute format at location {}.", Attr.Location);
            return;
        }
        VkAttr.offset = Attr.Offset;
        VkAttributes.PushBack(VkAttr);
    }

    VkPipelineVertexInputStateCreateInfo VertexInput{};
    {
        VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VertexInput.vertexBindingDescriptionCount = static_cast<uint32>(VkBindings.Size());
        VertexInput.pVertexBindingDescriptions = VkBindings.Data();
        VertexInput.vertexAttributeDescriptionCount = static_cast<uint32>(VkAttributes.Size());
        VertexInput.pVertexAttributeDescriptions = VkAttributes.Data();
    }

    VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
    {
        InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    VkPipelineViewportStateCreateInfo ViewportState{};
    {
        ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        ViewportState.viewportCount = 1;
        ViewportState.scissorCount = 1;
    }

    VkPipelineRasterizationStateCreateInfo Rasterizer{};
    {
        Rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        Rasterizer.polygonMode = ToVkPolygonMode(this->Desc.RasterizerState.FillMode);
        Rasterizer.cullMode = ToVkCullMode(this->Desc.RasterizerState.CullMode);
        Rasterizer.frontFace = this->Desc.RasterizerState.bFrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
        Rasterizer.lineWidth = 1.f; // TODO: kodak
    }

    VkPipelineMultisampleStateCreateInfo Multisampling{};
    {
        Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // TODO: kodak
    }

    VkPipelineDepthStencilStateCreateInfo DepthStencil{};
    {
        DepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        if (this->Desc.DepthStencilFormat != EPixelFormat::Unknown)
        {
            DepthStencil.depthTestEnable = this->Desc.DepthStencilState.bDepthTestEnable ? VK_TRUE : VK_FALSE;
            DepthStencil.depthWriteEnable = this->Desc.DepthStencilState.bDepthWriteEnable ? VK_TRUE : VK_FALSE;
            DepthStencil.depthCompareOp = ToVkCompareOp(this->Desc.DepthStencilState.DepthFunc);
        }
        else
        {
            DepthStencil.depthTestEnable = VK_FALSE;
            DepthStencil.depthWriteEnable = VK_FALSE;
        }
    }

    LE::Array<VkPipelineColorBlendAttachmentState> ColorBlendAttachments;
    ColorBlendAttachments.Resize(this->Desc.RenderTargetCount);
    for (uint32 i = 0; i < this->Desc.RenderTargetCount; ++i)
    {
        const bool bEnableBlendState = this->Desc.BlendState.bEnable ? VK_TRUE : VK_FALSE;
        VkPipelineColorBlendAttachmentState BlendAttachment{};
        {
            BlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // TODO: kodak
            BlendAttachment.blendEnable = bEnableBlendState;
        }
        ColorBlendAttachments[i] = BlendAttachment;
    }

    VkPipelineColorBlendStateCreateInfo ColorBlendInfo{};
    {
        ColorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        ColorBlendInfo.attachmentCount = this->Desc.RenderTargetCount;
        ColorBlendInfo.pAttachments = ColorBlendAttachments.Data();
    }

    VkDynamicState Dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicState{};
    {
        DynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        DynamicState.dynamicStateCount = 2;
        DynamicState.pDynamicStates = Dynamics;
    }

    LE::Array<VkFormat> ColorAttachmentFormats;
    ColorAttachmentFormats.Reserve(this->Desc.RenderTargetCount);
    for (uint32 i = 0; i < this->Desc.RenderTargetCount; ++i)
    {
        const VkFormat Format = ToVkFormat(this->Desc.RenderTargetFormats[i]);
        if (Format == VK_FORMAT_UNDEFINED)
        {
            LE_LOG(LogRAL, Error, "Pipeline creation failed: invalid render target format at index {}.", i);
            return;
        }
        ColorAttachmentFormats.PushBack(Format);
    }

    const VkFormat DepthStencilFormat = ToVkFormat(this->Desc.DepthStencilFormat);
    if (this->Desc.DepthStencilFormat != EPixelFormat::Unknown &&
        this->Desc.DepthStencilFormat != EPixelFormat::D32_FLOAT &&
        this->Desc.DepthStencilFormat != EPixelFormat::D24_UNORM_S8_UINT)
    {
        LE_LOG(LogRAL, Error, "Pipeline creation failed: invalid depth/stencil format={}.", static_cast<uint32>(this->Desc.DepthStencilFormat));
        return;
    }

    VkPipelineRenderingCreateInfo RenderingInfo{};
    {
        RenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        RenderingInfo.colorAttachmentCount = static_cast<uint32>(ColorAttachmentFormats.Size());
        RenderingInfo.pColorAttachmentFormats = ColorAttachmentFormats.Data();
        RenderingInfo.depthAttachmentFormat = DepthStencilFormat;
        RenderingInfo.stencilAttachmentFormat = this->Desc.DepthStencilFormat == EPixelFormat::D24_UNORM_S8_UINT
            ? DepthStencilFormat
            : VK_FORMAT_UNDEFINED;
    }

    VkGraphicsPipelineCreateInfo PipelineInfo{};
    {
        PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        PipelineInfo.pNext = &RenderingInfo;
        PipelineInfo.stageCount = static_cast<uint32>(ShaderStages.Size());

        PipelineInfo.pStages = ShaderStages.Data();
        PipelineInfo.pVertexInputState = &VertexInput;
        PipelineInfo.pInputAssemblyState = &InputAssembly;
        PipelineInfo.pViewportState = &ViewportState;
        PipelineInfo.pRasterizationState = &Rasterizer;
        PipelineInfo.pMultisampleState = &Multisampling;
        PipelineInfo.pDepthStencilState = &DepthStencil;
        PipelineInfo.pColorBlendState = &ColorBlendInfo;
        PipelineInfo.pDynamicState = &DynamicState;

        PipelineInfo.layout = this->PipelineLayout;
        PipelineInfo.renderPass = VK_NULL_HANDLE;
        PipelineInfo.subpass = 0;
    }
    Result = vkCreateGraphicsPipelines(this->Device->VkContext.LogicalDevice, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &this->Pipeline);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateGraphicsPipelines failed. VkResult={}", static_cast<int32>(Result));
        this->Pipeline = VK_NULL_HANDLE;
        return;
    }

    LE_LOG(LogRAL, Info, "Vulkan graphics pipeline created. RenderTargets={}, HasDepth={}",
        this->Desc.RenderTargetCount, this->Desc.DepthStencilFormat != EPixelFormat::Unknown);
}

FVulkanRALPipeline_Graphics::~FVulkanRALPipeline_Graphics()
{
    if (this->Device == nullptr || this->Device->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        this->Pipeline = VK_NULL_HANDLE;
        this->PipelineLayout = VK_NULL_HANDLE;
        return;
    }

    if (this->Pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device->VkContext.LogicalDevice, this->Pipeline, nullptr);
        this->Pipeline = VK_NULL_HANDLE;
    }
    if (this->PipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Device->VkContext.LogicalDevice, this->PipelineLayout, nullptr);
        this->PipelineLayout = VK_NULL_HANDLE;
    }
}

} // namespace LE
