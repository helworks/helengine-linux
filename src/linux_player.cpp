#include "linux_player.hpp"
#include "linux_render_manager_2d.hpp"
#include "linux_render_manager_3d.hpp"
#include "sdl_input_backend.hpp"
#include "sdl_host.hpp"
#include "Core.hpp"
#include "CoreInitializationOptions.hpp"
#include "PlatformInfo.hpp"
#include "PlatformSettingsConfiguration.hpp"
#include "HostFileSystemContentStreamSource.hpp"
#include "RuntimeSceneCatalog.hpp"
#include "RuntimeSceneCatalogEntry.hpp"
#include "SceneManager.hpp"
#include "SceneLoadMode.hpp"
#include "GeneratedRuntimeModuleRegistration.hpp"
#include "StandardPlatformInputConfiguration.hpp"
#include "StandardPlatformActionBinding.hpp"
#include "InputControlId.hpp"
#include "InputDeviceKind.hpp"
#include "InputControlKind.hpp"
#include "StandardPlatformAction.hpp"
#include "runtime/runtime_startup_manifest.hpp"
#include "runtime/runtime_scene_catalog_manifest.hpp"
#include "runtime/runtime_standard_platform_input_manifest.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace helengine::linux {
::RuntimeSceneCatalog* LinuxPlayer::CreateSceneCatalog() {
    size_t count=0; const auto* entries=he_runtime_scene_catalog_entries(&count);
    if (!entries || !count || count>INT32_MAX) { throw std::runtime_error("Native Linux player requires a nonempty generated runtime scene catalog."); }
    auto catalog=std::make_unique<Array<::RuntimeSceneCatalogEntry*>>(static_cast<int32_t>(count));
    for (size_t index=0;index<count;++index) {
        if (!entries[index].SceneId || !entries[index].CookedRelativePath) { throw std::runtime_error("Generated scene catalog contains an invalid entry."); }
        (*catalog)[static_cast<int32_t>(index)]=new ::RuntimeSceneCatalogEntry(entries[index].SceneId,entries[index].CookedRelativePath);
    }
    return new ::RuntimeSceneCatalog(catalog.release());
}

::StandardPlatformInputConfiguration* LinuxPlayer::CreateStandardInput() {
    size_t count=0; const auto* entries=he_runtime_standard_platform_action_entries(&count);
    auto bindings=std::make_unique<List<::StandardPlatformActionBinding*>>();
    for (size_t index=0;index<count;++index) {
        const auto& entry=entries[index];
        bindings->Add(new ::StandardPlatformActionBinding(static_cast<::StandardPlatformAction>(entry.ActionId),::InputControlId(static_cast<::InputDeviceKind>(entry.DeviceKind),static_cast<::InputControlKind>(entry.ControlKind),entry.DeviceIndex,entry.ControlIndex)));
    }
    return new ::StandardPlatformInputConfiguration(bindings.release());
}

int LinuxPlayer::Run(const LinuxRunOptions& options) {
    std::unique_ptr<SdlHost> window;
    std::unique_ptr<VulkanContext> offscreen;
    if (options.Offscreen) { offscreen=std::make_unique<VulkanContext>(std::span<const char* const>{},options.Validation,VK_API_VERSION_1_2); }
    else { window=std::make_unique<SdlHost>("HelEngine Linux",HELENGINE_LINUX_INITIAL_WIDTH,HELENGINE_LINUX_INITIAL_HEIGHT,options.Validation); }
    VulkanContext& graphics=window ? window->GetContext() : *offscreen;
    VulkanRenderer2D renderer(graphics,HELENGINE_LINUX_INITIAL_WIDTH,HELENGINE_LINUX_INITIAL_HEIGHT,(std::filesystem::path(options.ContentRoot)/"shaders").string());
    LinuxRenderManager2D render2d(renderer); LinuxRenderManager3D render3d(renderer,render2d); SdlInputBackend input(options.Offscreen);
    auto content=std::make_unique<::HostFileSystemContentStreamSource>(options.ContentRoot);
    std::unique_ptr<::RuntimeSceneCatalog> catalog(CreateSceneCatalog());
    std::unique_ptr<::StandardPlatformInputConfiguration> standardInput(CreateStandardInput());
    std::unique_ptr<::PlatformSettingsConfiguration> settings(he_create_runtime_platform_settings());
    ::CoreInitializationOptions initialization;
    initialization.ContentStreamSource=content.get(); initialization.SceneCatalog=catalog.get(); initialization.StandardPlatformInputConfiguration=standardInput.get();
    initialization.SceneLoadingBudgetMilliseconds=he_get_runtime_scene_loading_budget_milliseconds(); initialization.PlatformSettingsConfiguration=settings.get();
    ::Core core(&initialization); ::PlatformInfo platform(he_get_runtime_platform_name(),he_get_runtime_platform_version());
    render3d.AddWindow(1,HELENGINE_LINUX_INITIAL_WIDTH,HELENGINE_LINUX_INITIAL_HEIGHT); render3d.set_ShadowsEnabled(he_get_runtime_shadows_enabled());
    core.Initialize(&render3d,&render2d,&input,&platform,&initialization); RegisterGeneratedRuntimeModules(&core);
    try {
        auto* entries=catalog->get_Entries();
        const std::string scene=options.SceneId.empty() ? (*entries)[0]->get_SceneId().raw() : options.SceneId;
        ::RuntimeSceneCatalogEntry* selected=nullptr;
        if (!catalog->TryGetEntry__out1(scene,selected)) { throw std::runtime_error("Requested scene is absent from the actual packaged catalog: " + scene); }
        std::cout << "core=generated scene=" << scene << " catalog_count=" << entries->get_Length() << " device=" << graphics.GetProperties().deviceName << '\n';
        core.get_SceneManager()->LoadScene(scene,::SceneLoadMode::Single); core.get_SceneManager()->CommitPendingOperationsAtFrameBoundary();
        bool running=true; uint32_t frames=0; auto previous=std::chrono::steady_clock::now();
        while (running && (!options.Frames || frames<options.Frames)) {
            if (window) {
                SDL_Event event {}; while (window->PollEvent(event)) { input.HandleEvent(event); if (event.type==SDL_EVENT_QUIT) { running=false; } }
                const auto size=window->GetDrawableSize();
                if (!size.width || !size.height) { SDL_Delay(16); continue; }
                if (size.width!=renderer.GetWidth() || size.height!=renderer.GetHeight()) { renderer.Resize(size.width,size.height); render3d.OnWindowResize(1,size.width,size.height); }
            }
            const auto now=std::chrono::steady_clock::now();
            const double elapsed=options.Offscreen ? 1.0/60.0 : std::chrono::duration<double>(now-previous).count(); previous=now;
            core.Update(elapsed); core.Draw(); if (window) { window->Present(renderer); } ++frames;
        }
        if (graphics.GetValidationErrorCount()) { throw std::runtime_error("Vulkan validation errors occurred during actual generated-core execution."); }
        uint64_t checksum=1469598103934665603ULL;
        for (uint8_t byte : render3d.GetLastPixels()) { checksum=(checksum^byte)*1099511628211ULL; }
        bool nonuniform=false; const auto& pixels=render3d.GetLastPixels();
        for (size_t index=4;index+3<pixels.size();index+=4) { if (pixels[index]!=pixels[0] || pixels[index+1]!=pixels[1] || pixels[index+2]!=pixels[2] || pixels[index+3]!=pixels[3]) { nonuniform=true; break; } }
        std::cout << "core_frames=" << frames << " draw_calls=" << render3d.get_LastDrawCallCount() << " rgba_bytes=" << pixels.size() << " checksum=" << checksum << " nonuniform=" << nonuniform << '\n';
        if (options.Offscreen && (!frames || render3d.get_LastDrawCallCount()<=0 || !nonuniform)) { throw std::runtime_error("Generated-core offscreen run did not produce drawable content; this is not a successful menu validation."); }
        core.Dispose(); catalog->Dispose(); standardInput->Dispose();
        return 0;
    } catch (...) {
        try { core.Dispose(); } catch (const std::exception& cleanup) { std::cerr << "Core shutdown also failed: " << cleanup.what() << '\n'; }
        throw;
    }
}
}
