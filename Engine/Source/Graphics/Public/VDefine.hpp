#ifndef ENGINE_VDEFINE_HPP
#define ENGINE_VDEFINE_HPP

#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

namespace VulkanCore {
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct QueueFamilyIndices final {
    std::optional<std::uint32_t> graphicsIndex;
    std::optional<std::uint32_t> presentIndex;

    operator bool() {
        return graphicsIndex.has_value() && presentIndex.has_value();
    }
};
} // namespace VulkanCore

#endif // ENGINE_VDEFINE_HPP
