#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <span>
#include <string>

namespace helengine::linux {
/// <summary>Owns Vulkan instance, selected graphics device, queue and transient commands; never requests a desktop surface implicitly.</summary>
class VulkanContext final {
public:
    /// <summary>Creates Vulkan 1.1 resources, optionally enabling surface extensions supplied by SDL and explicit validation.</summary>
    explicit VulkanContext(std::span<const char* const> extensions = {}, bool validation = false);
    /// <summary>Waits for owned GPU work, then releases resources in reverse initialization order.</summary>
    ~VulkanContext();
    /// <summary>Prevents duplicating ownership of native Vulkan handles.</summary>
    VulkanContext(const VulkanContext&) = delete;
    /// <summary>Prevents assignment of native Vulkan ownership.</summary>
    VulkanContext& operator=(const VulkanContext&) = delete;
    /// <summary>Exposes the instance for SDL surface creation.</summary>
    VkInstance GetInstance() const { return Instance; }
    /// <summary>Exposes the selected hardware or software physical device for capability queries.</summary>
    VkPhysicalDevice GetPhysicalDevice() const { return PhysicalDevice; }
    /// <summary>Exposes the owned logical device for renderer resource creation.</summary>
    VkDevice GetDevice() const { return Device; }
    /// <summary>Exposes the graphics queue used by this single render thread.</summary>
    VkQueue GetQueue() const { return Queue; }
    /// <summary>Returns the graphics family that must also support presentation when a surface is attached.</summary>
    uint32_t GetQueueFamily() const { return QueueFamily; }
    /// <summary>Returns the selected device properties for truthful device diagnostics.</summary>
    const VkPhysicalDeviceProperties& GetProperties() const { return Properties; }
    /// <summary>Finds a compatible memory type or rejects unsupported requested memory flags.</summary>
    uint32_t FindMemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;
    /// <summary>Allocates and begins one primary command buffer for a synchronous upload/render transaction.</summary>
    VkCommandBuffer BeginCommands();
    /// <summary>Submits commands and waits on a fence before freeing the command buffer or allowing resources to be reused.</summary>
    void SubmitAndWait(VkCommandBuffer commands, VkSemaphore wait = VK_NULL_HANDLE, VkSemaphore signal = VK_NULL_HANDLE);
    /// <summary>Throws a contextual native exception when a Vulkan operation does not succeed.</summary>
    static void Require(VkResult result, const char* operation);
private:
    /// <summary>Releases initialized handles, including partially initialized constructor state.</summary>
    void Release() noexcept;
    /// <summary>Owns the Vulkan loader instance.</summary>
    VkInstance Instance = VK_NULL_HANDLE;
    /// <summary>Borrows the selected physical graphics device from Instance.</summary>
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    /// <summary>Owns the logical device used by all renderer resources.</summary>
    VkDevice Device = VK_NULL_HANDLE;
    /// <summary>Borrows the graphics queue from Device.</summary>
    VkQueue Queue = VK_NULL_HANDLE;
    /// <summary>Owns command buffer allocation for synchronous transfers and draws.</summary>
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    /// <summary>Stores the selected family index without inventing a fallback index.</summary>
    uint32_t QueueFamily = UINT32_MAX;
    /// <summary>Stores actual API/device capabilities for logging and feature checks.</summary>
    VkPhysicalDeviceProperties Properties {};
};
}
