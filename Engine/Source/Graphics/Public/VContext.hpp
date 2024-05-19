#ifndef ENGINE_VCONTEXT_HPP
#define ENGINE_VCONTEXT_HPP

#include "Source/Graphics/Public/VDefine.hpp"

#include <vulkan/vulkan.h>

namespace VulkanCore {
class Context {
  public:
    Context();
    ~Context();
    void InitVulkan();
    void Cleanup();
    VkInstance GetInstance() { return _instance; }
    VkPhysicalDevice GetPhysicalDevice() { return _physicalDevice; }
    VkDevice GetDevice() { return _device; }
    VkQueue GetGraphicsQueue() { return _graphicsQueue; }
    VkQueue GetPresentQueue() { return _presentQueue; }

  private:
    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();

    VkInstance _instance;
    VkPhysicalDevice _physicalDevice;
    VkDevice _device;
    VkQueue _graphicsQueue;
    VkQueue _presentQueue;
};
} // namespace VulkanCore

#endif // ENGINE_VCONTEXT_HPP
