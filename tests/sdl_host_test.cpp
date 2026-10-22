#include "sdl_host.hpp"
#include <iostream>
#include <stdexcept>

/// <summary>Exercises SDL-created Vulkan surface presentation and resize in an explicitly isolated display session.</summary>
int main() {
    try {
        if (!std::getenv("HELENGINE_ISOLATED_DISPLAY_TEST")) { throw std::runtime_error("Set HELENGINE_ISOLATED_DISPLAY_TEST only inside an isolated Xvfb/Wayland test session; this test must not open the user desktop."); }
        helengine::linux::SdlHost host("HelEngine isolated surface test",320,240,true);
        helengine::linux::VulkanRenderer2D renderer(host.GetContext(),320,240);
        const uint8_t white[] {255,255,255,255}; const auto texture = renderer.CreateTexture(1,1,white);
        for (int frame = 0; frame < 3; ++frame) {
            SDL_Event event {}; while (host.PollEvent(event)) {}
            if (frame == 1) { host.SetWindowSize(400,300); }
            auto extent = host.GetDrawableSize();
            if (!extent.width || !extent.height) { throw std::runtime_error("Isolated SDL test drawable unexpectedly has zero extent."); }
            renderer.Resize(extent.width,extent.height); renderer.BeginFrame({0,0,0,1});
            renderer.DrawRect(texture,8,8,32,32,{0,1,0,1}); renderer.EndFrameAndReadback(); host.Present(renderer);
        }
        if (host.GetContext().GetValidationErrorCount()) { throw std::runtime_error("Vulkan validation reported errors in SDL presentation."); }
        std::cout << "PASS SDL surface swapchain present resize device=" << host.GetContext().GetProperties().deviceName << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
