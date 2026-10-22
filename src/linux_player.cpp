#include "linux_player.hpp"
#include "generated_disposable_deleter.hpp"
#include "linux_render_manager_2d.hpp"
#include "linux_render_manager_3d.hpp"
#include "linux_input_replay.hpp"
#include "sdl_replay_gamepad.hpp"
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
#include "ObjectManager.hpp"
#include "Entity.hpp"
#include "Component.hpp"
#if __has_include("MenuComponent.hpp")
#include "MenuComponent.hpp"
#include "MenuItemComponent.hpp"
#endif
#include "LoadedSceneRecord.hpp"
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
#include <fstream>
#include <stdexcept>

namespace helengine::linux {
::RuntimeSceneCatalog* LinuxPlayer::CreateSceneCatalog() {
    size_t count=0; const auto* entries=he_runtime_scene_catalog_entries(&count);
    if (!entries || !count || count>INT32_MAX) { throw std::runtime_error("Native Linux player requires a nonempty generated runtime scene catalog."); }
    auto catalog=std::make_unique<Array<::RuntimeSceneCatalogEntry*>>(static_cast<int32_t>(count));
    std::vector<GeneratedDisposableOwner<::RuntimeSceneCatalogEntry>> temporaryOwners;
    for (size_t index=0;index<count;++index) {
        if (!entries[index].SceneId || !entries[index].CookedRelativePath) { throw std::runtime_error("Generated scene catalog contains an invalid entry."); }
        temporaryOwners.emplace_back(new ::RuntimeSceneCatalogEntry(entries[index].SceneId,entries[index].CookedRelativePath));
        (*catalog)[static_cast<int32_t>(index)]=temporaryOwners.back().get();
    }
    return new ::RuntimeSceneCatalog(catalog.get());
}

::StandardPlatformInputConfiguration* LinuxPlayer::CreateStandardInput(std::vector<std::unique_ptr<::StandardPlatformActionBinding>>& bindingOwners) {
    size_t count=0; const auto* entries=he_runtime_standard_platform_action_entries(&count);
    auto bindings=std::make_unique<List<::StandardPlatformActionBinding*>>();
    for (size_t index=0;index<count;++index) {
        const auto& entry=entries[index];
        bindingOwners.emplace_back(new ::StandardPlatformActionBinding(static_cast<::StandardPlatformAction>(entry.ActionId),::InputControlId(static_cast<::InputDeviceKind>(entry.DeviceKind),static_cast<::InputControlKind>(entry.ControlKind),entry.DeviceIndex,entry.ControlIndex)));
        bindings->Add(bindingOwners.back().get());
    }
    return new ::StandardPlatformInputConfiguration(bindings.get());
}

std::string LinuxPlayer::ResolveSceneId(::RuntimeSceneCatalog* catalog,const std::string& requestedSceneId,const std::string& startupPath) {
    if (!catalog) { throw std::invalid_argument("Linux scene selection requires an initialized runtime catalog."); }
    if (!requestedSceneId.empty()) { ::RuntimeSceneCatalogEntry* entry=nullptr; if (!catalog->TryGetEntry__out1(requestedSceneId,entry)) { throw std::runtime_error("Requested scene is absent from the actual packaged catalog: " + requestedSceneId); } return entry->get_SceneId().raw(); }
    if (startupPath.empty()) { throw std::runtime_error("Generated Linux startup manifest has no cooked scene path."); }
    std::string selected;
    auto* entries=catalog->get_Entries();
    for (int32_t index=0;index<entries->get_Length();++index) { auto* entry=(*entries)[index]; if (entry->get_CookedRelativePath()==startupPath) { if (!selected.empty()) { throw std::runtime_error("Generated startup scene path is ambiguous in the packaged catalog: " + startupPath); } selected=entry->get_SceneId().raw(); } }
    if (selected.empty()) { throw std::runtime_error("Generated startup scene is absent from the packaged catalog: " + startupPath); }
    return selected;
}

std::string LinuxPlayer::SnapshotMenuState(::Core* core) {
#if __has_include("MenuComponent.hpp")
    std::string state; int32_t items=0; auto* entities=core->get_ObjectManager()->get_Entities();
    for (int32_t index=0;entities && index<entities->get_Count();++index) {
        auto* entity=(*entities)[index]; if (!entity) { continue; } auto* components=entity->get_Components();
        for (int32_t componentIndex=0;components && componentIndex<components->get_Count();++componentIndex) {
            auto* component=(*components)[componentIndex];
            if (auto* menu=dynamic_cast<::MenuComponent*>(component)) { state+="panel="+menu->get_ActivePanelId().raw()+" selected="+menu->get_SelectedItemId().raw(); }
            else if (dynamic_cast<::MenuItemComponent*>(component)) { ++items; }
        }
    }
    return state.empty()?state:state+" menu_items="+std::to_string(items);
#else
    (void)core; return {};
#endif
}

int LinuxPlayer::Run(const LinuxRunOptions& options) {
    std::unique_ptr<LinuxInputReplay> replay;
    if (!options.InputScript.empty()) { std::ifstream script(options.InputScript); replay=std::make_unique<LinuxInputReplay>(script); }
    std::unique_ptr<SdlHost> window;
    std::unique_ptr<VulkanContext> offscreen;
    if (options.Offscreen) { offscreen=std::make_unique<VulkanContext>(std::span<const char* const>{},options.Validation,VK_API_VERSION_1_2); }
    else { window=std::make_unique<SdlHost>("HelEngine Linux",HELENGINE_LINUX_INITIAL_WIDTH,HELENGINE_LINUX_INITIAL_HEIGHT,options.Validation); }
    VulkanContext& graphics=window ? window->GetContext() : *offscreen;
    VulkanRenderer2D renderer(graphics,HELENGINE_LINUX_INITIAL_WIDTH,HELENGINE_LINUX_INITIAL_HEIGHT,(std::filesystem::path(options.ContentRoot)/"shaders").string());
    LinuxRenderManager2D render2d(renderer); render2d.SetTextCommandReuseMemoryBudgetBytes(he_get_runtime_render2d_text_command_reuse_memory_budget_bytes());
    LinuxRenderManager3D render3d(renderer,render2d); SdlInputBackend input(options.Offscreen);
    std::unique_ptr<SdlReplayGamepad> replayGamepad;
    if (replay && replay->HasGamepadEvents()) { replayGamepad=std::make_unique<SdlReplayGamepad>(); input.AttachReplayGamepad(replayGamepad->GetId()); }
    auto content=std::make_unique<::HostFileSystemContentStreamSource>(options.ContentRoot);
    GeneratedDisposableOwner<::RuntimeSceneCatalog> catalog(CreateSceneCatalog());
    std::vector<std::unique_ptr<::StandardPlatformActionBinding>> bindingOwners;
    GeneratedDisposableOwner<::StandardPlatformInputConfiguration> standardInput(CreateStandardInput(bindingOwners));
    GeneratedDisposableOwner<::PlatformSettingsConfiguration> settings(he_create_runtime_platform_settings());
    ::CoreInitializationOptions initialization;
    initialization.ContentStreamSource=content.get(); initialization.SceneCatalog=catalog.get(); initialization.StandardPlatformInputConfiguration=standardInput.get();
    initialization.SceneLoadingBudgetMilliseconds=he_get_runtime_scene_loading_budget_milliseconds(); initialization.PlatformSettingsConfiguration=settings.get();
    ::Core core(&initialization); ::PlatformInfo platform(he_get_runtime_platform_name(),he_get_runtime_platform_version());
    render3d.AddWindow(1,HELENGINE_LINUX_INITIAL_WIDTH,HELENGINE_LINUX_INITIAL_HEIGHT); render3d.set_ShadowsEnabled(he_get_runtime_shadows_enabled());
    bool coreNeedsShutdown=false;
    try {
        core.Initialize(&render3d,&render2d,&input,&platform,&initialization); coreNeedsShutdown=true; RegisterGeneratedRuntimeModules(&core);
        auto* entries=catalog->get_Entries();
        const std::string scene=ResolveSceneId(catalog.get(),options.SceneId,he_get_runtime_startup_scene_relative_path());
        std::cout << "core=generated scene=" << scene << " catalog_count=" << entries->get_Length() << " device=" << graphics.GetProperties().deviceName << '\n';
        core.get_SceneManager()->LoadScene(scene,::SceneLoadMode::Single); core.get_SceneManager()->CommitPendingOperationsAtFrameBoundary();
        bool running=true; uint32_t frames=0; auto previous=std::chrono::steady_clock::now();
        std::string lastScene,lastMenu;
        while (running && (!options.Frames || frames<options.Frames)) {
            if (window) {
                SDL_Event event {}; while (window->PollEvent(event)) { input.HandleEvent(event); if (event.type==SDL_EVENT_QUIT) { running=false; } }
                const auto size=window->GetDrawableSize();
                if (!size.width || !size.height) { SDL_Delay(16); continue; }
                if (size.width!=renderer.GetWidth() || size.height!=renderer.GetHeight()) { renderer.Resize(size.width,size.height); render3d.OnWindowResize(1,size.width,size.height); }
            }
            if (replay) { if (options.Offscreen) { input.set_ReceiveInputInBackground(true); } for (const auto& event : replay->EventsForFrame(frames)) { if (event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type==SDL_EVENT_GAMEPAD_BUTTON_UP) { replayGamepad->Apply(event); std::cout << "replay_frame=" << frames << " gamepad_button=" << static_cast<int>(event.gbutton.button) << " down=" << event.gbutton.down << '\n'; } else { input.HandleEvent(event); std::cout << "replay_frame=" << frames << " scancode=" << event.key.scancode << " down=" << event.key.down << '\n'; } } }
            const auto now=std::chrono::steady_clock::now();
            const double elapsed=options.Offscreen ? 1.0/60.0 : std::chrono::duration<double>(now-previous).count(); previous=now;
            const auto updateStart=std::chrono::steady_clock::now(); core.Update(elapsed); const auto drawStart=std::chrono::steady_clock::now(); core.Draw();
            const auto drawEnd=std::chrono::steady_clock::now(); core.SetHostFrameTimings(std::chrono::duration<double,std::milli>(drawStart-updateStart).count(),std::chrono::duration<double,std::milli>(drawEnd-drawStart).count());
            auto* loaded=core.get_SceneManager()->get_LoadedScenes();
            if (loaded && loaded->get_Count()>0) { const auto current=loaded->get_Item(loaded->get_Count()-1)->get_SceneId().raw(); if (current!=lastScene) { std::cout << "loaded_scene_frame=" << frames << " scene=" << current << '\n'; lastScene=current; } }
            if (replay) { const auto state=SnapshotMenuState(&core); if (state!=lastMenu) { std::cout << "menu_state_frame=" << frames << ' ' << state << '\n'; lastMenu=state; } }
            if (window) { window->Present(renderer); } ++frames;
        }
        if (replay && !replay->IsComplete()) { throw std::runtime_error("Linux player stopped before all explicit input replay events were captured."); }
        if (graphics.GetValidationErrorCount()) { throw std::runtime_error("Vulkan validation errors occurred during actual generated-core execution."); }
        uint64_t checksum=1469598103934665603ULL;
        for (uint8_t byte : render3d.GetLastPixels()) { checksum=(checksum^byte)*1099511628211ULL; }
        bool nonuniform=false; const auto& pixels=render3d.GetLastPixels();
        for (size_t index=4;index+3<pixels.size();index+=4) { if (pixels[index]!=pixels[0] || pixels[index+1]!=pixels[1] || pixels[index+2]!=pixels[2] || pixels[index+3]!=pixels[3]) { nonuniform=true; break; } }
        std::cout << "core_frames=" << frames << " draw_calls=" << render3d.get_LastDrawCallCount() << " rgba_bytes=" << pixels.size() << " checksum=" << checksum << " nonuniform=" << nonuniform << '\n';
        if (options.Offscreen && (!frames || render3d.get_LastDrawCallCount()<=0 || !nonuniform)) { throw std::runtime_error("Generated-core offscreen run did not produce drawable content; this is not a successful menu validation."); }
        coreNeedsShutdown=false; core.Dispose();
        return 0;
    } catch (...) {
        if (coreNeedsShutdown) {
            coreNeedsShutdown=false;
            try { core.Dispose(); }
            catch (const std::exception& cleanup) { std::cerr << "Core shutdown also failed: " << cleanup.what() << '\n'; }
            catch (const std::exception* cleanup) { std::cerr << "Core shutdown also failed: " << (cleanup ? cleanup->what() : "null generated exception") << '\n'; delete cleanup; }
            catch (...) { std::cerr << "Core shutdown also failed with an unknown exception.\n"; }
        }
        throw;
    }
}
}
