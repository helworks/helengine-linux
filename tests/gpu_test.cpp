#include "vulkan_renderer_2d.hpp"
#include <iostream>
#include <stdexcept>

/// <summary>Runs a real offscreen render, then verifies texture sampling, draw order, alpha and clipping through GPU readback.</summary>
int main() {
    try {
        helengine::linux::VulkanContext context;
        const auto& device = context.GetProperties();
        std::cout << "device=" << device.deviceName << " type=" << device.deviceType
                  << " api=" << VK_VERSION_MAJOR(device.apiVersion) << "." << VK_VERSION_MINOR(device.apiVersion) << '\n';
        helengine::linux::VulkanRenderer2D renderer(context, 64, 64);
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
        std::cout << "PASS textured-quads alpha draw-order clipping resize zero-drawable readback\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
