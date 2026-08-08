#include "CoreMinimal.h"
#include "Vulkan/VulkanRAL.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace LE
{

LE_DECLARE_LOG_CATEGORY(LogRAL);

namespace
{
static const char* const GVkKhrPortabilitySubsetExtensionName = "VK_KHR_portability_subset";

bool HasDeviceExtension(VkPhysicalDevice PhysicalDevice, const char* ExtensionName)
{
    if (PhysicalDevice == VK_NULL_HANDLE || ExtensionName == nullptr)
    {
        return false;
    }

    uint32 ExtensionCount = 0;
    VkResult Result = vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &ExtensionCount, nullptr);
    if (Result != VK_SUCCESS || ExtensionCount == 0)
    {
        return false;
    }

    LE::Array<VkExtensionProperties> ExtensionProps;
    ExtensionProps.Resize(ExtensionCount);
    Result = vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &ExtensionCount, ExtensionProps.Data());
    if (Result != VK_SUCCESS)
    {
        return false;
    }

    for (const VkExtensionProperties& Ext : ExtensionProps)
    {
        if (strcmp(Ext.extensionName, ExtensionName) == 0)
        {
            return true;
        }
    }

    return false;
}

#if LE_RAL_ENABLE_VALIDATION
const char* ValidationSeverityToText(VkDebugUtilsMessageSeverityFlagBitsEXT Severity)
{
    if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   return "ERROR";
    if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) return "WARN";
    if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)    return "INFO";
    return "VERBOSE";
}

LE::LogLevel ValidationSeverityToLogLevel(VkDebugUtilsMessageSeverityFlagBitsEXT Severity)
{
    if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   return LE::LogLevel::Error;
    if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) return LE::LogLevel::Warn;
    if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)    return LE::LogLevel::Info;
    return LE::LogLevel::Trace;
}

LE::String ValidationTypeToText(VkDebugUtilsMessageTypeFlagsEXT TypeFlags)
{
    LE::String Text;
    auto AppendType = [&Text](const char* const Name)
    {
        if (!Text.IsEmpty()) { Text.Append("|"); }
        Text.Append(Name);
    };
    if (TypeFlags & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)     AppendType("General");
    if (TypeFlags & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)  AppendType("Validation");
    if (TypeFlags & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) AppendType("Performance");
    return Text.IsEmpty() ? LE::String("Unknown") : Text;
}

LE::String ValidationObjectsToText(const VkDebugUtilsMessengerCallbackDataEXT* CallbackData)
{
    if (CallbackData == nullptr || CallbackData->objectCount == 0 || CallbackData->pObjects == nullptr)
    {
        return "Objects: none";
    }

    LE::String Text("Objects(");
    char Buffer[64]{};
    std::snprintf(Buffer, sizeof(Buffer), "%u):", CallbackData->objectCount);
    Text.Append(Buffer);
    for (uint32 i = 0; i < CallbackData->objectCount; ++i)
    {
        const VkDebugUtilsObjectNameInfoEXT& Obj = CallbackData->pObjects[i];
        std::snprintf(Buffer, sizeof(Buffer), " [#%u type=%u handle=0x%llx", i,
            static_cast<uint32>(Obj.objectType),
            static_cast<unsigned long long>(Obj.objectHandle));
        Text.Append(Buffer);
        if (Obj.pObjectName != nullptr)
        {
            Text.Append(" name=");
            Text.Append(Obj.pObjectName);
        }
        Text.Append("]");
    }
    return Text;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanValidationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT MessageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT MessageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    (void)pUserData;

    const char* MessageIdName = (pCallbackData != nullptr && pCallbackData->pMessageIdName != nullptr)
        ? pCallbackData->pMessageIdName
        : "UnknownMessageId";
    const int32 MessageIdNumber = (pCallbackData != nullptr) ? pCallbackData->messageIdNumber : 0;
    const char* MessageText = (pCallbackData != nullptr && pCallbackData->pMessage != nullptr)
        ? pCallbackData->pMessage
        : "No validation message.";

    const LE::String TypeText = ValidationTypeToText(MessageType);
    const LE::String ObjectsText = ValidationObjectsToText(pCallbackData);

    switch (ValidationSeverityToLogLevel(MessageSeverity))
    {
    case LE::LogLevel::Error:
        LE_LOG(LogRAL, Error, "[VK Validation][{}][{}] {}({})\nMessage: {}\n{}",
            ValidationSeverityToText(MessageSeverity), TypeText.Data(), MessageIdName, MessageIdNumber, MessageText, ObjectsText.Data());
        break;
    case LE::LogLevel::Warn:
        LE_LOG(LogRAL, Warn, "[VK Validation][{}][{}] {}({})\nMessage: {}\n{}",
            ValidationSeverityToText(MessageSeverity), TypeText.Data(), MessageIdName, MessageIdNumber, MessageText, ObjectsText.Data());
        break;
    case LE::LogLevel::Info:
        LE_LOG(LogRAL, Info, "[VK Validation][{}][{}] {}({})\nMessage: {}\n{}",
            ValidationSeverityToText(MessageSeverity), TypeText.Data(), MessageIdName, MessageIdNumber, MessageText, ObjectsText.Data());
        break;
    default:
        LE_LOG(LogRAL, Trace, "[VK Validation][{}][{}] {}({})\nMessage: {}\n{}",
            ValidationSeverityToText(MessageSeverity), TypeText.Data(), MessageIdName, MessageIdNumber, MessageText, ObjectsText.Data());
        break;
    }

    return VK_FALSE;
}
#endif
}

FVulkanRALDevice::FVulkanRALDevice()
{
    LE_LOG(LogRAL, Info, "Initializing Vulkan RAL device...");

	this->InternalCreateInstance();
    if (this->VkContext.Instance == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Vulkan instance creation failed.");
        return;
    }
    this->InternalSetupValidationMessenger();

	this->InternalSelectPhysicalDevice();
    if (this->VkContext.PhysicalDevice == VK_NULL_HANDLE || this->VkContext.GraphicsFamilyIndex == static_cast<uint32>(-1))
    {
        LE_LOG(LogRAL, Error, "Vulkan physical device selection failed.");
        return;
    }

	this->InternalCreateLogicalDevice();
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Vulkan logical device creation failed.");
        return;
    }

	this->InternalSetupBindlessHeap();

	// Create graphics queue
	this->GraphicsQueue = new FVulkanRALQueue(this, this->VkContext.GraphicsFamilyIndex, 0);
    if (this->GraphicsQueue == nullptr || static_cast<FVulkanRALQueue*>(this->GraphicsQueue)->Handle == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "Failed to create graphics queue (family={}, index=0).", this->VkContext.GraphicsFamilyIndex);
    }
    else
    {
        LE_LOG(LogRAL, Info, "Vulkan RAL device initialized successfully. GraphicsFamilyIndex={}, BindlessSupported={}",
            this->VkContext.GraphicsFamilyIndex, this->VkContext.bBindlessSupported);
    }
}

FVulkanRALDevice::~FVulkanRALDevice()
{
    LE_LOG(LogRAL, Info, "Destroying Vulkan RAL device...");

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
        this->InternalDestroyValidationMessenger();
		vkDestroyInstance(this->VkContext.Instance, nullptr);
		this->VkContext.Instance = VK_NULL_HANDLE;
	}

    LE_LOG(LogRAL, Info, "Vulkan RAL device destroyed.");
}

void FVulkanRALDevice::InternalCreateInstance()
{
    uint32 LoaderApiVersion = VK_API_VERSION_1_0;
    const auto EnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (EnumerateInstanceVersion != nullptr)
    {
        uint32 ReportedApiVersion = VK_API_VERSION_1_0;
        if (EnumerateInstanceVersion(&ReportedApiVersion) == VK_SUCCESS)
        {
            LoaderApiVersion = ReportedApiVersion;
        }
    }

    const uint32 RequestedApiVersion = LoaderApiVersion >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;

    VkApplicationInfo AppInfo{};
    {
        AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        AppInfo.pApplicationName = "Limitless Engine"; // TODO: kodak
        AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0); // TODO: kodak
        AppInfo.pEngineName = "Limitless RAL"; // TODO: kodak
        AppInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0); // TODO: kodak
        AppInfo.apiVersion = RequestedApiVersion;
    }
    this->VkContext.InstanceApiVersion = RequestedApiVersion;

    VkInstanceCreateInfo CreateInfo{};
    {
        CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        CreateInfo.pApplicationInfo = &AppInfo;
        CreateInfo.enabledExtensionCount = RAL::Vulkan::InstanceExtensionCount;
        CreateInfo.ppEnabledExtensionNames = RAL::Vulkan::InstanceExtensions;
        CreateInfo.enabledLayerCount = RAL::Vulkan::InstanceLayerCount;
        CreateInfo.ppEnabledLayerNames = RAL::Vulkan::InstanceLayers;
#if PLATFORM_MAC
        CreateInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    }

    LE::String ExtensionText;
    for (uint32 i = 0; i < CreateInfo.enabledExtensionCount; ++i)
    {
        if (i > 0)
        {
            ExtensionText.Append(", ");
        }
        ExtensionText.Append(CreateInfo.ppEnabledExtensionNames[i] != nullptr ? CreateInfo.ppEnabledExtensionNames[i] : "<null>");
    }
    LE_LOG(LogRAL, Info, "Creating Vulkan instance. Flags=0x{:x}, Extensions=[{}], Layers={}",
        static_cast<uint32>(CreateInfo.flags), ExtensionText.Data(), CreateInfo.enabledLayerCount);

    VkResult Result = vkCreateInstance(&CreateInfo, nullptr, &this->VkContext.Instance);
    if (Result == VK_ERROR_LAYER_NOT_PRESENT && CreateInfo.enabledLayerCount > 0)
    {
        LE_LOG(LogRAL, Warn, "Validation layer not present, retrying vkCreateInstance without validation layers.");
        // Retry without validation layers if they are unavailable in current runtime.
        CreateInfo.enabledLayerCount = 0;
        CreateInfo.ppEnabledLayerNames = nullptr;
        Result = vkCreateInstance(&CreateInfo, nullptr, &this->VkContext.Instance);
    }
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateInstance failed. VkResult={}", static_cast<int32>(Result));
        this->VkContext.Instance = VK_NULL_HANDLE;
        this->VkContext.InstanceApiVersion = VK_API_VERSION_1_0;
        return;
    }

    LE_LOG(LogRAL, Info, "Vulkan instance created. LoaderApiVersion={}.{}.{}, RequestedApiVersion={}.{}.{}",
        VK_VERSION_MAJOR(LoaderApiVersion), VK_VERSION_MINOR(LoaderApiVersion), VK_VERSION_PATCH(LoaderApiVersion),
        VK_VERSION_MAJOR(AppInfo.apiVersion), VK_VERSION_MINOR(AppInfo.apiVersion), VK_VERSION_PATCH(AppInfo.apiVersion));
}

void FVulkanRALDevice::InternalSetupValidationMessenger()
{
#if LE_RAL_ENABLE_VALIDATION
    if (this->VkContext.Instance == VK_NULL_HANDLE)
    {
        return;
    }

    auto CreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(this->VkContext.Instance, "vkCreateDebugUtilsMessengerEXT"));
    if (CreateDebugUtilsMessengerEXT == nullptr)
    {
        LE_LOG(LogRAL, Warn, "vkCreateDebugUtilsMessengerEXT is unavailable. Validation callback will not be installed.");
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT CreateInfo{};
    {
        CreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        // Validation output policy: only emit hard errors.
        CreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        CreateInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        CreateInfo.pfnUserCallback = VulkanValidationCallback;
        CreateInfo.pUserData = this;
    }

    VkResult Result = CreateDebugUtilsMessengerEXT(this->VkContext.Instance, &CreateInfo, nullptr, &this->VkContext.DebugMessenger);
    if (Result != VK_SUCCESS)
    {
        this->VkContext.DebugMessenger = VK_NULL_HANDLE;
        LE_LOG(LogRAL, Warn, "Failed to create Vulkan validation messenger. VkResult={}", static_cast<int32>(Result));
        return;
    }

    LE_LOG(LogRAL, Info, "Vulkan validation messenger installed.");
#endif
}

void FVulkanRALDevice::InternalDestroyValidationMessenger()
{
#if LE_RAL_ENABLE_VALIDATION
    if (this->VkContext.Instance == VK_NULL_HANDLE || this->VkContext.DebugMessenger == VK_NULL_HANDLE)
    {
        return;
    }

    auto DestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(this->VkContext.Instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (DestroyDebugUtilsMessengerEXT == nullptr)
    {
        LE_LOG(LogRAL, Warn, "vkDestroyDebugUtilsMessengerEXT is unavailable. Skipping validation messenger destroy.");
        this->VkContext.DebugMessenger = VK_NULL_HANDLE;
        return;
    }

    DestroyDebugUtilsMessengerEXT(this->VkContext.Instance, this->VkContext.DebugMessenger, nullptr);
    this->VkContext.DebugMessenger = VK_NULL_HANDLE;
    LE_LOG(LogRAL, Info, "Vulkan validation messenger destroyed.");
#endif
}

void FVulkanRALDevice::InternalSelectPhysicalDevice()
{
    uint32 DeviceCount = 0;
    VkResult Result = VK_SUCCESS;
    {
        Result = vkEnumeratePhysicalDevices(this->VkContext.Instance, &DeviceCount, nullptr);
    }
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkEnumeratePhysicalDevices(count) failed. VkResult={}", static_cast<int32>(Result));
        this->VkContext.PhysicalDevice = VK_NULL_HANDLE;
        this->VkContext.GraphicsFamilyIndex = static_cast<uint32>(-1);
        return;
    }
    if (DeviceCount == 0)
    {
        LE_LOG(LogRAL, Error, "No Vulkan physical device found.");
        this->VkContext.PhysicalDevice = VK_NULL_HANDLE;
        this->VkContext.GraphicsFamilyIndex = static_cast<uint32>(-1);
        return;
    }
    LE::Array<VkPhysicalDevice> Devices;
    Devices.Resize(DeviceCount);
    {
        Result = vkEnumeratePhysicalDevices(this->VkContext.Instance, &DeviceCount, Devices.Data());
    }
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkEnumeratePhysicalDevices(list) failed. VkResult={}", static_cast<int32>(Result));
        this->VkContext.PhysicalDevice = VK_NULL_HANDLE;
        this->VkContext.GraphicsFamilyIndex = static_cast<uint32>(-1);
        return;
    }
    for (const VkPhysicalDevice& Device : Devices)
    {
        VkPhysicalDeviceProperties Props{};
        vkGetPhysicalDeviceProperties(Device, &Props);
        LE_LOG(LogRAL, Info, "Detected Vulkan GPU: {} (type={}, api={}.{}.{})",
            Props.deviceName,
            static_cast<uint32>(Props.deviceType),
            VK_VERSION_MAJOR(Props.apiVersion),
            VK_VERSION_MINOR(Props.apiVersion),
            VK_VERSION_PATCH(Props.apiVersion));
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
        LE_LOG(LogRAL, Warn, "No discrete GPU found. Fallback to first available physical device.");
    }

    uint32 QueueFamilyCount = 0;
    {
        vkGetPhysicalDeviceQueueFamilyProperties(this->VkContext.PhysicalDevice, &QueueFamilyCount, nullptr);
    }
    LE::Array<VkQueueFamilyProperties> QueueFamilies;
    QueueFamilies.Resize(QueueFamilyCount);
    {
        vkGetPhysicalDeviceQueueFamilyProperties(this->VkContext.PhysicalDevice, &QueueFamilyCount, QueueFamilies.Data());
    }
    for (uint32 i = 0; i < QueueFamilyCount; i++)
    {
        if (QueueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            this->VkContext.GraphicsFamilyIndex = i;
            break;
        }
    }

    if (this->VkContext.GraphicsFamilyIndex == static_cast<uint32>(-1))
    {
        LE_LOG(LogRAL, Error, "Failed to find graphics queue family.");
        return;
    }

    VkPhysicalDeviceProperties SelectedProps{};
    vkGetPhysicalDeviceProperties(this->VkContext.PhysicalDevice, &SelectedProps);
    LE_LOG(LogRAL, Info, "Selected Vulkan GPU: {} (graphicsFamily={})",
        SelectedProps.deviceName, this->VkContext.GraphicsFamilyIndex);
}

void FVulkanRALDevice::InternalCreateLogicalDevice()
{
    if (this->VkContext.PhysicalDevice == VK_NULL_HANDLE || this->VkContext.GraphicsFamilyIndex == static_cast<uint32>(-1))
    {
        LE_LOG(LogRAL, Error, "InternalCreateLogicalDevice called with invalid physical device or queue family.");
        this->VkContext.LogicalDevice = VK_NULL_HANDLE;
        this->VkContext.bBindlessSupported = false;
        return;
    }

    float QueuePriority = 1.f;
    VkDeviceQueueCreateInfo QueueInfo{};
    {
        QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        QueueInfo.queueFamilyIndex = this->VkContext.GraphicsFamilyIndex;
        QueueInfo.queueCount = 1;
        QueueInfo.pQueuePriorities = &QueuePriority;
    }

    VkPhysicalDeviceFeatures2 SupportedFeatures{};
    VkPhysicalDeviceDynamicRenderingFeatures SupportedDynamicRenderingFeatures{};
    VkPhysicalDeviceDescriptorIndexingFeatures SupportedIndexingFeatures{};
    {
        SupportedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        SupportedFeatures.pNext = &SupportedDynamicRenderingFeatures;
        SupportedDynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        SupportedDynamicRenderingFeatures.pNext = &SupportedIndexingFeatures;
        SupportedIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        vkGetPhysicalDeviceFeatures2(this->VkContext.PhysicalDevice, &SupportedFeatures);
    }

    VkPhysicalDeviceProperties PhysicalDeviceProperties{};
    {
        vkGetPhysicalDeviceProperties(this->VkContext.PhysicalDevice, &PhysicalDeviceProperties);
    }

    const bool bSupportsDescriptorIndexingCore =
        VK_VERSION_MAJOR(PhysicalDeviceProperties.apiVersion) > 1 ||
        (VK_VERSION_MAJOR(PhysicalDeviceProperties.apiVersion) == 1 &&
         VK_VERSION_MINOR(PhysicalDeviceProperties.apiVersion) >= 2);

    const bool bSupportsDynamicRenderingCore = this->VkContext.InstanceApiVersion >= VK_API_VERSION_1_3 && PhysicalDeviceProperties.apiVersion >= VK_API_VERSION_1_3;
    const bool bSupportsDynamicRenderingKHR = !bSupportsDynamicRenderingCore && HasDeviceExtension(this->VkContext.PhysicalDevice, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    const bool bCanEnableDynamicRendering = SupportedDynamicRenderingFeatures.dynamicRendering == VK_TRUE && (bSupportsDynamicRenderingCore || bSupportsDynamicRenderingKHR);

    if (!bCanEnableDynamicRendering)
    {
        LE_LOG(LogRAL, Error,
            "Dynamic rendering is required but unsupported. InstanceApi={}.{}.{}, DeviceApi={}.{}.{}, Feature={}, Core={}, KHR={}",
            VK_VERSION_MAJOR(this->VkContext.InstanceApiVersion),
            VK_VERSION_MINOR(this->VkContext.InstanceApiVersion),
            VK_VERSION_PATCH(this->VkContext.InstanceApiVersion),
            VK_VERSION_MAJOR(PhysicalDeviceProperties.apiVersion),
            VK_VERSION_MINOR(PhysicalDeviceProperties.apiVersion),
            VK_VERSION_PATCH(PhysicalDeviceProperties.apiVersion),
            SupportedDynamicRenderingFeatures.dynamicRendering == VK_TRUE,
            bSupportsDynamicRenderingCore,
            bSupportsDynamicRenderingKHR);
        this->VkContext.LogicalDevice = VK_NULL_HANDLE;
        this->VkContext.bBindlessSupported = false;
        this->VkContext.bDynamicRenderingEnabled = false;
        this->VkContext.bDynamicRenderingUsesKHR = false;
        return;
    }

    bool bSupportsDescriptorIndexingExt = false;
    if (!bSupportsDescriptorIndexingCore)
    {
        bSupportsDescriptorIndexingExt = HasDeviceExtension(this->VkContext.PhysicalDevice, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    }

    const bool bCanEnableBindless =
        SupportedIndexingFeatures.descriptorBindingPartiallyBound == VK_TRUE &&
        SupportedIndexingFeatures.runtimeDescriptorArray == VK_TRUE &&
        SupportedIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE &&
        (bSupportsDescriptorIndexingCore || bSupportsDescriptorIndexingExt);
    const bool bSupportsPortabilitySubset = HasDeviceExtension(this->VkContext.PhysicalDevice, GVkKhrPortabilitySubsetExtensionName);

    VkPhysicalDeviceDescriptorIndexingFeatures EnabledIndexingFeatures{};
    {
        EnabledIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        if (bCanEnableBindless)
        {
            EnabledIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
            EnabledIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
            EnabledIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        }
    }

    VkPhysicalDeviceDynamicRenderingFeatures EnabledDynamicRenderingFeatures{};
    {
        EnabledDynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        EnabledDynamicRenderingFeatures.dynamicRendering = VK_TRUE;
        EnabledDynamicRenderingFeatures.pNext = bCanEnableBindless ? &EnabledIndexingFeatures : nullptr;
    }

    VkPhysicalDeviceFeatures2 DeviceFeatures{};
    {
        DeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        DeviceFeatures.pNext = &EnabledDynamicRenderingFeatures;
        DeviceFeatures.features.samplerAnisotropy = SupportedFeatures.features.samplerAnisotropy;
    }

    LE::Array<const char*> EnabledDeviceExtensions;
    EnabledDeviceExtensions.Reserve(RAL::Vulkan::DeviceExtensionCount + 3);
    for (uint32 i = 0; i < RAL::Vulkan::DeviceExtensionCount; ++i)
    {
        EnabledDeviceExtensions.PushBack(RAL::Vulkan::DeviceExtensions[i]);
    }
    if (bCanEnableBindless && !bSupportsDescriptorIndexingCore)
    {
        EnabledDeviceExtensions.PushBack(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    }
    if (bSupportsDynamicRenderingKHR)
    {
        EnabledDeviceExtensions.PushBack(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }
#if PLATFORM_MAC
    if (bSupportsPortabilitySubset)
    {
        EnabledDeviceExtensions.PushBack(GVkKhrPortabilitySubsetExtensionName);
    }
#endif

    VkDeviceCreateInfo DeviceInfo{};
    {
        DeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        DeviceInfo.pNext = &DeviceFeatures;
        DeviceInfo.queueCreateInfoCount = 1;
        DeviceInfo.pQueueCreateInfos = &QueueInfo;
        DeviceInfo.enabledExtensionCount = static_cast<uint32>(EnabledDeviceExtensions.Size());
        DeviceInfo.ppEnabledExtensionNames = EnabledDeviceExtensions.Data();
    }
    const VkResult Result = vkCreateDevice(this->VkContext.PhysicalDevice, &DeviceInfo, nullptr, &this->VkContext.LogicalDevice);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateDevice failed. VkResult={}", static_cast<int32>(Result));
        this->VkContext.LogicalDevice = VK_NULL_HANDLE;
        this->VkContext.bBindlessSupported = false;
        this->VkContext.bDynamicRenderingEnabled = false;
        this->VkContext.bDynamicRenderingUsesKHR = false;
        return;
    }

    this->VkContext.bDynamicRenderingUsesKHR = bSupportsDynamicRenderingKHR;
    const char* BeginRenderingProcName = bSupportsDynamicRenderingKHR ? "vkCmdBeginRenderingKHR" : "vkCmdBeginRendering";
    const char* EndRenderingProcName = bSupportsDynamicRenderingKHR ? "vkCmdEndRenderingKHR" : "vkCmdEndRendering";
    this->VkContext.CmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(
        vkGetDeviceProcAddr(this->VkContext.LogicalDevice, BeginRenderingProcName));
    this->VkContext.CmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(
        vkGetDeviceProcAddr(this->VkContext.LogicalDevice, EndRenderingProcName));
    if (this->VkContext.CmdBeginRendering == nullptr || this->VkContext.CmdEndRendering == nullptr)
    {
        LE_LOG(LogRAL, Error,
            "Dynamic rendering command loading failed. Path={}, BeginProc={}, EndProc={}",
            bSupportsDynamicRenderingKHR ? "KHR" : "Core",
            this->VkContext.CmdBeginRendering != nullptr,
            this->VkContext.CmdEndRendering != nullptr);
        vkDestroyDevice(this->VkContext.LogicalDevice, nullptr);
        this->VkContext.LogicalDevice = VK_NULL_HANDLE;
        this->VkContext.bBindlessSupported = false;
        this->VkContext.bDynamicRenderingEnabled = false;
        this->VkContext.bDynamicRenderingUsesKHR = false;
        this->VkContext.CmdBeginRendering = nullptr;
        this->VkContext.CmdEndRendering = nullptr;
        return;
    }

    this->VkContext.bBindlessSupported = bCanEnableBindless;
    this->VkContext.bDynamicRenderingEnabled = true;
    LE_LOG(LogRAL, Info,
        "Logical device created. DynamicRendering={} (path={}), BindlessSupported={} (coreIndexing={}, extIndexing={}, portabilitySubset={})",
        this->VkContext.bDynamicRenderingEnabled,
        this->VkContext.bDynamicRenderingUsesKHR ? "KHR" : "Core",
        this->VkContext.bBindlessSupported,
        bSupportsDescriptorIndexingCore,
        bSupportsDescriptorIndexingExt,
        bSupportsPortabilitySubset);
}

FRALSwapchain* FVulkanRALDevice::InternalCreateSwapchain(const FRALSwapchainDesc& InDesc)
{
    // TODO: kodak Memory manage
    LE_LOG(LogRAL, Info, "Creating swapchain: {}x{}, vsync={}", InDesc.Width, InDesc.Height, InDesc.bEnableVsync);
    FVulkanRALSwapchain* Result = new FVulkanRALSwapchain(this, InDesc);
    return Result;
}


void FVulkanRALDevice::InternalSetupBindlessHeap()
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE || !this->VkContext.bBindlessSupported)
    {
        LE_LOG(LogRAL, Info, "Skip bindless heap setup. LogicalDeviceValid={}, BindlessSupported={}",
            this->VkContext.LogicalDevice != VK_NULL_HANDLE, this->VkContext.bBindlessSupported);
        return;
    }

    VkPhysicalDeviceDescriptorIndexingProperties DescriptorIndexingProperties{};
    VkPhysicalDeviceProperties2 Properties2{};
    {
        DescriptorIndexingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
        Properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        Properties2.pNext = &DescriptorIndexingProperties;
        vkGetPhysicalDeviceProperties2(this->VkContext.PhysicalDevice, &Properties2);
    }

    uint32 DescriptorCount = RAL::Vulkan::MaxBindlessDescriptorCount;
    DescriptorCount = std::min(DescriptorCount, DescriptorIndexingProperties.maxDescriptorSetUpdateAfterBindSampledImages);
    DescriptorCount = std::min(DescriptorCount, DescriptorIndexingProperties.maxDescriptorSetUpdateAfterBindSamplers);
    DescriptorCount = std::min(DescriptorCount, DescriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindSampledImages);
    DescriptorCount = std::min(DescriptorCount, DescriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindSamplers);
    if (DescriptorCount == 0)
    {
        LE_LOG(LogRAL, Warn, "Bindless descriptor count resolved to 0. Disabling bindless.");
        this->VkContext.bBindlessSupported = false;
        return;
    }

    // A large binding slot, store all Texture2D
    VkDescriptorSetLayoutBinding Binding{};
    {
        Binding.binding = 0;
        Binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Binding.descriptorCount = DescriptorCount;
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
    VkResult Result = vkCreateDescriptorSetLayout(this->VkContext.LogicalDevice, &LayoutInfo, nullptr, &this->VkContext.BindlessLayout);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateDescriptorSetLayout(bindless) failed. VkResult={}", static_cast<int32>(Result));
        this->VkContext.BindlessLayout = VK_NULL_HANDLE;
        this->VkContext.bBindlessSupported = false;
        return;
    }

    VkDescriptorPoolSize PoolSize;
    {
        PoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        PoolSize.descriptorCount = DescriptorCount;
    }
    VkDescriptorPoolCreateInfo PoolInfo{};
    {
        PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        PoolInfo.maxSets = 1;
        PoolInfo.poolSizeCount = 1;
        PoolInfo.pPoolSizes = &PoolSize;
    }
    Result = vkCreateDescriptorPool(this->VkContext.LogicalDevice, &PoolInfo, nullptr, &this->VkContext.BindlessPool);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkCreateDescriptorPool(bindless) failed. VkResult={}", static_cast<int32>(Result));
        vkDestroyDescriptorSetLayout(this->VkContext.LogicalDevice, this->VkContext.BindlessLayout, nullptr);
        this->VkContext.BindlessLayout = VK_NULL_HANDLE;
        this->VkContext.BindlessPool = VK_NULL_HANDLE;
        this->VkContext.bBindlessSupported = false;
        return;
    }

    VkDescriptorSetAllocateInfo AllocateInfo{};
    {
        AllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        AllocateInfo.descriptorPool = this->VkContext.BindlessPool;
        AllocateInfo.descriptorSetCount = 1;
        AllocateInfo.pSetLayouts = &this->VkContext.BindlessLayout;
    }
    Result = vkAllocateDescriptorSets(this->VkContext.LogicalDevice, &AllocateInfo, &this->VkContext.BindlessDescriptorSet);
    if (Result != VK_SUCCESS)
    {
        LE_LOG(LogRAL, Error, "vkAllocateDescriptorSets(bindless) failed. VkResult={}", static_cast<int32>(Result));
        vkDestroyDescriptorPool(this->VkContext.LogicalDevice, this->VkContext.BindlessPool, nullptr);
        vkDestroyDescriptorSetLayout(this->VkContext.LogicalDevice, this->VkContext.BindlessLayout, nullptr);
        this->VkContext.BindlessPool = VK_NULL_HANDLE;
        this->VkContext.BindlessLayout = VK_NULL_HANDLE;
        this->VkContext.BindlessDescriptorSet = VK_NULL_HANDLE;
        this->VkContext.bBindlessSupported = false;
        return;
    }

    LE_LOG(LogRAL, Info, "Bindless heap initialized. DescriptorCount={}", DescriptorCount);
}

FRALSampler* FVulkanRALDevice::CreateSampler(const FRALSamplerDesc& Desc)
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateSampler failed: logical device is null.");
        return nullptr;
    }
    return new FVulkanRALSampler(this, Desc);
}

FRALBindGroupLayout* FVulkanRALDevice::CreateBindGroupLayout(const FRALBindGroupLayoutDesc& Desc)
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateBindGroupLayout failed: logical device is null.");
        return nullptr;
    }
    return new FVulkanRALBindGroupLayout(this, Desc);
}

FRALBindGroup* FVulkanRALDevice::CreateBindGroup(const FRALBindGroupDesc& Desc)
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateBindGroup failed: logical device is null.");
        return nullptr;
    }
    return new FVulkanRALBindGroup(this, Desc);
}

FRALShader* FVulkanRALDevice::CreateShaderFromFile(EShaderStage Stage, const void* Data, uint64 Size, const LE::String& EntryPoint)
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE || Data == nullptr || Size == 0 || (Size % 4) != 0)
    {
        LE_LOG(LogRAL, Error, "CreateShaderFromFile failed: invalid input. LogicalDeviceValid={}, DataValid={}, Size={}",
            this->VkContext.LogicalDevice != VK_NULL_HANDLE, Data != nullptr, Size);
        return nullptr;
    }

    FRALShaderDesc Desc;
    Desc.Stage = Stage;
    Desc.ByteCode = Data;
    Desc.ByteCodeSize = Size;
    Desc.EntryPoint = EntryPoint;

    FVulkanRALShader* Shader = new FVulkanRALShader(this, Desc);
    if (Shader->Module == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateShaderFromFile failed: shader module creation returned null.");
        delete Shader;
        return nullptr;
    }
    LE_LOG(LogRAL, Info, "Shader module created. Stage={}, Size={} bytes", static_cast<uint32>(Stage), Size);
    return Shader;
}

FRALPipeline_Graphics* FVulkanRALDevice::CreateGraphicsPipeline(const FRALPipelineDesc_Graphics& Desc)
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateGraphicsPipeline failed: logical device is null.");
        return nullptr;
    }

    FVulkanRALPipeline_Graphics* Pipeline = new FVulkanRALPipeline_Graphics(this, Desc);
    if (Pipeline->Pipeline == VK_NULL_HANDLE || Pipeline->PipelineLayout == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateGraphicsPipeline failed: invalid Vulkan pipeline handles.");
        delete Pipeline;
        return nullptr;
    }
    LE_LOG(LogRAL, Info, "Graphics pipeline created successfully.");
    return Pipeline;
}

FRALBuffer* FVulkanRALDevice::CreateBuffer(const FRALBufferDesc& Desc)
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateBuffer failed: logical device is null.");
        return nullptr;
    }
    return new FVulkanRALBuffer(this, Desc);
}

FRALTexture* FVulkanRALDevice::CreateTexture(const FRALTextureDesc& Desc)
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateTexture failed: logical device is null.");
        return nullptr;
    }
    return new FVulkanRALTexture(this, Desc);
}

FRALTextureView* FVulkanRALDevice::CreateTextureView(const FRALTextureViewDesc& Desc)
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE || Desc.Texture == nullptr)
    {
        LE_LOG(LogRAL, Error, "CreateTextureView failed: invalid input. LogicalDeviceValid={}, TextureValid={}", this->VkContext.LogicalDevice != VK_NULL_HANDLE, Desc.Texture != nullptr);
        return nullptr;
    }

    FVulkanRALTextureView* TextureView = new FVulkanRALTextureView(this, Desc);
    if (TextureView->View == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateTextureView failed: Vulkan image view creation returned null.");
        delete TextureView;
        return nullptr;
    }
    return TextureView;
}

FRALCommandList* FVulkanRALDevice::CreateCommandList(EQueueType Type)
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateCommandList failed: logical device is null.");
        return nullptr;
    }
	return new FVulkanRALCommandList(this, Type);
}

FRALSwapchain* FVulkanRALDevice::CreateSwapchain(const FRALSwapchainDesc& Desc)
{
    if (this->VkContext.LogicalDevice == VK_NULL_HANDLE || this->VkContext.Instance == VK_NULL_HANDLE || this->VkContext.PhysicalDevice == VK_NULL_HANDLE)
    {
        LE_LOG(LogRAL, Error, "CreateSwapchain failed: Vulkan context is incomplete.");
        return nullptr;
    }
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
    if (Resource == nullptr)
    {
        LE_LOG(LogRAL, Warn, "AllocateBindlessIndex called with null resource.");
    }
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
		FVulkanRALDevice* Device = new FVulkanRALDevice();
        if (Device->VkContext.LogicalDevice == VK_NULL_HANDLE || Device->GetGraphicsQueue() == nullptr)
        {
            LE_LOG(LogRAL, Error, "CreateDevice failed: invalid Vulkan device or graphics queue.");
            delete Device;
            return nullptr;
        }
        LE_LOG(LogRAL, Info, "CreateDevice succeeded.");
        return Device;
	}

	void DestroyResource(FRALResource* Resource) noexcept
	{
		delete Resource;
	}
}

} // namespace LE
