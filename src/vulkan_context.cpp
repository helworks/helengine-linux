#include "vulkan_context.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace helengine::linux {
void VulkanContext::Require(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) { throw std::runtime_error(std::string(operation) + " failed: VkResult=" + std::to_string(result)); }
}

VulkanContext::VulkanContext(std::span<const char* const> extensions, bool validation) {
    try {
        uint32_t loaderVersion = VK_API_VERSION_1_0;
        auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
        if (enumerateVersion) { Require(enumerateVersion(&loaderVersion), "vkEnumerateInstanceVersion"); }
        if (loaderVersion < VK_API_VERSION_1_1) { throw std::runtime_error("Vulkan loader API 1.1 or newer is required."); }
        const char* validationLayer = "VK_LAYER_KHRONOS_validation";
        if (validation) {
            uint32_t count = 0;
            Require(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties(count)");
            std::vector<VkLayerProperties> layers(count);
            Require(vkEnumerateInstanceLayerProperties(&count, layers.data()), "vkEnumerateInstanceLayerProperties");
            if (std::none_of(layers.begin(), layers.end(), [validationLayer](const auto& layer) { return std::strcmp(layer.layerName, validationLayer) == 0; })) {
                throw std::runtime_error("Explicit Vulkan validation requested but VK_LAYER_KHRONOS_validation is unavailable.");
            }
        }
        VkApplicationInfo application {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "HelEngine Linux";
        application.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo create {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create.pApplicationInfo = &application;
        create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        create.enabledLayerCount = validation ? 1 : 0;
        create.ppEnabledLayerNames = validation ? &validationLayer : nullptr;
        Require(vkCreateInstance(&create, nullptr, &Instance), "vkCreateInstance(Vulkan1.1)");
        uint32_t count = 0;
        Require(vkEnumeratePhysicalDevices(Instance, &count, nullptr), "vkEnumeratePhysicalDevices(count)");
        if (count == 0) { throw std::runtime_error("No Vulkan physical device is available."); }
        std::vector<VkPhysicalDevice> devices(count);
        Require(vkEnumeratePhysicalDevices(Instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
        int bestScore = -1;
        for (VkPhysicalDevice candidate : devices) {
            VkPhysicalDeviceProperties properties {};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_1) { continue; }
            uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            for (uint32_t index = 0; index < familyCount; ++index) {
                if (!(families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !families[index].queueCount) { continue; }
                const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
                if (score > bestScore) { bestScore = score; PhysicalDevice = candidate; QueueFamily = index; Properties = properties; }
                break;
            }
        }
        if (PhysicalDevice == VK_NULL_HANDLE) { throw std::runtime_error("No Vulkan 1.1 device exposes a graphics queue."); }
        float priority = 1;
        VkDeviceQueueCreateInfo queue {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = QueueFamily;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo device {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device.queueCreateInfoCount = 1;
        device.pQueueCreateInfos = &queue;
        const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        device.enabledExtensionCount = extensions.empty() ? 0 : 1;
        device.ppEnabledExtensionNames = extensions.empty() ? nullptr : &swapchainExtension;
        Require(vkCreateDevice(PhysicalDevice, &device, nullptr, &Device), "vkCreateDevice(graphics)");
        vkGetDeviceQueue(Device, QueueFamily, 0, &Queue);
        VkCommandPoolCreateInfo pool {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = QueueFamily;
        Require(vkCreateCommandPool(Device, &pool, nullptr, &CommandPool), "vkCreateCommandPool");
    } catch (...) { Release(); throw; }
}

VulkanContext::~VulkanContext() { Release(); }

void VulkanContext::Release() noexcept {
    if (Device) {
        vkDeviceWaitIdle(Device);
        if (CommandPool) { vkDestroyCommandPool(Device, CommandPool, nullptr); }
        vkDestroyDevice(Device, nullptr);
        Device = VK_NULL_HANDLE;
    }
    if (Instance) { vkDestroyInstance(Instance, nullptr); Instance = VK_NULL_HANDLE; }
}

uint32_t VulkanContext::FindMemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties memory {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &memory);
    for (uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        if ((bits & (1U << index)) && (memory.memoryTypes[index].propertyFlags & flags) == flags) { return index; }
    }
    throw std::runtime_error("No Vulkan memory type satisfies flags=" + std::to_string(flags));
}

VkCommandBuffer VulkanContext::BeginCommands() {
    VkCommandBufferAllocateInfo allocate {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = CommandPool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    Require(vkAllocateCommandBuffers(Device, &allocate, &commands), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult result = vkBeginCommandBuffer(commands, &begin);
    if (result != VK_SUCCESS) { vkFreeCommandBuffers(Device, CommandPool, 1, &commands); Require(result, "vkBeginCommandBuffer"); }
    return commands;
}

void VulkanContext::SubmitAndWait(VkCommandBuffer commands) {
    VkFence fence = VK_NULL_HANDLE;
    try {
        Require(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
        VkFenceCreateInfo create {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        Require(vkCreateFence(Device, &create, nullptr, &fence), "vkCreateFence");
        VkSubmitInfo submit {VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &commands;
        Require(vkQueueSubmit(Queue, 1, &submit, fence), "vkQueueSubmit");
        Require(vkWaitForFences(Device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    } catch (...) {
        vkDeviceWaitIdle(Device);
        if (fence) { vkDestroyFence(Device, fence, nullptr); }
        vkFreeCommandBuffers(Device, CommandPool, 1, &commands);
        throw;
    }
    vkDestroyFence(Device, fence, nullptr);
    vkFreeCommandBuffers(Device, CommandPool, 1, &commands);
}
}
