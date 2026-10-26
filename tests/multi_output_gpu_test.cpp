#include "vulkan_renderer_2d.hpp"
#include <iostream>
#include <stdexcept>

/// <summary>Requires independently owned targets to retain real GPU alpha/pixels across selection, resize and destruction.</summary>
int main() {
    try {
        helengine::linux::VulkanContext context({},true);
        {
            helengine::linux::VulkanRenderer2D renderer(context,64,48);
            renderer.BeginFrame({1,0,0,1}); const auto main=renderer.EndFrameAndReadback(); const auto mainImage=renderer.GetOutputImage();
            renderer.AddOutput(2,32,24); renderer.SelectOutput(2);
            if (renderer.GetOutputImage()==mainImage) { throw std::runtime_error("Outputs alias one image."); }
            renderer.BeginFrame({0,0,0,0}); renderer.DrawRect(0,0,0,16,12,{0,1,0,0.5f}); const auto glass=renderer.EndFrameAndReadback();
            if (glass.size()!=32*24*4 || glass[1]<126 || glass[1]>129 || glass[3]<126 || glass[3]>129 || glass.back()!=0) { throw std::runtime_error("Glass target lost partial/empty alpha."); }
            renderer.Resize(40,30); renderer.SelectOutput(1);
            if (renderer.GetWidth()!=64 || renderer.GetHeight()!=48 || renderer.GetOutputImage()!=mainImage) { throw std::runtime_error("Secondary resize changed main target ownership or extent."); }
            helengine::linux::GpuBuffer readback(context,main.size(),VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            auto commands=context.BeginCommands(); VkBufferImageCopy copy {}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={64,48,1};
            vkCmdCopyImageToBuffer(commands,renderer.GetOutputImage(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.Get(),1,&copy); context.SubmitAndWait(commands);
            std::vector<uint8_t> preserved(main.size()); std::memcpy(preserved.data(),readback.Map(),preserved.size()); readback.Unmap();
            if (main!=preserved || preserved[0]!=255 || preserved[3]!=255) { throw std::runtime_error("Secondary draw contaminated main GPU pixels."); }
            renderer.SelectOutput(2); if (renderer.GetWidth()!=40 || renderer.GetHeight()!=30) { throw std::runtime_error("Secondary extent was not retained."); }
        }
        if (context.GetValidationErrorCount()) { throw std::runtime_error("Target teardown or rendering caused Vulkan validation errors."); }
        std::cout << "PASS separate GPU targets, alpha isolation, resize and destruction\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
