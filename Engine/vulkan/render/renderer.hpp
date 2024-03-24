#ifndef VULKAN_LIB_RENDERER_HPP
#define VULKAN_LIB_RENDERER_HPP

#include "vulkan/vulkan.hpp"
#include <limits>
#include "glm/glm.hpp"

#include "buffer/buffer.hpp"
#include "core/window.hpp"
#include "swapchain/swapchain.hpp"

namespace ida {
class IdaRenderer final {
  public:
    IdaRenderer(IdaWindow& window);
    ~IdaRenderer();
    IdaRenderer(const IdaRenderer&) = delete;
    IdaRenderer& operator=(const IdaRenderer&) = delete;

    vk::RenderPass GetRenderPass() const { return swapChain_->GetRenderPass(); }
    float GetAspectRatio() const { return swapChain_->GetExtentAspectRatio(); }
    bool IsFrameInProgress() const { return isFrameStarted; }

    vk::CommandBuffer GetCurrentCommandBuffer() const {
        IO::Assert(isFrameStarted, "No command buffer for current frame");
        return commandBuffers_[currentFrameIndex];
    }
    int GetCurrentFrameIndex() const {
        IO::Assert(isFrameStarted, "No command buffer for current frame");
        return currentFrameIndex;
    }

    vk::CommandBuffer BeginFrame();
    void EndFrame();
    void BeginSwapChainRenderPass(vk::CommandBuffer commandBuffer);
    void EndSwapChainRenderPass(vk::CommandBuffer commandBuffer);

  private:
    void CreateCommandBuffers();
    void FreeCommandBuffers();
    void RecreateSwapChain();

    IdaWindow& window_;

    std::unique_ptr<IdaSwapChain> swapChain_;
    std::vector<vk::CommandBuffer> commandBuffers_;

    uint32_t currentImageIndex = 0;
    int currentFrameIndex = 0;
    bool isFrameStarted = false;
};

} // namespace ida

#endif // VULKAN_LIB_RENDERER_HPP
