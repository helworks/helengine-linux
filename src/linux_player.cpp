#include "linux_player.hpp"
#include "linux_session_guard.hpp"
#include "generated_disposable_deleter.hpp"
#include "linux_render_manager_2d.hpp"
#include "linux_render_manager_3d.hpp"
#include "linux_input_replay.hpp"
#include "linux_tracy_profiler.hpp"
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
#if __has_include("SoftwarePathTracerComponent.hpp")
#include "SoftwarePathTracerComponent.hpp"
#include "SoftwarePathTraceSessionState.hpp"
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

void LinuxPlayer::ValidateSoftwareTracer(::Core* core,bool requireProgress,const std::vector<uint8_t>& pixels,uint32_t width,uint32_t height) {
    bool found=false;
#if __has_include("SoftwarePathTracerComponent.hpp")
    auto* entities=core->get_ObjectManager()->get_Entities();
    for (int32_t index=0;entities && index<entities->get_Count();++index) {
        auto* entity=(*entities)[index]; if (!entity || !entity->get_IsHierarchyEnabled()) { continue; } auto* components=entity->get_Components();
        for (int32_t componentIndex=0;components && componentIndex<components->get_Count();++componentIndex) {
            auto* tracer=dynamic_cast<::SoftwarePathTracerComponent*>((*components)[componentIndex]); if (!tracer) { continue; } found=true;
            bool varied=false; const size_t first=(static_cast<size_t>(height/4)*width+width/4)*4;
            if (pixels.size()!=static_cast<size_t>(width)*height*4) { throw std::runtime_error("Software tracer validation requires an actual complete GPU readback."); }
            for (uint32_t y=height/4;y<height*3/4 && !varied;++y) { for (uint32_t x=width/4;x<width*3/4;++x) { const size_t pixel=(static_cast<size_t>(y)*width+x)*4; if (pixels[pixel]!=pixels[first] || pixels[pixel+1]!=pixels[first+1] || pixels[pixel+2]!=pixels[first+2]) { varied=true; break; } } }
            std::cout << "tracer_state=" << static_cast<int>(tracer->get_SessionState()) << " tracer_spp=" << tracer->get_CompletedSpp() << " tracer_rays=" << tracer->get_TotalRays() << " tracer_nonfinite=" << tracer->get_NonFiniteSampleCount() << " tracer_roi_nonuniform=" << varied << '\n';
            if (tracer->get_SessionState()==::SoftwarePathTraceSessionState::Failed) { throw std::runtime_error("Generated software tracer entered Failed; a drawable HUD is not successful scene rendering."); }
            if (requireProgress && (tracer->get_SessionState()!=::SoftwarePathTraceSessionState::Tracing || tracer->get_CompletedSpp()<1 || tracer->get_TotalRays()<76800 || tracer->get_NonFiniteSampleCount()!=0 || !varied)) { throw std::runtime_error("Software tracer did not complete a finite 320x240 sample pass with actual central GPU output."); }
        }
    }
#else
    (void)core; (void)pixels; (void)width; (void)height;
#endif
    if (requireProgress && !found) { throw std::runtime_error("Strict software tracing validation found no active generated tracing component."); }
}

void LinuxPlayer::ValidateShadowReception(::Core* core,LinuxRenderManager3D& renderer,uint32_t width,uint32_t height) {
    if (!renderer.get_ShadowsEnabled()) { throw std::runtime_error("Shadow reception validation requires authored shadows enabled in the runtime manifest."); }
    core->Draw(); const auto enabled=renderer.GetLastPixels();
    renderer.set_ShadowsEnabled(false);
    try { core->Draw(); }
    catch (...) { renderer.set_ShadowsEnabled(true); throw; }
    const auto disabled=renderer.GetLastPixels();
    renderer.set_ShadowsEnabled(true); core->Draw(); const auto& restored=renderer.GetLastPixels();
    if (enabled.size()!=static_cast<size_t>(width)*height*4 || disabled.size()!=enabled.size() || restored.size()!=enabled.size()) { throw std::runtime_error("Shadow reception validation received an incomplete GPU frame."); }
    size_t changed=0;
    for (uint32_t y=height/4;y<height*3/4;++y) {
        for (uint32_t x=width/4;x<width*3/4;++x) {
            const size_t offset=(static_cast<size_t>(y)*width+x)*4;
            if (enabled[offset]!=disabled[offset] || enabled[offset+1]!=disabled[offset+1] || enabled[offset+2]!=disabled[offset+2]) { ++changed; }
        }
    }
    const bool exact=enabled==restored;
    std::cout << "shadow_receiver_changed_pixels=" << changed << " shadow_receiver_restored=" << exact << '\n';
    if (!changed || !exact) { throw std::runtime_error("Frozen generated scene did not receive real shadows or failed exact on/off/on restoration."); }
}

int LinuxPlayer::Run(const LinuxRunOptions& options) {
    if (!options.Session.empty()) { LinuxSessionGuard::Activate(options.Session,options.ParentPid); }
    std::unique_ptr<LinuxInputReplay> replay;
    if (!options.InputScript.empty()) { std::ifstream script(options.InputScript); replay=std::make_unique<LinuxInputReplay>(script); }
    std::vector<LinuxOutputOptions> outputs=options.Outputs;
    if (outputs.empty()) { LinuxOutputOptions output; output.Width=HELENGINE_LINUX_INITIAL_WIDTH; output.Height=HELENGINE_LINUX_INITIAL_HEIGHT; outputs.push_back(output); }
    std::unique_ptr<SdlHost> window;
    std::unique_ptr<VulkanContext> offscreen;
    std::vector<std::unique_ptr<SdlHost>> secondaryWindows;
    std::vector<SdlHost*> windows;
    std::vector<VkExtent2D> extents;
    std::vector<float> scales;
    if (options.Offscreen) { offscreen=std::make_unique<VulkanContext>(std::span<const char* const>{},options.Validation,VK_API_VERSION_1_2); }
    for (const auto& output : outputs) {
        if (options.Offscreen) { windows.push_back(nullptr); extents.push_back({output.Width,output.Height}); continue; }
        const auto title=options.Session.empty()?"HelEngine Linux" : "HelEngine:"+options.Session+":"+output.Tag;
        if (!window) { window=std::make_unique<SdlHost>(title.c_str(),output.Width,output.Height,options.Validation,nullptr,output.Transparent,options.Noninteractive); windows.push_back(window.get()); }
        else { secondaryWindows.push_back(std::make_unique<SdlHost>(title.c_str(),output.Width,output.Height,options.Validation,&window->GetContext(),output.Transparent,options.Noninteractive)); windows.push_back(secondaryWindows.back().get()); }
        scales.push_back(windows.back()->GetDisplayScale());
        const auto extent=windows.back()->GetDrawableSize();
        if (!extent.width || !extent.height) { throw std::runtime_error("Native output has no drawable extent during initialization."); } extents.push_back(extent);
    }
    VulkanContext& graphics=window ? window->GetContext() : *offscreen;
    VulkanRenderer2D renderer(graphics,extents[0].width,extents[0].height,(std::filesystem::path(options.ContentRoot)/"shaders").string());
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
    initialization.CpuProfileSink=GetLinuxCpuProfileSink();
    ::Core core(&initialization); ::PlatformInfo platform(he_get_runtime_platform_name(),he_get_runtime_platform_version());
    for (size_t index=0;index<outputs.size();++index) {
        const intptr_t handle=static_cast<intptr_t>(index+1); render3d.AddWindow(handle,extents[index].width,extents[index].height);
        if (!outputs[index].Tag.empty()) { render3d.SetWindowTag(handle,outputs[index].Tag); }
        render3d.SetOutputTransparent(handle,outputs[index].Transparent);
    }
    render3d.set_ShadowsEnabled(he_get_runtime_shadows_enabled());
    std::vector<uint64_t> generations(outputs.size(),1),ticks(outputs.size(),0);
    std::vector<bool> active(outputs.size(),true);
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
                SDL_Event event {}; while (window->PollEvent(event)) { if (!options.Noninteractive) { input.HandleEvent(event); } if (event.type==SDL_EVENT_QUIT || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) { running=false; } }
                bool anyActive=false;
                for (size_t index=0;index<outputs.size();++index) {
                    const auto size=windows[index]->GetDrawableSize(); active[index]=size.width && size.height;
                    if (!active[index]) { continue; } anyActive=true;
                    const auto scale=windows[index]->GetDisplayScale();
                    if (size.width!=extents[index].width || size.height!=extents[index].height || scale!=scales[index]) {
                        scales[index]=scale; extents[index]=size; ++generations[index]; render3d.OnWindowResize(static_cast<intptr_t>(index+1),size.width,size.height);
                    }
                }
                if (!anyActive) { SDL_Delay(16); continue; }
            }
            if (replay) { if (options.Offscreen) { input.set_ReceiveInputInBackground(true); } for (const auto& event : replay->EventsForFrame(frames)) { if (event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type==SDL_EVENT_GAMEPAD_BUTTON_UP) { replayGamepad->Apply(event); std::cout << "replay_frame=" << frames << " gamepad_button=" << static_cast<int>(event.gbutton.button) << " down=" << event.gbutton.down << '\n'; } else { input.HandleEvent(event); std::cout << "replay_frame=" << frames << " scancode=" << event.key.scancode << " down=" << event.key.down << '\n'; } } }
            const auto now=std::chrono::steady_clock::now();
            const double elapsed=options.Offscreen ? 1.0/60.0 : std::chrono::duration<double>(now-previous).count(); previous=now;
            const auto updateStart=std::chrono::steady_clock::now(); core.Update(elapsed); const auto drawStart=std::chrono::steady_clock::now(); core.Draw();
            const auto drawEnd=std::chrono::steady_clock::now(); core.SetHostFrameTimings(std::chrono::duration<double,std::milli>(drawStart-updateStart).count(),std::chrono::duration<double,std::milli>(drawEnd-drawStart).count());
            FlushLinuxProfilerFrame(&core);
            auto* loaded=core.get_SceneManager()->get_LoadedScenes();
            if (loaded && loaded->get_Count()>0) { const auto current=loaded->get_Item(loaded->get_Count()-1)->get_SceneId().raw(); if (current!=lastScene) { std::cout << "loaded_scene_frame=" << frames << " scene=" << current << '\n'; lastScene=current; } }
            if (replay) { const auto state=SnapshotMenuState(&core); if (state!=lastMenu) { std::cout << "menu_state_frame=" << frames << ' ' << state << '\n'; lastMenu=state; } }
            for (size_t index=0;index<outputs.size();++index) {
                if (!active[index]) { continue; }
                renderer.SelectOutput(index+1);
                const bool completed=options.Offscreen || windows[index]->Present(renderer,scales[index]);
                if (!completed) { continue; } ++ticks[index];
                if (!options.Outputs.empty()) {
                    std::cout << "HELENGINE_OUTPUT 1 " << (options.Session.empty()?"-":options.Session) << ' ' << outputs[index].Tag << ' ' << generations[index] << ' ' << ticks[index] << ' ' << (render3d.IsOutputReady(static_cast<intptr_t>(index+1))?1:0) << ' ' << renderer.GetWidth() << ' ' << renderer.GetHeight() << '\n' << std::flush;
                }
            }
            renderer.SelectOutput(1); ++frames;
        }
        if (replay && !replay->IsComplete()) { throw std::runtime_error("Linux player stopped before all explicit input replay events were captured."); }
        if (options.RequireShadowReception) { ValidateShadowReception(&core,render3d,renderer.GetWidth(),renderer.GetHeight()); }
        if (graphics.GetValidationErrorCount()) { throw std::runtime_error("Vulkan validation errors occurred during actual generated-core execution."); }
        if (!options.Outputs.empty()) {
            for (size_t index=0;index<outputs.size();++index) {
                const auto& outputPixels=render3d.GetOutputPixels(static_cast<intptr_t>(index+1)); uint64_t outputChecksum=1469598103934665603ULL; size_t zero=0,partial=0,opaque=0;
                for (const auto byte : outputPixels) { outputChecksum=(outputChecksum^byte)*1099511628211ULL; }
                for (size_t pixel=3;pixel<outputPixels.size();pixel+=4) { if (!outputPixels[pixel]) { ++zero; } else if (outputPixels[pixel]==255) { ++opaque; } else { ++partial; } }
                std::cout << "output=" << outputs[index].Tag << " rgba_bytes=" << outputPixels.size() << " checksum=" << outputChecksum << " alpha_zero=" << zero << " alpha_partial=" << partial << " alpha_opaque=" << opaque << '\n';
            }
        }
        uint64_t checksum=1469598103934665603ULL;
        for (uint8_t byte : render3d.GetLastPixels()) { checksum=(checksum^byte)*1099511628211ULL; }
        bool nonuniform=false; const auto& pixels=render3d.GetLastPixels();
        for (size_t index=4;index+3<pixels.size();index+=4) { if (pixels[index]!=pixels[0] || pixels[index+1]!=pixels[1] || pixels[index+2]!=pixels[2] || pixels[index+3]!=pixels[3]) { nonuniform=true; break; } }
        std::cout << "core_frames=" << frames << " draw_calls=" << render3d.get_LastDrawCallCount() << " mesh_visible_samples=" << render3d.GetLastVisibleMeshSamples() << " shadow_visible_samples=" << render3d.GetLastVisibleShadowSamples() << " rgba_bytes=" << pixels.size() << " checksum=" << checksum << " nonuniform=" << nonuniform << '\n';
        ValidateSoftwareTracer(&core,options.RequireTracerProgress,pixels,renderer.GetWidth(),renderer.GetHeight());
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
