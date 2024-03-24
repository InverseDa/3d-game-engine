#include "context.hpp"
#include "log/log.hpp"
#include "tools.hpp"

namespace ida {
Context* Context::instance_ = nullptr;

void Context::Init(std::vector<const char*>& extensions, GetSurfaceCallback cb) {
    instance_ = new Context(extensions, cb);
}

void Context::Quit() {
    delete instance_;
}

Context& Context::GetInstance() {
    return *instance_;
}

Context::Context(std::vector<const char*>& extensions, GetSurfaceCallback cb) {
    getSurfaceCb_ = cb;

    instance = CreateInstance(extensions);
    if (!instance) {
        IO::ThrowError("Failed to create instance");
    }

    phyDevice = PickupPhysicalDevice();
    if (!phyDevice) {
        IO::ThrowError("Failed to pickup physical device");
    }
    IO::PrintLog(LOG_LEVEL::LOG_LEVEL_INFO, "Physical device name: {}", phyDevice.getProperties().deviceName.data());

    surface_ = getSurfaceCb_(instance);
    device = CreateDevice(surface_);
    if (!device) {
        IO::ThrowError("Failed to create device");
    }

    auto queueInfo = QueryQueueFamily(surface_);
    graphicsQueue = device.getQueue(queueInfo.graphicsIndex.value(), 0);
    presentQueue = device.getQueue(queueInfo.presentIndex.value(), 0);

    commandPool = CreateCommandPool();
}

Context::~Context() {
    device.destroyCommandPool(commandPool);
    device.destroy();

    instance.destroySurfaceKHR(surface_);
    instance.destroy();
}

vk::Instance Context::CreateInstance(std::vector<const char*>& extensions) {
    auto appInfo = vk::ApplicationInfo()
                       .setApiVersion(VK_VERSION_1_3);
    auto createInfo = vk::InstanceCreateInfo()
                          .setPApplicationInfo(&appInfo)
                          .setPEnabledExtensionNames(extensions)
                          .setPEnabledLayerNames(validationLayers);
    return vk::createInstance(createInfo);
}

vk::PhysicalDevice Context::PickupPhysicalDevice() {
    auto devices = instance.enumeratePhysicalDevices();
    if (devices.size() == 0) {
        IO::ThrowError("Failed to find physical devices suitable for vulkan! Make sure you have a compatible GPU installed.");
    }
    return devices[0];
}

vk::Device Context::CreateDevice(vk::SurfaceKHR surface) {
    vk::DeviceCreateInfo deviceCreateInfo;
    QueueFamilyIndices queueInfo = QueryQueueFamily(surface);
    deviceCreateInfo.setPEnabledExtensionNames(deviceExtensions);

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    float priority = 1.0;
    if (queueInfo.graphicsIndex.value() == queueInfo.presentIndex.value()) {
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.setPQueuePriorities(&priority);
        queueCreateInfo.setQueueFamilyIndex(queueInfo.graphicsIndex.value());
        queueCreateInfo.setQueueCount(1);
        queueCreateInfos.push_back(queueCreateInfo);
    } else {
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.setPQueuePriorities(&priority);
        queueCreateInfo.setQueueFamilyIndex(queueInfo.graphicsIndex.value());
        queueCreateInfo.setQueueCount(1);
        queueCreateInfos.push_back(queueCreateInfo);

        queueCreateInfo.setQueueFamilyIndex(queueInfo.presentIndex.value());
        queueCreateInfos.push_back(queueCreateInfo);
    }
    deviceCreateInfo.setQueueCreateInfos(queueCreateInfos);

    return phyDevice.createDevice(deviceCreateInfo);
}

QueueFamilyIndices Context::QueryQueueFamily(vk::SurfaceKHR surface) {
    QueueFamilyIndices queueFamilyIndices;
    auto queueFamilies = phyDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueCount > 0 && queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            queueFamilyIndices.graphicsIndex = i;
        }
        if (queueFamilies[i].queueCount > 0 && phyDevice.getSurfaceSupportKHR(i, surface)) {
            queueFamilyIndices.presentIndex = i;
        }
        if (queueFamilyIndices) {
            break;
        }
    }
    return queueFamilyIndices;
}

uint32_t Context::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    auto memProperties = phyDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    IO::ThrowError("Failed to find suitable memory type!");
}

SwapChainSupportDetails Context::QuerySwapChainSupport() {
    SwapChainSupportDetails details;
    details.capabilities = phyDevice.getSurfaceCapabilitiesKHR(surface_);
    details.formats = phyDevice.getSurfaceFormatsKHR(surface_);
    details.presentModes = phyDevice.getSurfacePresentModesKHR(surface_);
    return details;
}

vk::Format Context::QuerySupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
    for (vk::Format format : candidates) {
        vk::FormatProperties props = phyDevice.getFormatProperties(format);
        if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    IO::ThrowError("Failed to find supported format");
}

void Context::CreateImageWithInfo(const vk::ImageCreateInfo& imageInfo, vk::MemoryPropertyFlags properties, vk::Image& image, vk::DeviceMemory& imageMemory) {
    image = device.createImage(imageInfo);
    vk::MemoryRequirements memRequirements = device.getImageMemoryRequirements(image);
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.setAllocationSize(memRequirements.size);
    allocInfo.setMemoryTypeIndex(FindMemoryType(memRequirements.memoryTypeBits, properties));
    imageMemory = device.allocateMemory(allocInfo);
    device.bindImageMemory(image, imageMemory, 0);
}

std::vector<vk::CommandBuffer> Context::CreateCommandBuffer(int count) {
    auto cmdInfo = vk::CommandBufferAllocateInfo()
                       .setCommandPool(commandPool)
                       .setLevel(vk::CommandBufferLevel::ePrimary)
                       .setCommandBufferCount(count);
    return device.allocateCommandBuffers(cmdInfo);
}

void Context::ExecuteCommandBuffer(vk::Queue queue, std::function<void(vk::CommandBuffer&)> func) {
    auto cmdBuf = CreateCommandBuffer()[0];
    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmdBuf.begin(beginInfo);
    if (func)
        func(cmdBuf);
    cmdBuf.end();
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(cmdBuf);
    queue.submit(submitInfo);
    queue.waitIdle();
    device.waitIdle();
    device.freeCommandBuffers(commandPool, cmdBuf);
}

vk::CommandPool Context::CreateCommandPool() {
    auto queueInfo = QueryQueueFamily(surface_);
    vk::CommandPoolCreateInfo createInfo;
    createInfo.setQueueFamilyIndex(queueInfo.graphicsIndex.value());
    createInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient);
    return device.createCommandPool(createInfo);
}

} // namespace ida