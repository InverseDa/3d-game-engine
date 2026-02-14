#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

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

static VkRenderPass CreateMinimalRenderPass(FVulkanRALDevice* Device, const FRALPipelineDesc_Graphics& Desc)
{
    std::vector<VkAttachmentDescription> Attachments;
    std::vector<VkAttachmentReference> ColorRefs;

    for (uint32 i = 0; i < Desc.RenderTargetCount; ++i)
    {
        VkAttachmentDescription ColorAttachment{};
        {
            const VkFormat VkRtFormat = ToVkFormat(Desc.RenderTargetFormats[i]);
            if (VkRtFormat == VK_FORMAT_UNDEFINED)
            {
                return VK_NULL_HANDLE;
            }
            ColorAttachment.format = VkRtFormat;
            ColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            ColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            ColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            ColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ColorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        VkAttachmentReference Ref{};
        {
            Ref.attachment = static_cast<uint32>(Attachments.size());
            Ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        Attachments.push_back(ColorAttachment);
        ColorRefs.push_back(Ref);
    }

    VkAttachmentReference DepthRef{};
    bool bHasDepth = Desc.DepthStencilFormat != EPixelFormat::Unknown;
    if (bHasDepth)
    {
        VkAttachmentDescription Depth{};
        {
            const VkFormat VkDepthFormat = ToVkFormat(Desc.DepthStencilFormat);
            if (VkDepthFormat == VK_FORMAT_UNDEFINED)
            {
                return VK_NULL_HANDLE;
            }
            Depth.format = VkDepthFormat;
            Depth.samples = VK_SAMPLE_COUNT_1_BIT;
            Depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            Depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            Depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            Depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            Depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            Depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            DepthRef.attachment = static_cast<uint32>(Attachments.size());
            DepthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        Attachments.push_back(Depth);
    }

    VkSubpassDescription SubpassDesc{};
    {
        SubpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        SubpassDesc.colorAttachmentCount = static_cast<uint32>(ColorRefs.size());
        SubpassDesc.pColorAttachments = ColorRefs.data();
        if (bHasDepth)
        {
            SubpassDesc.pDepthStencilAttachment = &DepthRef;
        }
    }

    VkRenderPassCreateInfo RenderPassInfo{};
    {
        RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        RenderPassInfo.attachmentCount = static_cast<uint32>(Attachments.size());
        RenderPassInfo.pAttachments = Attachments.data();
        RenderPassInfo.subpassCount = 1;
        RenderPassInfo.pSubpasses = &SubpassDesc;
    }

    VkRenderPass RenderPass = VK_NULL_HANDLE;
    const VkResult Result = vkCreateRenderPass(Device->VkContext.LogicalDevice, &RenderPassInfo, nullptr, &RenderPass);
    if (Result != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return RenderPass;
}

FVulkanRALPipeline_Graphics::FVulkanRALPipeline_Graphics(FVulkanRALDevice* InDevice, const FRALPipelineDesc_Graphics& InDesc)
    : TVulkanResourceBase<FRALPipelineDesc_Graphics>(InDevice, InDesc)
{
    std::vector<VkDescriptorSetLayout> SetLayouts{};
    SetLayouts.reserve(Desc.BindGroupLayouts.size());
    for (FRALBindGroupLayout* SetLayout : Desc.BindGroupLayouts)
    {
        FVulkanRALBindGroupLayout* VkLayout = static_cast<FVulkanRALBindGroupLayout*>(SetLayout);
        SetLayouts.push_back(VkLayout->Handle);
    }

    VkPipelineLayoutCreateInfo LayoutInfo{};
    {
        LayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        LayoutInfo.setLayoutCount = static_cast<uint32>(SetLayouts.size());
        LayoutInfo.pSetLayouts = SetLayouts.data();
    }
    VkResult Result = vkCreatePipelineLayout(this->Device->VkContext.LogicalDevice, &LayoutInfo, nullptr, &this->PipelineLayout);
    if (Result != VK_SUCCESS)
    {
        this->PipelineLayout = VK_NULL_HANDLE;
        return;
    }

    // TODO: kodak Currently create the minimal render pass
    this->RenderPass = CreateMinimalRenderPass(this->Device, this->Desc);
    if (this->RenderPass == VK_NULL_HANDLE)
    {
        return;
    }

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> ShaderStages{};
    ShaderStages.reserve(2); // TODO: kodak vs and ps

    if (this->Desc.VertexShader)
    {
        FVulkanRALShader* VS = static_cast<FVulkanRALShader*>(this->Desc.VertexShader);
        if (VS->Module == VK_NULL_HANDLE)
        {
            return;
        }
        VkPipelineShaderStageCreateInfo ShaderStage{};
        {
            ShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            ShaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
            ShaderStage.module = VS->Module;
            ShaderStage.pName = VS->GetEntryPoint().GetData();
            ShaderStages.emplace_back(ShaderStage);
        }
    }
    if (this->Desc.PixelShader)
    {
        FVulkanRALShader* PS = static_cast<FVulkanRALShader*>(this->Desc.PixelShader);
        if (PS->Module == VK_NULL_HANDLE)
        {
            return;
        }
        VkPipelineShaderStageCreateInfo ShaderStage{};
        {
            ShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            ShaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            ShaderStage.module = PS->Module;
            ShaderStage.pName = PS->GetEntryPoint().GetData();
            ShaderStages.emplace_back(ShaderStage);
        }
    }

    // 将 RAL 顶点输入转换为 Vulkan
    std::vector<VkVertexInputBindingDescription> VkBindings;
    for (const auto& Binding : Desc.VertexBindings)
    {
        VkVertexInputBindingDescription VkBinding{};
        VkBinding.binding = Binding.Binding;
        VkBinding.stride = Binding.Stride;
        VkBinding.inputRate = Binding.bPerInstance ?
            VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        VkBindings.push_back(VkBinding);
    }

    std::vector<VkVertexInputAttributeDescription> VkAttributes;
    for (const auto& Attr : Desc.VertexAttributes)
    {
        VkVertexInputAttributeDescription VkAttr{};
        VkAttr.location = Attr.Location;
        VkAttr.binding = Attr.Binding;
        VkAttr.format = ToVkFormat(Attr.Format);
        if (VkAttr.format == VK_FORMAT_UNDEFINED)
        {
            return;
        }
        VkAttr.offset = Attr.Offset;
        VkAttributes.push_back(VkAttr);
    }

    VkPipelineVertexInputStateCreateInfo VertexInput{};
    {
        VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VertexInput.vertexBindingDescriptionCount = static_cast<uint32>(VkBindings.size());
        VertexInput.pVertexBindingDescriptions = VkBindings.data();
        VertexInput.vertexAttributeDescriptionCount = static_cast<uint32>(VkAttributes.size());
        VertexInput.pVertexAttributeDescriptions = VkAttributes.data();
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

    std::vector<VkPipelineColorBlendAttachmentState> ColorBlendAttachments{};
    ColorBlendAttachments.resize(this->Desc.RenderTargetCount);
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
        ColorBlendInfo.pAttachments = ColorBlendAttachments.data();
    }

    VkDynamicState Dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicState{};
    {
        DynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        DynamicState.dynamicStateCount = 2;
        DynamicState.pDynamicStates = Dynamics;
    }

    VkGraphicsPipelineCreateInfo PipelineInfo{};
    {
        PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        PipelineInfo.stageCount = static_cast<uint32>(ShaderStages.size());

        PipelineInfo.pStages = ShaderStages.data();
        PipelineInfo.pVertexInputState = &VertexInput;
        PipelineInfo.pInputAssemblyState = &InputAssembly;
        PipelineInfo.pViewportState = &ViewportState;
        PipelineInfo.pRasterizationState = &Rasterizer;
        PipelineInfo.pMultisampleState = &Multisampling;
        PipelineInfo.pDepthStencilState = &DepthStencil;
        PipelineInfo.pColorBlendState = &ColorBlendInfo;
        PipelineInfo.pDynamicState = &DynamicState;

        PipelineInfo.layout = this->PipelineLayout;
        PipelineInfo.renderPass = this->RenderPass;
        PipelineInfo.subpass = 0; // TODO: kodak
    }
    Result = vkCreateGraphicsPipelines(this->Device->VkContext.LogicalDevice, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &this->Pipeline);
    if (Result != VK_SUCCESS)
    {
        this->Pipeline = VK_NULL_HANDLE;
    }
}

FVulkanRALPipeline_Graphics::~FVulkanRALPipeline_Graphics()
{
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
    if (this->RenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(Device->VkContext.LogicalDevice, this->RenderPass, nullptr);
        this->RenderPass = VK_NULL_HANDLE;
    }
}
