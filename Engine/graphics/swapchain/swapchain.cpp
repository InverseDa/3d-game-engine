#include "swapchain.hpp"
#include "core/context.hpp"

namespace ida {

IdaSwapChain::IdaSwapChain(vk::Extent2D windowExtent) : windowExtent_(windowExtent) {
    Init();
}

IdaSwapChain::IdaSwapChain(vk::Extent2D windowExtent, std::shared_ptr<IdaSwapChain> previous)
    : windowExtent_(windowExtent), oldSwapChain_(previous) {
    Init();
    oldSwapChain_ = nullptr;
}

void IdaSwapChain::Init() {
    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateDepthResources();
    CreateFramebuffers();
    CreateSyncObjects();
}

IdaSwapChain::~IdaSwapChain() {
    auto& device = Context::GetInstance().device;
    for (auto imageView : swapChainImageViews_) {
        device.destroyImageView(imageView);
    }
    swapChainImageViews_.clear();
    if (swapChain_ != nullptr) {
        device.destroySwapchainKHR(swapChain_);
    }

    for (size_t i = 0; i < depthImages_.size(); i++) {
        device.destroyImageView(depthImageViews_[i]);
        device.destroyImage(depthImages_[i]);
        device.freeMemory(depthImageMemories_[i]);
    }

    for (auto framebuffer : swapChainFramebuffers_) {
        device.destroyFramebuffer(framebuffer);
    }
    device.destroyRenderPass(renderPass_);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        device.destroySemaphore(imageAvailableSemaphores_[i]);
        device.destroySemaphore(renderFinishedSemaphores_[i]);
        device.destroyFence(inFlightFences_[i]);
    }
}

vk::Format IdaSwapChain::FindDepthFormat() {
    return Context::GetInstance().QuerySupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

vk::Result IdaSwapChain::AcquireNextImageIndex(uint32_t& imageIndex) {
    auto& device = Context::GetInstance().device;
    device.waitForFences(inFlightFences_[currentFrame], true, std::numeric_limits<std::uint64_t>::max());

    auto resultValue = device.acquireNextImageKHR(swapChain_, std::numeric_limits<std::uint64_t>::max(), imageAvailableSemaphores_[currentFrame], nullptr);
    imageIndex = resultValue.value;
    return resultValue.result;
}

vk::Result IdaSwapChain::SubmitCommandBuffers(const vk::CommandBuffer* buffers, uint32_t* imageIndex) {
    auto& ctx = Context::GetInstance();
    auto& device = ctx.device;
    if (imagesInFlight_[*imageIndex] != nullptr) {
        device.waitForFences(imagesInFlight_[*imageIndex], true, std::numeric_limits<std::uint64_t>::max());
    }
    imagesInFlight_[*imageIndex] = inFlightFences_[currentFrame];

    std::array<vk::PipelineStageFlags, 1> waitStages = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    std::array<vk::Semaphore, 1> waitSemaphores = {imageAvailableSemaphores_[currentFrame]};
    std::array<vk::Semaphore, 1> signalSemaphores = {renderFinishedSemaphores_[currentFrame]};
    auto submitInfo = vk::SubmitInfo()
                          .setWaitSemaphoreCount(1)
                          .setPWaitSemaphores(waitSemaphores.data())
                          .setPWaitDstStageMask(waitStages.data())
                          .setCommandBufferCount(1)
                          .setPCommandBuffers(buffers)
                          .setSignalSemaphoreCount(1)
                          .setPSignalSemaphores(signalSemaphores.data());
    device.resetFences(inFlightFences_[currentFrame]);
    ctx.graphicsQueue.submit(submitInfo, inFlightFences_[currentFrame]);

    std::array<vk::SwapchainKHR, 1> swapChains = {swapChain_};
    auto presentInfo = vk::PresentInfoKHR()
                           .setWaitSemaphoreCount(1)
                           .setPWaitSemaphores(signalSemaphores.data())
                           .setSwapchainCount(1)
                           .setPSwapchains(swapChains.data())
                           .setPImageIndices(imageIndex);

    auto result = ctx.presentQueue.presentKHR(presentInfo);
    if(result != vk::Result::eSuccess) {
        IO::PrintLog(LOG_LEVEL::LOG_LEVEL_ERROR, "Failed to present image");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    return result;
}

void IdaSwapChain::CreateSwapChain() {
    auto& ctx = Context::GetInstance();
    auto swapChainSupport = ctx.QuerySwapChainSupport();
    vk::SurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    vk::PresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
    vk::Extent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);

    auto imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    auto createInfo = vk::SwapchainCreateInfoKHR()
                          .setSurface(Context::GetInstance().GetSurface())
                          .setMinImageCount(imageCount)
                          .setImageFormat(surfaceFormat.format)
                          .setImageColorSpace(surfaceFormat.colorSpace)
                          .setImageExtent(extent)
                          .setImageArrayLayers(1)
                          .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
                          .setPreTransform(swapChainSupport.capabilities.currentTransform)
                          .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
                          .setPresentMode(presentMode)
                          .setClipped(true)
                          .setOldSwapchain(oldSwapChain_ ? oldSwapChain_->swapChain_ : nullptr);

    QueueFamilyIndices indices = ctx.QueryQueueFamily(Context::GetInstance().GetSurface());
    std::array queueIndices = {indices.graphicsIndex.value(), indices.presentIndex.value()};
    if (indices.graphicsIndex.value() == indices.presentIndex.value()) {
        createInfo.setImageSharingMode(vk::SharingMode::eExclusive);
        createInfo.setQueueFamilyIndexCount(0);
        createInfo.setPQueueFamilyIndices(nullptr);
    } else {
        createInfo.setImageSharingMode(vk::SharingMode::eConcurrent);
        createInfo.setQueueFamilyIndexCount(static_cast<uint32_t>(queueIndices.size()));
        createInfo.setQueueFamilyIndices(queueIndices);
    }

    swapChain_ = ctx.device.createSwapchainKHR(createInfo);
    swapChainImages_ = ctx.device.getSwapchainImagesKHR(swapChain_);
    swapChainImageFormat_ = surfaceFormat.format;
    swapChainExtent_ = extent;
}

void IdaSwapChain::CreateImageViews() {
    auto& device = Context::GetInstance().device;
    swapChainImageViews_.resize(swapChainImages_.size());
    for (size_t i = 0; i < swapChainImages_.size(); i++) {
        auto viewInfo = vk::ImageViewCreateInfo()
                            .setImage(swapChainImages_[i])
                            .setViewType(vk::ImageViewType::e2D)
                            .setFormat(swapChainImageFormat_)
                            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        swapChainImageViews_[i] = device.createImageView(viewInfo);
    }
}

void IdaSwapChain::CreateDepthResources() {
    auto& ctx = Context::GetInstance();
    swapChainDepthFormat_ = FindDepthFormat();

    depthImages_.resize(GetImageCount());
    depthImageMemories_.resize(GetImageCount());
    depthImageViews_.resize(GetImageCount());

    for (int i = 0; i < depthImages_.size(); i++) {
        auto depthImageCreateInfo = vk::ImageCreateInfo()
                                        .setImageType(vk::ImageType::e2D)
                                        .setExtent({swapChainExtent_.width, swapChainExtent_.height, 1})
                                        .setMipLevels(1)
                                        .setArrayLayers(1)
                                        .setFormat(swapChainDepthFormat_)
                                        .setTiling(vk::ImageTiling::eOptimal)
                                        .setInitialLayout(vk::ImageLayout::eUndefined)
                                        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
                                        .setSamples(vk::SampleCountFlagBits::e1)
                                        .setSharingMode(vk::SharingMode::eExclusive)
                                        .setQueueFamilyIndexCount(0);
        ctx.CreateImageWithInfo(depthImageCreateInfo, vk::MemoryPropertyFlagBits::eDeviceLocal, depthImages_[i], depthImageMemories_[i]);

        auto depthImageViewCreateInfo = vk::ImageViewCreateInfo()
                                            .setImage(depthImages_[i])
                                            .setViewType(vk::ImageViewType::e2D)
                                            .setFormat(swapChainDepthFormat_)
                                            .setSubresourceRange({vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});

        depthImageViews_[i] = ctx.device.createImageView(depthImageViewCreateInfo);
    }
}

void IdaSwapChain::CreateRenderPass() {
    swapChainDepthFormat_ = FindDepthFormat();

    auto depthAttachment = vk::AttachmentDescription()
                               .setFormat(swapChainDepthFormat_)
                               .setSamples(vk::SampleCountFlagBits::e1)
                               .setLoadOp(vk::AttachmentLoadOp::eClear)
                               .setStoreOp(vk::AttachmentStoreOp::eDontCare)
                               .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                               .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                               .setInitialLayout(vk::ImageLayout::eUndefined)
                               .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
    auto depthAttachmentRef = vk::AttachmentReference()
                                  .setAttachment(1)
                                  .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    auto colorAttachment = vk::AttachmentDescription()
                               .setFormat(swapChainImageFormat_)
                               .setSamples(vk::SampleCountFlagBits::e1)
                               .setLoadOp(vk::AttachmentLoadOp::eClear)
                               .setStoreOp(vk::AttachmentStoreOp::eStore)
                               .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                               .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                               .setInitialLayout(vk::ImageLayout::eUndefined)
                               .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
    auto colorAttachmentRef = vk::AttachmentReference()
                                  .setAttachment(0)
                                  .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    auto subPass = vk::SubpassDescription()
                       .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                       .setColorAttachmentCount(1)
                       .setPColorAttachments(&colorAttachmentRef)
                       .setPDepthStencilAttachment(&depthAttachmentRef);
    auto subPassDependency = vk::SubpassDependency()
                                 .setSrcSubpass(vk::SubpassExternal)
                                 .setDstSubpass(0)
                                 .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
                                 .setSrcAccessMask(vk::AccessFlagBits::eNoneKHR)
                                 .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
                                 .setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eColorAttachmentWrite);
    std::vector<vk::AttachmentDescription> attachments = {colorAttachment, depthAttachment};
    auto renderPassCreateInfo = vk::RenderPassCreateInfo()
                                    .setAttachmentCount(static_cast<uint32_t>(attachments.size()))
                                    .setAttachments(attachments)
                                    .setSubpassCount(1)
                                    .setSubpasses(subPass)
                                    .setDependencyCount(1)
                                    .setDependencies(subPassDependency);

    renderPass_ = Context::GetInstance().device.createRenderPass(renderPassCreateInfo);
}

void IdaSwapChain::CreateFramebuffers() {
    swapChainFramebuffers_.resize(GetImageCount());
    for (size_t i = 0; i < GetImageCount(); i++) {
        std::array<vk::ImageView, 2> attachments = {swapChainImageViews_[i], depthImageViews_[i]};
        auto framebufferCreateInfo = vk::FramebufferCreateInfo()
                                         .setRenderPass(renderPass_)
                                         .setAttachmentCount(static_cast<uint32_t>(attachments.size()))
                                         .setPAttachments(attachments.data())
                                         .setWidth(swapChainExtent_.width)
                                         .setHeight(swapChainExtent_.height)
                                         .setLayers(1);
        swapChainFramebuffers_[i] = Context::GetInstance().device.createFramebuffer(framebufferCreateInfo);
    }
}

void IdaSwapChain::CreateSyncObjects() {
    auto& device = Context::GetInstance().device;
    imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);
    imagesInFlight_.resize(GetImageCount(), nullptr);

    auto semaphoreCreateInfo = vk::SemaphoreCreateInfo();
    auto fenceCreateInfo = vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        imageAvailableSemaphores_[i] = device.createSemaphore(semaphoreCreateInfo);
        renderFinishedSemaphores_[i] = device.createSemaphore(semaphoreCreateInfo);
        inFlightFences_[i] = device.createFence(fenceCreateInfo);
    }
}

vk::SurfaceFormatKHR IdaSwapChain::ChooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == vk::Format::eB8G8R8A8Srgb && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

vk::PresentModeKHR IdaSwapChain::ChooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == vk::PresentModeKHR::eMailbox) {
            IO::PrintLog(LOG_LEVEL::LOG_LEVEL_INFO, "Present Mode: Mailbox");
            return availablePresentMode;
        }
    }
    IO::PrintLog(LOG_LEVEL::LOG_LEVEL_INFO, "Present Mode: V-Sync(FIFO)");
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D IdaSwapChain::ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        vk::Extent2D actualExtent = windowExtent_;
        actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));
        return actualExtent;
    }
}

} // namespace ida
