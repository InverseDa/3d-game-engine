#include "pipeline.hpp"
#include "tools.hpp"
#include "core/context.hpp"
#include "model/model.hpp"

#include <vector>

namespace ida {
// ******************************* IdaPipeline *******************************
IdaPipeline::IdaPipeline(const std::string& vertPath, const std::string& fragPath, const PipelineConfigInfo& configInfo) {
    CreateGraphicsPipeline(ReadWholeFile(vertPath), ReadWholeFile(fragPath), configInfo);
}

IdaPipeline::IdaPipeline(const std::vector<char>& vertCode, const std::vector<char>& fragCode, const PipelineConfigInfo& configInfo) {
    CreateGraphicsPipeline(vertCode, fragCode, configInfo);
}

IdaPipeline::~IdaPipeline() {
    auto& device = Context::GetInstance().device;
    device.destroyShaderModule(vertShaderModule_);
    device.destroyShaderModule(fragShaderModule_);
    device.destroyPipeline(pipeline_);
}

void IdaPipeline::Bind(vk::CommandBuffer commandBuffer) {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
}

void IdaPipeline::DefaultPipelineConfigInfo(PipelineConfigInfo& configInfo) {
    configInfo.inputAssemblyInfo.topology = vk::PrimitiveTopology::eTriangleList;
    configInfo.inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

    configInfo.viewportInfo.viewportCount = 1;
    configInfo.viewportInfo.pViewports = nullptr;
    configInfo.viewportInfo.scissorCount = 1;
    configInfo.viewportInfo.pScissors = nullptr;

    configInfo.rasterizationInfo.depthClampEnable = VK_FALSE;
    configInfo.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
    configInfo.rasterizationInfo.polygonMode = vk::PolygonMode::eFill;
    configInfo.rasterizationInfo.lineWidth = 1.0f;
    configInfo.rasterizationInfo.cullMode = vk::CullModeFlagBits::eNone;
    configInfo.rasterizationInfo.frontFace = vk::FrontFace::eCounterClockwise;
    configInfo.rasterizationInfo.depthBiasEnable = VK_FALSE;
    configInfo.rasterizationInfo.depthBiasConstantFactor = 0.0f; // Optional
    configInfo.rasterizationInfo.depthBiasClamp = 0.0f;          // Optional
    configInfo.rasterizationInfo.depthBiasSlopeFactor = 0.0f;    // Optional

    configInfo.multisampleInfo.sampleShadingEnable = VK_FALSE;
    configInfo.multisampleInfo.rasterizationSamples = vk::SampleCountFlagBits::e1;
    configInfo.multisampleInfo.minSampleShading = 1.0f;          // Optional
    configInfo.multisampleInfo.pSampleMask = nullptr;            // Optional
    configInfo.multisampleInfo.alphaToCoverageEnable = VK_FALSE; // Optional
    configInfo.multisampleInfo.alphaToOneEnable = VK_FALSE;      // Optional

    configInfo.colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                     vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    configInfo.colorBlendAttachment.blendEnable = VK_FALSE;
    configInfo.colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;  // Optional
    configInfo.colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eZero; // Optional
    configInfo.colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;             // Optional
    configInfo.colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;  // Optional
    configInfo.colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero; // Optional
    configInfo.colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;             // Optional

    configInfo.colorBlendInfo.logicOpEnable = VK_FALSE;
    configInfo.colorBlendInfo.logicOp = vk::LogicOp::eCopy; // Optional
    configInfo.colorBlendInfo.attachmentCount = 1;
    configInfo.colorBlendInfo.pAttachments = &configInfo.colorBlendAttachment;
    configInfo.colorBlendInfo.blendConstants[0] = 0.0f; // Optional
    configInfo.colorBlendInfo.blendConstants[1] = 0.0f; // Optional
    configInfo.colorBlendInfo.blendConstants[2] = 0.0f; // Optional
    configInfo.colorBlendInfo.blendConstants[3] = 0.0f; // Optional

    configInfo.depthStencilInfo.depthTestEnable = VK_TRUE;
    configInfo.depthStencilInfo.depthWriteEnable = VK_TRUE;
    configInfo.depthStencilInfo.depthCompareOp = vk::CompareOp::eLess;
    configInfo.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
    configInfo.depthStencilInfo.minDepthBounds = 0.0f; // Optional
    configInfo.depthStencilInfo.maxDepthBounds = 1.0f; // Optional
    configInfo.depthStencilInfo.stencilTestEnable = VK_FALSE;
    //    configInfo.depthStencilInfo.front = {}; // Optional
    //    configInfo.depthStencilInfo.back = {};  // Optional

    configInfo.dynamicStateEnables = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    configInfo.dynamicStateInfo.pDynamicStates = configInfo.dynamicStateEnables.data();
    configInfo.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(configInfo.dynamicStateEnables.size());
    configInfo.dynamicStateInfo.flags = vk::PipelineDynamicStateCreateFlags();

    configInfo.bindingDescriptions = IdaModel::Vertex::GetBindingDescriptions();
    configInfo.attributeDescriptions = IdaModel::Vertex::GetAttributeDescriptions();
}

void IdaPipeline::EnableAlphaBlending(PipelineConfigInfo& configInfo) {
    configInfo.colorBlendAttachment.blendEnable = vk::True;
    configInfo.colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                     vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    configInfo.colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    configInfo.colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    configInfo.colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    configInfo.colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    configInfo.colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    configInfo.colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
}

void IdaPipeline::CreateGraphicsPipeline(const std::vector<char>& vertCode, const std::vector<char>& fragCode, const PipelineConfigInfo& configInfo) {
    CreateShaderModule(vertCode, &vertShaderModule_);
    CreateShaderModule(fragCode, &fragShaderModule_);

    vk::PipelineShaderStageCreateInfo shaderStages[2];
    shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
    shaderStages[0].module = vertShaderModule_;
    shaderStages[0].pName = "main";
    shaderStages[0].flags = vk::PipelineShaderStageCreateFlags();
    shaderStages[0].pNext = nullptr;
    shaderStages[0].pSpecializationInfo = nullptr;
    shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
    shaderStages[1].module = fragShaderModule_;
    shaderStages[1].pName = "main";
    shaderStages[1].flags = vk::PipelineShaderStageCreateFlags();
    shaderStages[1].pNext = nullptr;
    shaderStages[1].pSpecializationInfo = nullptr;

    auto& bindingDescriptions = configInfo.bindingDescriptions;
    auto& attributeDescriptions = configInfo.attributeDescriptions;

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &configInfo.inputAssemblyInfo;
    pipelineInfo.pViewportState = &configInfo.viewportInfo;
    pipelineInfo.pRasterizationState = &configInfo.rasterizationInfo;
    pipelineInfo.pMultisampleState = &configInfo.multisampleInfo;
    pipelineInfo.pDepthStencilState = &configInfo.depthStencilInfo;
    pipelineInfo.pColorBlendState = &configInfo.colorBlendInfo;
    pipelineInfo.pDynamicState = &configInfo.dynamicStateInfo;

    pipelineInfo.layout = configInfo.pipelineLayout;
    pipelineInfo.renderPass = configInfo.renderPass;
    pipelineInfo.subpass = configInfo.subpass;

    pipelineInfo.basePipelineIndex = -1;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    auto result = Context::GetInstance().device.createGraphicsPipeline(VK_NULL_HANDLE, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        IO::ThrowError("Failed to create graphics pipeline!");
    }
    pipeline_ = result.value;
}

void IdaPipeline::CreateShaderModule(const std::vector<char>& code, vk::ShaderModule* shaderModule) {
    auto& device = Context::GetInstance().device;
    auto createInfo = vk::ShaderModuleCreateInfo()
                          .setCodeSize(code.size())
                          .setPCode(reinterpret_cast<const uint32_t*>(code.data()));
    device.createShaderModule(&createInfo, nullptr, shaderModule);
}

} // namespace ida
