#include "sdl_host.hpp"
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <stdexcept>

namespace helengine::linux {
SdlHost::SdlHost(const char* title, int width, int height, bool validation,VulkanContext* sharedContext,bool transparent,bool noninteractive) : Transparent(transparent) {
    try {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { throw std::runtime_error(std::string("SDL_Init(video/gamepad): ") + SDL_GetError()); }
        VideoInitialized = true;
        Window = SDL_CreateWindow(title,width,height,SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN | ((transparent || noninteractive) ? SDL_WINDOW_TRANSPARENT : 0) | (noninteractive ? SDL_WINDOW_NOT_FOCUSABLE : 0));
        if (!Window) { throw std::runtime_error(std::string("SDL_CreateWindow(Vulkan): ") + SDL_GetError()); }
        Uint32 count = 0; const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        if (!extensions || !count) { throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions: ") + SDL_GetError()); }
        if (sharedContext) {
            Context=sharedContext;
            if (!SDL_Vulkan_CreateSurface(Window,Context->GetInstance(),nullptr,&Surface)) { throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ")+SDL_GetError()); }
            OwnsSurface=true;
            VkBool32 supported=VK_FALSE; VulkanContext::Require(vkGetPhysicalDeviceSurfaceSupportKHR(Context->GetPhysicalDevice(),Context->GetPresentQueueFamily(),Surface,&supported),"vkGetPhysicalDeviceSurfaceSupportKHR(secondary)");
            if (!supported) { throw std::runtime_error("Shared Vulkan presentation queue cannot present the additional output."); }
        } else {
            OwnedContext=std::make_unique<VulkanContext>(std::span<const char* const>(extensions,count),validation,VK_API_VERSION_1_2,Window); Context=OwnedContext.get(); Surface=Context->GetSurface();
        }
        if (noninteractive) {
            SDL_Surface* shape=SDL_CreateSurface(1,1,SDL_PIXELFORMAT_RGBA32);
            if (!shape) { throw std::runtime_error(std::string("SDL_CreateSurface(empty input region): ")+SDL_GetError()); }
            const bool cleared=SDL_FillSurfaceRect(shape,nullptr,0); const bool shaped=cleared && SDL_SetWindowShape(Window,shape); SDL_DestroySurface(shape);
            if (!shaped) { throw std::runtime_error(std::string("SDL_SetWindowShape(empty input region): ")+SDL_GetError()); }
        }
        if (!SDL_ShowWindow(Window)) { throw std::runtime_error(std::string("SDL_ShowWindow: ")+SDL_GetError()); }
        VkSemaphoreCreateInfo semaphore {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VulkanContext::Require(vkCreateSemaphore(Context->GetDevice(),&semaphore,nullptr,&Acquired),"vkCreateSemaphore(acquire)");
        VulkanContext::Require(vkCreateSemaphore(Context->GetDevice(),&semaphore,nullptr,&Ready),"vkCreateSemaphore(present)");
        RecreateSwapchain();
    } catch (...) { Release(); throw; }
}

SdlHost::~SdlHost() { Release(); }

void SdlHost::Release() noexcept {
    if (Context) {
        VkDevice device = Context->GetDevice(); vkDeviceWaitIdle(device);
        if (Swapchain) { vkDestroySwapchainKHR(device,Swapchain,nullptr); Swapchain = VK_NULL_HANDLE; }
        if (Ready) { vkDestroySemaphore(device,Ready,nullptr); Ready = VK_NULL_HANDLE; }
        if (Acquired) { vkDestroySemaphore(device,Acquired,nullptr); Acquired = VK_NULL_HANDLE; }
        if (OwnsSurface && Surface) { vkDestroySurfaceKHR(Context->GetInstance(),Surface,nullptr); }
        Surface=VK_NULL_HANDLE; OwnsSurface=false; OwnedContext.reset(); Context=nullptr;
    }
    if (Window) { SDL_DestroyWindow(Window); Window = nullptr; }
    if (VideoInitialized) { SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD); VideoInitialized = false; }
}

VkExtent2D SdlHost::GetDrawableSize() const {
    if (SDL_GetWindowFlags(Window) & SDL_WINDOW_MINIMIZED) { return {0,0}; }
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(Window,&width,&height)) { throw std::runtime_error(std::string("SDL_GetWindowSizeInPixels: ") + SDL_GetError()); }
    return {static_cast<uint32_t>(std::max(width,0)),static_cast<uint32_t>(std::max(height,0))};
}

bool SdlHost::PollEvent(SDL_Event& event) {
    auto extent = GetDrawableSize();
    if (!extent.width || !extent.height) { return SDL_WaitEventTimeout(&event,50); }
    return SDL_PollEvent(&event);
}

void SdlHost::SetWindowSize(int width, int height) {
    if (width <= 0 || height <= 0) { throw std::invalid_argument("Logical SDL window size must be positive; minimization is a separate lifecycle event."); }
    if (!SDL_SetWindowSize(Window,width,height)) { throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError()); }
}

bool SdlHost::RecreateSwapchain() {
    auto drawable = GetDrawableSize(); if (!drawable.width || !drawable.height) { return false; }
    VkSurfaceCapabilitiesKHR capabilities {};
    VulkanContext::Require(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Context->GetPhysicalDevice(),Surface,&capabilities),"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) { throw std::runtime_error("SDL Vulkan surface does not support transfer-destination swapchain images."); }
    Extent = capabilities.currentExtent.width == UINT32_MAX ? VkExtent2D{std::clamp(drawable.width,capabilities.minImageExtent.width,capabilities.maxImageExtent.width),std::clamp(drawable.height,capabilities.minImageExtent.height,capabilities.maxImageExtent.height)} : capabilities.currentExtent;
    uint32_t count = 0;
    VulkanContext::Require(vkGetPhysicalDeviceSurfaceFormatsKHR(Context->GetPhysicalDevice(),Surface,&count,nullptr),"vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    if (!count) { throw std::runtime_error("SDL Vulkan surface has no supported color format."); }
    std::vector<VkSurfaceFormatKHR> formats(count);
    VulkanContext::Require(vkGetPhysicalDeviceSurfaceFormatsKHR(Context->GetPhysicalDevice(),Surface,&count,formats.data()),"vkGetPhysicalDeviceSurfaceFormatsKHR");
    const auto selected = std::find_if(formats.begin(),formats.end(),[](const auto& format) { return format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; });
    if (selected == formats.end()) { throw std::runtime_error("SDL Vulkan surface requires unsupported swapchain format; BGRA8_UNORM/sRGB is required."); }
    VkFormatProperties formatProperties {}; vkGetPhysicalDeviceFormatProperties(Context->GetPhysicalDevice(),selected->format,&formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) { throw std::runtime_error("Swapchain format cannot receive Vulkan image blits."); }
    uint32_t imageCount = capabilities.minImageCount + 1; if (capabilities.maxImageCount) { imageCount = std::min(imageCount,capabilities.maxImageCount); }
    VkSwapchainCreateInfoKHR create {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; create.surface = Surface; create.minImageCount = imageCount; create.imageFormat = selected->format; create.imageColorSpace = selected->colorSpace;
    create.imageExtent = Extent; create.imageArrayLayers = 1; create.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT; create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; create.preTransform = capabilities.currentTransform;
    const uint32_t families[] {Context->GetQueueFamily(),Context->GetPresentQueueFamily()};
    if (families[0]!=families[1]) { create.imageSharingMode=VK_SHARING_MODE_CONCURRENT; create.queueFamilyIndexCount=2; create.pQueueFamilyIndices=families; }
    if (Transparent) {
        if (!(capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)) { throw std::runtime_error("Transparent output requires supported premultiplied-alpha Vulkan composition."); }
        create.compositeAlpha=VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    } else {
        if (!(capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) { throw std::runtime_error("Opaque output requires opaque Vulkan composition."); }
        create.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    create.presentMode = VK_PRESENT_MODE_FIFO_KHR; create.clipped = VK_TRUE; create.oldSwapchain = Swapchain;
    VulkanContext::Require(vkDeviceWaitIdle(Context->GetDevice()),"vkDeviceWaitIdle(swapchain)");
    VkSwapchainKHR replacement = VK_NULL_HANDLE;
    VulkanContext::Require(vkCreateSwapchainKHR(Context->GetDevice(),&create,nullptr,&replacement),"vkCreateSwapchainKHR");
    if (Swapchain) { vkDestroySwapchainKHR(Context->GetDevice(),Swapchain,nullptr); }
    Swapchain = replacement;
    VulkanContext::Require(vkGetSwapchainImagesKHR(Context->GetDevice(),Swapchain,&count,nullptr),"vkGetSwapchainImagesKHR(count)"); Images.resize(count);
    VulkanContext::Require(vkGetSwapchainImagesKHR(Context->GetDevice(),Swapchain,&count,Images.data()),"vkGetSwapchainImagesKHR"); return true;
}

bool SdlHost::Present(VulkanRenderer2D& renderer) {
    auto drawable = GetDrawableSize(); if (!drawable.width || !drawable.height) { SDL_Delay(16); return false; }
    if (!Swapchain || drawable.width != Extent.width || drawable.height != Extent.height) { if (!RecreateSwapchain()) { return false; } }
    uint32_t image = 0;
    const VkResult acquire = vkAcquireNextImageKHR(Context->GetDevice(),Swapchain,UINT64_MAX,Acquired,VK_NULL_HANDLE,&image);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { RecreateSwapchain(); return false; }
    if (acquire != VK_SUBOPTIMAL_KHR) { VulkanContext::Require(acquire,"vkAcquireNextImageKHR"); }
    VkCommandBuffer commands = Context->BeginCommands();
    VkImageMemoryBarrier barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = Images[image]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkImageBlit blit {}; blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; blit.srcOffsets[1] = {static_cast<int32_t>(renderer.GetWidth()),static_cast<int32_t>(renderer.GetHeight()),1}; blit.dstOffsets[1] = {static_cast<int32_t>(Extent.width),static_cast<int32_t>(Extent.height),1};
    vkCmdBlitImage(commands,renderer.GetOutputImage(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,Images[image],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    Context->SubmitAndWait(commands,Acquired,Ready);
    VkPresentInfoKHR present {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; present.waitSemaphoreCount = 1; present.pWaitSemaphores = &Ready; present.swapchainCount = 1; present.pSwapchains = &Swapchain; present.pImageIndices = &image;
    const VkResult result = vkQueuePresentKHR(Context->GetPresentQueue(),&present);
    VulkanContext::Require(vkQueueWaitIdle(Context->GetPresentQueue()),"vkQueueWaitIdle(present semaphore reuse)");
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR) { RecreateSwapchain(); return false; }
    VulkanContext::Require(result,"vkQueuePresentKHR"); return true;
}
}
