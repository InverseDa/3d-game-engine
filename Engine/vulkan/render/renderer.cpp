#include "renderer.hpp"
#include "core/context.hpp"

namespace ida {
IdaRenderer::IdaRenderer(ida::IdaWindow& window) : window_{window} {
    RecreateSwapChain();
    CreateCommandBuffers();
}
IdaRenderer::~IdaRenderer() {
    FreeCommandBuffers();
}

void IdaRenderer::RecreateSwapChain() {
    auto& device = Context::GetInstance().device;
    auto extent = window_.GetExtent();
    while (extent.width == 0 || extent.height == 0) {
        extent = window_.GetExtent();
        glfwPollEvents();
    }
    device.waitIdle();

    if (swapChain_ == nullptr) {
        swapChain_ = std::make_unique<IdaSwapChain>(extent);
    } else {
        std::shared_ptr<IdaSwapChain> oldSwapChain = std::move(swapChain_);
        swapChain_ = std::make_unique<IdaSwapChain>(extent, oldSwapChain);

        if (!oldSwapChain->CompareSwapFormats(*swapChain_.get())) {
            throw std::runtime_error("Swap chain image (or depth) format has changed!");
        }
    }
}

void IdaRenderer::FreeCommandBuffers() {
    auto& ctx = Context::GetInstance();
    ctx.device.freeCommandBuffers(ctx.commandPool, commandBuffers_);
    commandBuffers_.clear();
}

void IdaRenderer::CreateCommandBuffers() {
    auto& ctx = Context::GetInstance();
    commandBuffers_.resize(IdaSwapChain::MAX_FRAMES_IN_FLIGHT);
    auto allocateInfo = vk::CommandBufferAllocateInfo()
                            .setCommandPool(ctx.commandPool)
                            .setCommandBufferCount(static_cast<uint32_t>(commandBuffers_.size()))
                            .setLevel(vk::CommandBufferLevel::ePrimary);
    commandBuffers_ = ctx.device.allocateCommandBuffers(allocateInfo);
}

vk::CommandBuffer IdaRenderer::BeginFrame() {
    IO::Assert(!isFrameStarted, "Frame already in progress");
    auto result = swapChain_->AcquireNextImageIndex(currentImageIndex);
    if (result == vk::Result::eErrorOutOfDateKHR) {
        RecreateSwapChain();
        return nullptr;
    }
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        IO::ThrowError("Failed to acquire swap chain image");
    }
    isFrameStarted = true;

    auto& ctx = Context::GetInstance();
    auto cmdBuffer = commandBuffers_[currentFrameIndex];
    auto beginInfo = vk::CommandBufferBeginInfo();
    cmdBuffer.begin(beginInfo);
    return cmdBuffer;
}

void IdaRenderer::EndFrame() {
    IO::Assert(isFrameStarted, "Frame not in progress");
    auto& ctx = Context::GetInstance();
    auto cmdBuffer = commandBuffers_[currentFrameIndex];
    cmdBuffer.end();

    auto result = swapChain_->SubmitCommandBuffers(&cmdBuffer, &currentImageIndex);
    if (result == vk::Result::eErrorOutOfDateKHR ||
        result == vk::Result::eSuboptimalKHR ||
        window_.IsResizeNow()) {
        window_.ResetResizeFlag();
        RecreateSwapChain();
    } else if (result != vk::Result::eSuccess) {
        IO::ThrowError("Failed to present swap chain image");
    }
    currentFrameIndex = (currentFrameIndex + 1) % IdaSwapChain::MAX_FRAMES_IN_FLIGHT;
    isFrameStarted = false;
}

void IdaRenderer::BeginSwapChainRenderPass(vk::CommandBuffer commandBuffer) {
    IO::Assert(isFrameStarted, "Can't call IdaRenderer::BeginSwapChainRenderPass if frame is not in progress");
    IO::Assert(commandBuffer == commandBuffers_[currentFrameIndex], "Can't begin render pass on command buffer from a different frame");
    auto& ctx = Context::GetInstance();

    std::array<vk::ClearValue, 2> clearValues;
    clearValues[0].color = {0.2f, 0.3f, 0.3f, 1.0f};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0.f);
    auto renderPassInfo = vk::RenderPassBeginInfo()
                              .setRenderPass(swapChain_->GetRenderPass())
                              .setFramebuffer(swapChain_->GetFrameBuffer(currentImageIndex))
                              .setRenderArea({{0, 0}, swapChain_->GetSwapChainExtent()})
                              .setClearValueCount(static_cast<uint32_t>(clearValues.size()))
                              .setPClearValues(clearValues.data());
    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    auto viewport = vk::Viewport()
                        .setX(0.0f)
                        .setY(0.0f)
                        .setWidth(static_cast<float>(swapChain_->GetSwapChainExtent().width))
                        .setHeight(static_cast<float>(swapChain_->GetSwapChainExtent().height))
                        .setMinDepth(0.0f)
                        .setMaxDepth(1.0f);
    auto scissor = vk::Rect2D()
                       .setOffset({0, 0})
                       .setExtent(swapChain_->GetSwapChainExtent());
    commandBuffer.setViewport(0, viewport);
    commandBuffer.setScissor(0, scissor);
}

void IdaRenderer::EndSwapChainRenderPass(vk::CommandBuffer commandBuffer) {
    IO::Assert(isFrameStarted, "Can't call IdaRenderer::EndSwapChainRenderPass if frame is not in progress");
    IO::Assert(commandBuffer == commandBuffers_[currentFrameIndex], "Can't end render pass on command buffer from a different frame");
    commandBuffer.endRenderPass();
}

} // namespace ida