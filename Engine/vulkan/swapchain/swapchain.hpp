#ifndef VULKAN_LIB_SWAPCHAIN_HPP
#define VULKAN_LIB_SWAPCHAIN_HPP

#include "vulkan/vulkan.hpp"

namespace ida {
class IdaSwapChain final {
  public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    IdaSwapChain(vk::Extent2D windowExtent);
    IdaSwapChain(vk::Extent2D windowExtent, std::shared_ptr<IdaSwapChain> previous);
    ~IdaSwapChain();
    IdaSwapChain(const IdaSwapChain&) = delete;
    IdaSwapChain& operator=(const IdaSwapChain&) = delete;

    vk::Framebuffer GetFrameBuffer(int index) { return swapChainFramebuffers_[index]; }
    vk::RenderPass GetRenderPass() { return renderPass_; }
    vk::ImageView GetImageView(int index) { return swapChainImageViews_[index]; }
    size_t GetImageCount() { return swapChainImages_.size(); }
    vk::Format GetSwapChainImageFormat() { return swapChainImageFormat_; }
    vk::Extent2D GetSwapChainExtent() { return swapChainExtent_; }
    uint32_t GetWidth() { return swapChainExtent_.width; }
    uint32_t GetHeight() { return swapChainExtent_.height; }
    float GetExtentAspectRatio() { return static_cast<float>(swapChainExtent_.width) / static_cast<float>(swapChainExtent_.height); }

    vk::Format FindDepthFormat();
    vk::Result AcquireNextImageIndex(uint32_t& imageIndex);
    vk::Result SubmitCommandBuffers(const vk::CommandBuffer* buffers, uint32_t* imageIndex);

    bool CompareSwapFormats(const IdaSwapChain& other) const {
        return swapChainImageFormat_ == other.swapChainImageFormat_ &&
               swapChainDepthFormat_ == other.swapChainDepthFormat_;
    }

  private:
    void Init();
    void CreateSwapChain();
    void CreateImageViews();
    void CreateDepthResources();
    void CreateRenderPass();
    void CreateFramebuffers();
    void CreateSyncObjects();

    vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
    vk::PresentModeKHR ChooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);
    vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);

    vk::Format swapChainImageFormat_;
    vk::Format swapChainDepthFormat_;
    vk::Extent2D swapChainExtent_;

    std::vector<vk::Framebuffer> swapChainFramebuffers_;
    vk::RenderPass renderPass_;

    std::vector<vk::Image> depthImages_;
    std::vector<vk::DeviceMemory> depthImageMemories_;
    std::vector<vk::ImageView> depthImageViews_;
    std::vector<vk::Image> swapChainImages_;
    std::vector<vk::ImageView> swapChainImageViews_;

    vk::Extent2D windowExtent_;

    vk::SwapchainKHR swapChain_;
    std::shared_ptr<IdaSwapChain> oldSwapChain_;

    std::vector<vk::Semaphore> imageAvailableSemaphores_;
    std::vector<vk::Semaphore> renderFinishedSemaphores_;
    std::vector<vk::Fence> inFlightFences_;
    std::vector<vk::Fence> imagesInFlight_;
    size_t currentFrame = 0;
};
} // namespace ida

#endif // VULKAN_LIB_SWAPCHAIN_HPP
