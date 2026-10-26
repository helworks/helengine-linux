#include "sdl_host.hpp"
#include <iostream>
#include <stdexcept>

/// <summary>Verifies borrowed-device presentation and independent native surface teardown only in an explicitly isolated compositor.</summary>
int main() {
    try {
        if (!std::getenv("HELENGINE_ISOLATED_DISPLAY_TEST")) { throw std::runtime_error("Multi-output host test requires an explicitly isolated compositor."); }
        helengine::linux::SdlHost main("HelEngine isolated opaque output",320,240,true,nullptr,false,true);
        helengine::linux::VulkanRenderer2D renderer(main.GetContext(),320,240);
        renderer.BeginFrame({1,0,0,1}); renderer.EndFrameAndReadback(); main.Present(renderer);
        {
            helengine::linux::SdlHost glass("HelEngine isolated transparent output",160,120,true,&main.GetContext(),true,true);
            if (main.GetContext().GetDevice()!=glass.GetContext().GetDevice()) { throw std::runtime_error("Outputs do not share one Vulkan device."); }
            renderer.AddOutput(2,160,120); renderer.SelectOutput(2);
            renderer.BeginFrame({0,0,0,0}); renderer.DrawRect(0,8,8,64,64,{0,1,0,0.5f}); renderer.EndFrameAndReadback(); glass.Present(renderer);
            glass.SetWindowSize(200,150);
            for (int attempt=0;attempt<50;++attempt) {
                SDL_Event event {}; while (glass.PollEvent(event)) {}
                const auto extent=glass.GetDrawableSize();
                if (extent.width==200 && extent.height==150) { break; } SDL_Delay(10);
            }
            const auto size=glass.GetDrawableSize();
            if (size.width!=200 || size.height!=150 || main.GetDrawableSize().width!=320) { throw std::runtime_error("Secondary resize changed another output or failed."); }
            renderer.Resize(size.width,size.height); renderer.BeginFrame({0,0,0,0}); renderer.EndFrameAndReadback(); glass.Present(renderer);
        }
        renderer.SelectOutput(1); renderer.BeginFrame({0,0,1,1}); renderer.EndFrameAndReadback();
        if (!main.Present(renderer)) { throw std::runtime_error("First output stopped presenting after secondary host destruction."); }
        if (main.GetContext().GetValidationErrorCount()) { throw std::runtime_error("Shared output presentation/teardown caused Vulkan validation errors."); }
        std::cout << "PASS isolated shared-device opaque/transparent surfaces, empty shape API, independent resize/destruction\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
