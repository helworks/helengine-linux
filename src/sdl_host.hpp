#pragma once
#include "vulkan_renderer_2d.hpp"
#include <SDL3/SDL.h>
#include <stdexcept>
#include <cmath>

namespace helengine::linux {
/// <summary>Owns one SDL3 Vulkan window, surface and swapchain; platform surface details remain inside SDL.</summary>
class SdlHost final {
public:
    /// <summary>Creates one explicit output; a borrowed sharedContext must outlive this secondary host.</summary>
    SdlHost(const char* title, int width, int height, bool validation = false, VulkanContext* sharedContext = nullptr, bool transparent = false, bool noninteractive = false);
    /// <summary>Synchronizes and destroys owned presentation resources, releasing a device only for its first output.</summary>
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
    /// <summary>Returns the actual SDL display scale so scale-only changes invalidate output generations.</summary>
    float GetDisplayScale() const { const float scale=SDL_GetWindowDisplayScale(Window); if (!std::isfinite(scale) || scale<=0) { throw std::runtime_error("Native output has invalid display scale."); } return scale; }
    /// <summary>Requests a logical SDL window size; the drawable extent is queried separately to respect DPI.</summary>
    void SetWindowSize(int width, int height);
    /// <summary>Blits a completed same-device RGBA target to the swapchain, recovering out-of-date/suboptimal surfaces explicitly.</summary>
    bool Present(VulkanRenderer2D& renderer);
    /// <summary>Rejects presentation progress when target dimensions or the scale captured before rendering no longer match the drawable.</summary>
    bool Present(VulkanRenderer2D& renderer,float renderedScale);
private:
    /// <summary>Recreates the swapchain when drawable extent is nonzero and validates presentation capabilities.</summary>
    bool RecreateSwapchain();
    /// <summary>Releases successfully created presentation resources after waiting for the device.</summary>
    void Release() noexcept;
    /// <summary>Owns the native SDL window.</summary>
    SDL_Window* Window = nullptr;
    /// <summary>Owns the Vulkan instance/device after the window supplies its required instance extensions.</summary>
    std::unique_ptr<VulkanContext> OwnedContext;
    /// <summary>Borrows the first output device; secondary outputs never destroy it.</summary>
    VulkanContext* Context = nullptr;
    /// <summary>Distinguishes secondary surfaces from the first surface owned by VulkanContext.</summary>
    bool OwnsSurface = false;
    /// <summary>Requires nonopaque Vulkan composition for alpha-bearing content.</summary>
    bool Transparent = false;
    /// <summary>Borrows the SDL-created surface owned by Context, selected before physical-device initialization.</summary>
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
