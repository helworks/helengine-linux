#pragma once
#include "vulkan_surface_source.hpp"
#include <SDL3/SDL_vulkan.h>
#include <stdexcept>
#include <string>

namespace helengine::linux {
/// <summary>Adapts one borrowed SDL window to the platform-independent Vulkan surface ownership contract.</summary>
class SdlVulkanSurfaceSource final : public helengine::vulkan::VulkanSurfaceSource {
    /// <summary>Borrows a live SDL window which must outlive its Vulkan context.</summary>
    SDL_Window* Window;
public:
    /// <summary>Rejects a missing window before any Vulkan instance can borrow this provider.</summary>
    explicit SdlVulkanSurfaceSource(SDL_Window* window):Window(window) {
        if(!Window) throw std::invalid_argument("SDL Vulkan surface requires a live window.");
    }
    /// <summary>Creates the actual window-system surface through SDL, preserving its original failure detail.</summary>
    VkSurfaceKHR Create(VkInstance instance) override {
        VkSurfaceKHR surface=VK_NULL_HANDLE;
        if(!SDL_Vulkan_CreateSurface(Window,instance,nullptr,&surface)) throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ")+SDL_GetError());
        return surface;
    }
    /// <summary>Releases the SDL-created surface before the borrowed window is destroyed.</summary>
    void Destroy(VkInstance instance,VkSurfaceKHR surface) noexcept override { SDL_Vulkan_DestroySurface(instance,surface,nullptr); }
};
}
