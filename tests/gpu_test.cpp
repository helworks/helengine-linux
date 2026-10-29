#include "vulkan_renderer_2d.hpp"
#include <iostream>
#include <stdexcept>
using namespace helengine::vulkan;

/// <summary>Runs a real offscreen render, then verifies texture sampling, draw order, alpha and clipping through GPU readback.</summary>
int main(int argc, char** argv) {
    try {
        const bool validation = argc == 2 && std::string(argv[1]) == "--validation";
        helengine::vulkan::VulkanContext context({},validation);
        const auto& device = context.GetProperties();
        std::cout << "device=" << device.deviceName << " type=" << device.deviceType
                  << " api=" << VK_VERSION_MAJOR(device.apiVersion) << "." << VK_VERSION_MINOR(device.apiVersion) << '\n';
        helengine::vulkan::VulkanRenderer2D renderer(context, 64, 64);
        const uint8_t white[] {255,255,255,255};
        const auto texture = renderer.CreateTexture(1, 1, white);
        renderer.BeginFrame({0,0,0,1});
        renderer.DrawRect(texture, 0,0,64,64, {1,0,0,1});
        renderer.PushClip(16,16,32,32);
        renderer.DrawRect(texture, 0,0,64,64, {0,0,1,0.5f});
        renderer.PopClip();
        auto pixels = renderer.EndFrameAndReadback();
        const size_t inside = (32 * 64 + 32) * 4;
        const size_t outside = (8 * 64 + 8) * 4;
        if (pixels[outside] != 255 || pixels[outside + 2] != 0) { throw std::runtime_error("GPU scissor rejected: outside pixel was modified."); }
        if (pixels[inside] < 126 || pixels[inside] > 129 || pixels[inside + 2] < 126 || pixels[inside + 2] > 129 || pixels[inside + 3] != 255) {
            throw std::runtime_error("GPU alpha/order rejected: expected opaque purple at clipped overlap.");
        }
        renderer.Resize(32, 16);
        renderer.BeginFrame({0,1,0,1});
        pixels = renderer.EndFrameAndReadback();
        if (pixels.size() != 32 * 16 * 4 || pixels[1] != 255) { throw std::runtime_error("GPU resize/readback rejected."); }
        if (renderer.Resize(0, 0)) { throw std::runtime_error("Zero drawable size must defer target recreation."); }
        const uint8_t atlas[] {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
        const auto atlasTexture = renderer.CreateTexture(2,2,atlas);
        const uint8_t updatedRegion[] {255,255,0,255, 10,20,30,40};
        renderer.UpdateTextureRegion(atlasTexture,0,1,1,1,updatedRegion,8);
        renderer.BeginFrame({0,0,0,1});
        renderer.DrawRect(atlasTexture,0,0,32,16,{1,1,1,1});
        pixels = renderer.EndFrameAndReadback();
        const size_t bottomLeft = (12 * 32 + 4) * 4;
        const size_t topRight = (4 * 32 + 24) * 4;
        if (pixels[bottomLeft] != 255 || pixels[bottomLeft+1] != 255 || pixels[bottomLeft+2] != 0 || pixels[topRight] != 0 || pixels[topRight+1] != 255) {
            throw std::runtime_error("GPU texture upload/update UV association or row-pitch handling failed.");
        }
        renderer.ReleaseTexture(atlasTexture);
        bool releasedTextureRejected = false;
        renderer.BeginFrame({0,0,0,1});
        try { renderer.DrawRect(atlasTexture,0,0,1,1,{1,1,1,1}); } catch (const std::out_of_range&) { releasedTextureRejected = true; }
        if (!releasedTextureRejected) { throw std::runtime_error("Released runtime texture handle was accepted."); }
        renderer.EndFrameAndReadback();
        renderer.Resize(64,64); renderer.BeginFrame({0,0,0,1});
        renderer.DrawRoundedRect(8,8,48,48,12,4,15,{1,0,0,1},{0,1,0,1});
        pixels = renderer.EndFrameAndReadback();
        const size_t shapeCorner = (8 * 64 + 8) * 4, shapeCenter = (32 * 64 + 32) * 4, shapeBorder = (9 * 64 + 32) * 4;
        if (pixels[shapeCorner] != 0 || pixels[shapeCorner+1] != 0 || pixels[shapeCenter] != 255 || pixels[shapeCenter+1] != 0 || pixels[shapeBorder] != 0 || pixels[shapeBorder+1] != 255) {
            throw std::runtime_error("GPU rounded rectangle fill, border or corner coverage failed.");
        }
        if (!renderer.GetOutputImageView() || !renderer.GetTextureDescriptor(texture).imageView) { throw std::runtime_error("Shared native target/texture borrowing did not expose live GPU views."); }
        renderer.BeginFrame({0,0,1,1});
        renderer.DrawRect(texture,16,16,32,32,{1,0,0,1});
        if (!renderer.EndFrame(false).empty()) { throw std::runtime_error("Disabled readback exposed CPU pixels."); }
        VkCommandBuffer loadCommands=context.BeginCommands();
        VkImageMemoryBarrier loadBarrier {}; loadBarrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        loadBarrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; loadBarrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        loadBarrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; loadBarrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        loadBarrier.srcQueueFamilyIndex=loadBarrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; loadBarrier.image=renderer.GetOutputImage(); loadBarrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(loadCommands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&loadBarrier); context.SubmitAndWait(loadCommands);
        renderer.BeginFrame({0,1,0,1},true); pixels=renderer.EndFrameAndReadback();
        if (pixels.size()!=64*64*4 || pixels[shapeCenter]!=255 || pixels[shapeCenter+1]!=0 || pixels[shapeCenter+2]!=0 || pixels[shapeCorner]!=0 || pixels[shapeCorner+2]!=255) { throw std::runtime_error("Readback after a GPU-only frame discarded rendered or clear color."); }
        if (context.GetValidationErrorCount()) { throw std::runtime_error("Vulkan validation reported errors during GPU contracts."); }
        std::cout << "PASS textured-quads alpha draw-order clipping resize zero-drawable texture-region row-pitch UV resource-release rounded-fill-border-corners readback\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
