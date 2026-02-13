#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

FVulkanRALDevice::FVulkanRALDevice()
{
	this->InternalCreateInstance();
	this->InternalSelectPhysicalDevice();
	this->InternalCreateLogicalDevice();
	this->InternalSetupBindlessHeap();

	// Create graphics queue
	this->GraphicsQueue = new FVulkanRALQueue(this, this->VkContext.GraphicsFamilyIndex, 0);
}

FVulkanRALDevice::~FVulkanRALDevice()
{
	// Clean up graphics queue
	if (this->GraphicsQueue)
	{
		delete this->GraphicsQueue;
		this->GraphicsQueue = nullptr;
	}

	// Clean up bindless resources
	if (this->VkContext.BindlessDescriptorSet != VK_NULL_HANDLE)
	{
		// Descriptor sets are freed when pool is destroyed, no need to free individually
	}
	if (this->VkContext.BindlessPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(this->VkContext.LogicalDevice, this->VkContext.BindlessPool, nullptr);
		this->VkContext.BindlessPool = VK_NULL_HANDLE;
	}
	if (this->VkContext.BindlessLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(this->VkContext.LogicalDevice, this->VkContext.BindlessLayout, nullptr);
		this->VkContext.BindlessLayout = VK_NULL_HANDLE;
	}

	// Clean up device and instance
	if (this->VkContext.LogicalDevice != VK_NULL_HANDLE)
	{
		vkDestroyDevice(this->VkContext.LogicalDevice, nullptr);
		this->VkContext.LogicalDevice = VK_NULL_HANDLE;
	}
	if (this->VkContext.Instance != VK_NULL_HANDLE)
	{
		vkDestroyInstance(this->VkContext.Instance, nullptr);
		this->VkContext.Instance = VK_NULL_HANDLE;
	}
}

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
        CreateInfo.enabledExtensionCount = RAL::Vulkan::InstanceExtensionCount;
        CreateInfo.ppEnabledExtensionNames = RAL::Vulkan::InstanceExtensions;
        CreateInfo.enabledLayerCount = RAL::Vulkan::InstanceLayerCount;
        CreateInfo.ppEnabledLayerNames = RAL::Vulkan::InstanceLayers;
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
        DeviceInfo.enabledExtensionCount = RAL::Vulkan::DeviceExtensionCount;
        DeviceInfo.ppEnabledExtensionNames = RAL::Vulkan::DeviceExtensions;
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

FRALShader* FVulkanRALDevice::CreateShaderFromFile(EShaderStage Stage, const void* Data, uint64 Size)
{
    FRALShaderDesc Desc;
    Desc.Stage = Stage;
    Desc.ByteCode = Data;
    Desc.ByteCodeSize = Size;
    Desc.EntryPoint = "main";
    return new FVulkanRALShader(this, Desc);
}

FRALPipeline_Graphics* FVulkanRALDevice::CreateGraphicsPipeline(const FRALPipelineDesc_Graphics& Desc)
{
    return new FVulkanRALPipeline_Graphics(this, Desc);
}

FRALBuffer* FVulkanRALDevice::CreateBuffer(const FRALBufferDesc& Desc)
{
    return new FVulkanRALBuffer(this, Desc);
}

FRALTexture* FVulkanRALDevice::CreateTexture(const FRALTextureDesc& Desc)
{
    return new FVulkanRALTexture(this, Desc);
}

FRALCommandList* FVulkanRALDevice::CreateCommandList(EQueueType Type)
{
	return new FVulkanRALCommandList(this, Type);
}

FRALSwapchain* FVulkanRALDevice::CreateSwapchain(const FRALSwapchainDesc& Desc)
{
	return this->InternalCreateSwapchain(Desc);
}

FRALQueue* FVulkanRALDevice::GetGraphicsQueue() const
{
	return this->GraphicsQueue;
}

void* FVulkanRALDevice::GetBindlessHeapGPUDescriptor() const
{
	return reinterpret_cast<void*>(this->VkContext.BindlessDescriptorSet);
}

uint32 FVulkanRALDevice::AllocateBindlessIndex(FRALResource* Resource)
{
	// TODO: kodak - Implement proper bindless index allocation
	// For now, return a placeholder index
	// This needs a proper allocation strategy (free list, etc.)
	static uint32 NextIndex = 0;
	return NextIndex++;
}

// ***********************************************************************************************
// ********************************** Factory Function *******************************************
// ***********************************************************************************************

namespace RAL
{
	FRALDevice* CreateDevice()
	{
		// For now, always create Vulkan device
		// TODO: kodak - Add platform selection logic (D3D12, OpenGL, etc.)
		return new FVulkanRALDevice();
	}
}