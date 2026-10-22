#pragma once
#include "vulkan_renderer_2d.hpp"
#include <SDL3/SDL.h>

namespace helengine::linux {
/// <summary>Owns one SDL3 Vulkan window, surface and swapchain; platform surface details remain inside SDL.</summary>
class SdlHost final {
public:
    /// <summary>Creates a desktop window only when explicitly constructed by a player or isolated host test.</summary>
    SdlHost(const char* title, int width, int height, bool validation = false);
    /// <summary>Synchronizes and destroys swapchain, surface, device and SDL window in dependency order.</summary>
    ~SdlHost();
    /// <summary>Prevents sharing ownership of the SDL window and presentation handles.</summary>
    SdlHost(const SdlHost&) = delete;
    /// <summary>Prevents copied presentation ownership.</summary>
    SdlHost& operator=(const SdlHost&) = delete;
    /// <summary>Returns the graphics context used by both the renderer and presentation queue.</summary>
    VulkanContext& GetContext() { return *Context; }
    /// <summary>Polls SDL input/lifecycle events; a minimized window waits up to 50ms to avoid a busy loop.</summary>
    bool PollEvent(SDL_Event& event);
    /// <summary>Returns current drawable pixel size, preserving zero size while minimized.</summary>
    VkExtent2D GetDrawableSize() const;
    /// <summary>Requests a logical SDL window size; the drawable extent is queried separately to respect DPI.</summary>
    void SetWindowSize(int width, int height);
    /// <summary>Blits a completed same-device RGBA target to the swapchain, recovering out-of-date/suboptimal surfaces explicitly.</summary>
    bool Present(VulkanRenderer2D& renderer);
private:
    /// <summary>Recreates the swapchain when drawable extent is nonzero and validates presentation capabilities.</summary>
    bool RecreateSwapchain();
    /// <summary>Releases successfully created presentation resources after waiting for the device.</summary>
    void Release() noexcept;
    /// <summary>Owns the native SDL window.</summary>
    SDL_Window* Window = nullptr;
    /// <summary>Owns the Vulkan instance/device after the window supplies its required instance extensions.</summary>
    std::unique_ptr<VulkanContext> Context;
    /// <summary>Owns the SDL-created native Vulkan surface.</summary>
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    /// <summary>Owns the current swapchain.</summary>
    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    /// <summary>Owns the acquire semaphore reused only after synchronous queue completion.</summary>
    VkSemaphore Acquired = VK_NULL_HANDLE;
    /// <summary>Owns the presentation-ready semaphore reused after presentation queue idle.</summary>
    VkSemaphore Ready = VK_NULL_HANDLE;
    /// <summary>Borrows swapchain images for transfer destination commands.</summary>
    std::vector<VkImage> Images;
    /// <summary>Stores the negotiated current presentation extent.</summary>
    VkExtent2D Extent {};
    /// <summary>Tracks successful SDL video initialization for partial teardown.</summary>
    bool VideoInitialized = false;
};
}
