#ifndef VULKAN_LIB_PIPELINE_HPP
#define VULKAN_LIB_PIPELINE_HPP

#include <vector>
#include "vulkan/vulkan.hpp"

namespace ida {
struct PipelineConfigInfo {
    PipelineConfigInfo() = default;
    PipelineConfigInfo(const PipelineConfigInfo&) = delete;
    PipelineConfigInfo& operator=(const PipelineConfigInfo&) = delete;

    std::vector<vk::VertexInputBindingDescription> bindingDescriptions{};
    std::vector<vk::VertexInputAttributeDescription> attributeDescriptions{};
    vk::PipelineViewportStateCreateInfo viewportInfo;
    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
    vk::PipelineRasterizationStateCreateInfo rasterizationInfo;
    vk::PipelineMultisampleStateCreateInfo multisampleInfo;
    vk::PipelineColorBlendAttachmentState colorBlendAttachment;
    vk::PipelineColorBlendStateCreateInfo colorBlendInfo;
    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo;
    std::vector<vk::DynamicState> dynamicStateEnables;
    vk::PipelineDynamicStateCreateInfo dynamicStateInfo;
    vk::PipelineLayout pipelineLayout = nullptr;
    vk::RenderPass renderPass = nullptr;
    uint32_t subpass = 0;
};

class IdaPipeline {
  public:
    IdaPipeline(const std::string& vertPath, const std::string& fragPath, const PipelineConfigInfo& configInfo);
    IdaPipeline(const std::vector<char>& vertCode, const std::vector<char>& fragCode, const PipelineConfigInfo& configInfo);

    ~IdaPipeline();
    IdaPipeline(const IdaPipeline&) = delete;
    IdaPipeline& operator=(const IdaPipeline&) = delete;

    void Bind(vk::CommandBuffer commandBuffer);

    static void DefaultPipelineConfigInfo(PipelineConfigInfo& configInfo);
    static void EnableAlphaBlending(PipelineConfigInfo& configInfo);

  private:
    void CreateGraphicsPipeline(
        const std::vector<char>& vertCode,
        const std::vector<char>& fragCode,
        const PipelineConfigInfo& configInfo);
    void CreateShaderModule(const std::vector<char>& code, vk::ShaderModule* shaderModule);

    vk::Pipeline pipeline_;
    vk::ShaderModule vertShaderModule_;
    vk::ShaderModule fragShaderModule_;
};

} // namespace ida
#endif // VULKAN_LIB_PIPELINE_HPP
