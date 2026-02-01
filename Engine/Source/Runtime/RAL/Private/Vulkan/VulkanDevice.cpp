#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

void FVulkanRALDevice::InternalCreateInstance()
{
    VkApplicationInfo AppInfo{};
    {
        AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        AppInfo.pApplicationName = "Limitless Engine"; // TODO: kodak
        AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0); // TODO: kodak
        AppInfo.pEngineName = "Limitless RAL"; // TODO: kodak
        AppInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0); // TODO: kodak
        AppInfo.apiVersion = VK_API_VERSION_1_2;
    }

    VkInstanceCreateInfo CreateInfo{};
    {
        CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        CreateInfo.pApplicationInfo = &AppInfo;
        CreateInfo.enabledExtensionCount = static_cast<uint32>(RAL::Vulkan::InstanceExtensions.size());
        CreateInfo.ppEnabledExtensionNames = RAL::Vulkan::InstanceExtensions.data();
        CreateInfo.enabledLayerCount = static_cast<uint32>(RAL::Vulkan::InstanceLayers.size());
        CreateInfo.ppEnabledLayerNames = RAL::Vulkan::InstanceLayers.data();
    }

    vkCreateInstance(&CreateInfo, nullptr, &this->VkContext.Instance);
}

void FVulkanRALDevice::InternalSelectPhysicalDevice()
{
    uint32 DeviceCount = 0;
    {
        vkEnumeratePhysicalDevices(this->VkContext.Instance, &DeviceCount, nullptr);
    }
    std::vector<VkPhysicalDevice> Devices(DeviceCount);
    {
        vkEnumeratePhysicalDevices(this->VkContext.Instance, &DeviceCount, Devices.data());
    }
    for (const VkPhysicalDevice& Device : Devices)
    {
        VkPhysicalDeviceProperties Props;
        vkGetPhysicalDeviceProperties(Device, &Props);
        // Prefer pick discrete gpu
        if (Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            this->VkContext.PhysicalDevice = Device;
            break;
        }
    }
    if (this->VkContext.PhysicalDevice == VK_NULL_HANDLE)
    {
        this->VkContext.PhysicalDevice = Devices[0];
    }

    uint32 QueueFamilyCount = 0;
    {
        vkGetPhysicalDeviceQueueFamilyProperties(this->VkContext.PhysicalDevice, &QueueFamilyCount, nullptr);
    }
    std::vector<VkQueueFamilyProperties> QueueFamilies(QueueFamilyCount);
    {
        vkGetPhysicalDeviceQueueFamilyProperties(this->VkContext.PhysicalDevice, &QueueFamilyCount, QueueFamilies.data());
    }
    for (uint32 i = 0; i < QueueFamilyCount; i++)
    {
        if (QueueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            this->VkContext.GraphicsFamilyIndex = i;
            break;
        }
    }
}

void FVulkanRALDevice::InternalCreateLogicalDevice()
{
    float QueuePriority = 1.f;
    VkDeviceQueueCreateInfo QueueInfo{};
    {
        QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        QueueInfo.queueFamilyIndex = this->VkContext.GraphicsFamilyIndex;
        QueueInfo.queueCount = 1;
        QueueInfo.pQueuePriorities = &QueuePriority;
    }
    // Enable bindless feature for default
    VkPhysicalDeviceDescriptorIndexingFeatures IndexingFeatures{};
    {
        IndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        IndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE; // Allow array has hollow
        IndexingFeatures.runtimeDescriptorArray = VK_TRUE; // Allow runtime dynamic array size
        IndexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE; // Allow update after binding
    }
    VkPhysicalDeviceFeatures2 DeviceFeatures; // kodak: why 2 ???
    {
        DeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        DeviceFeatures.pNext = &IndexingFeatures;
        DeviceFeatures.features.samplerAnisotropy = VK_TRUE;
    }
    VkDeviceCreateInfo DeviceInfo{};
    {
        DeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        DeviceInfo.pNext = &DeviceFeatures;
        DeviceInfo.queueCreateInfoCount = 1; 
        DeviceInfo.pQueueCreateInfos = &QueueInfo;
        DeviceInfo.enabledExtensionCount = static_cast<uint32>(RAL::Vulkan::DeviceExtensions.size());
        DeviceInfo.ppEnabledExtensionNames = RAL::Vulkan::DeviceExtensions.data();
    }
    vkCreateDevice(this->VkContext.PhysicalDevice, &DeviceInfo, nullptr, &this->VkContext.LogicalDevice);
}

FRALSwapchain* FVulkanRALDevice::InternalCreateSwapchain(const FRALSwapchainDesc& InDesc)
{
    // TODO: kodak Memory manage
    FVulkanRALSwapchain* Result = new FVulkanRALSwapchain(this, InDesc);
    return Result;
}


void FVulkanRALDevice::InternalSetupBindlessHeap()
{
    // A large binding slot, store all Texture2D
    VkDescriptorSetLayoutBinding Binding{};
    {
        Binding.binding = 0;
        Binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Binding.descriptorCount = RAL::Vulkan::MaxBindlessDescriptorCount;
        Binding.stageFlags = VK_SHADER_STAGE_ALL;
    }
    VkDescriptorBindingFlags Flags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo LayoutFlagsInfo{};
    {
        LayoutFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        LayoutFlagsInfo.bindingCount = 1;
        LayoutFlagsInfo.pBindingFlags = &Flags;
    }
    VkDescriptorSetLayoutCreateInfo LayoutInfo{};
    {
        LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        LayoutInfo.pNext = &LayoutFlagsInfo;
        LayoutInfo.bindingCount = 1;
        LayoutInfo.pBindings = &Binding;
        LayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    }
    vkCreateDescriptorSetLayout(this->VkContext.LogicalDevice, &LayoutInfo, nullptr, &this->VkContext.BindlessLayout);

    VkDescriptorPoolSize PoolSize;
    {
        PoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        PoolSize.descriptorCount = RAL::Vulkan::MaxBindlessDescriptorCount;
    }
    VkDescriptorPoolCreateInfo PoolInfo{};
    {
        PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        PoolInfo.maxSets = 1;
        PoolInfo.poolSizeCount = 1;
        PoolInfo.pPoolSizes = &PoolSize;
    }
    vkCreateDescriptorPool(this->VkContext.LogicalDevice, &PoolInfo, nullptr, &this->VkContext.BindlessPool);

    VkDescriptorSetAllocateInfo AllocateInfo{};
    {
        AllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        AllocateInfo.descriptorPool = this->VkContext.BindlessPool;
        AllocateInfo.descriptorSetCount = 1;
        AllocateInfo.pSetLayouts = &this->VkContext.BindlessLayout;
    }
    vkAllocateDescriptorSets(this->VkContext.LogicalDevice, &AllocateInfo, &this->VkContext.BindlessDescriptorSet);
}

FRALSampler* FVulkanRALDevice::CreateSampler(const FRALSamplerDesc& Desc)
{
    return new FVulkanRALSampler(this, Desc);
}

FRALBindGroupLayout* FVulkanRALDevice::CreateBindGroupLayout(const FRALBindGroupLayoutDesc& Desc)
{
    return new FVulkanRALBindGroupLayout(this, Desc);
}

FRALBindGroup* FVulkanRALDevice::CreateBindGroup(const FRALBindGroupDesc& Desc)
{
    return new FVulkanRALBindGroup(this, Desc);
}
