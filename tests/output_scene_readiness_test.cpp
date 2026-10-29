#include "vulkan_render_manager_3d.hpp"
#include "vulkan_renderer_2d.hpp"
#include "vulkan_render_manager_2d.hpp"
#include "sdl_input_backend.hpp"
#include "generated_disposable_deleter.hpp"
#include "Core.hpp"
#include "CoreInitializationOptions.hpp"
#include "PlatformInfo.hpp"
#include "HostFileSystemContentStreamSource.hpp"
#include "RuntimeSceneCatalog.hpp"
#include "RuntimeSceneCatalogEntry.hpp"
#include "SceneManager.hpp"
#include "CameraComponent.hpp"
#include "Entity.hpp"
#include "runtime/runtime_startup_manifest.hpp"
#include <iostream>
#include <stdexcept>
using namespace helengine::vulkan;

/// <summary>Creates a real generated entity/camera registered through the ordinary Core component lifecycle.</summary>
::Entity* CreateCamera(::Core& core,const char* tag) {
    auto* entity=new ::Entity(&core); entity->InitChildren(); entity->InitComponents();
    auto* camera=new ::CameraComponent(); camera->set_OutputWindowTag(tag); entity->AddComponent(camera); entity->InitializeHierarchy();
    return entity;
}

/// <summary>Exercises actual Core scene ownership and native GPU clears without mocking rendering or connecting a display.</summary>
int main(int argc,char** argv) {
    try {
        if (argc!=3) { throw std::invalid_argument("Use CONTENT readiness|alpha|legacy."); }
        const std::string selected=argv[2];
        helengine::vulkan::VulkanContext context({},true,VK_API_VERSION_1_2);
        helengine::vulkan::VulkanRenderer2D graphics(context,32,24);
        helengine::vulkan::VulkanRenderManager2D ui(graphics);
        helengine::vulkan::VulkanRenderManager3D scene(graphics,ui);
        scene.AddWindow(1,32,24);
        if (selected!="legacy") {
            scene.SetWindowTag(1,"main"); scene.AddWindow(2,24,16); scene.SetWindowTag(2,"glass"); scene.SetOutputTransparent(2,true);
            scene.AddWindow(3,16,12); scene.SetWindowTag(3,"absent-output"); scene.SetOutputTransparent(3,true);
        }
        helengine::linux::SdlInputBackend input(true);
        ::HostFileSystemContentStreamSource content(argv[1]);
        ::RuntimeSceneCatalogEntry entry("completed-scene",he_get_runtime_startup_scene_relative_path());
        Array<::RuntimeSceneCatalogEntry*> entries(1); entries[0]=&entry;
        helengine::vulkan::GeneratedDisposableOwner<::RuntimeSceneCatalog> catalog(new ::RuntimeSceneCatalog(&entries));
        ::CoreInitializationOptions initialization; initialization.ContentStreamSource=&content; initialization.SceneCatalog=catalog.get();
        ::Core core(&initialization); ::PlatformInfo platform("linux","test"); core.Initialize(&scene,&ui,&input,&platform,&initialization);
        auto* mainRoot=CreateCamera(core,selected=="legacy"?"":"main");
        auto* bootstrap=selected=="legacy"?nullptr:CreateCamera(core,"glass");
        auto* roots=new List<::Entity*>(); roots->Add(mainRoot);
        core.get_SceneManager()->TrackExternallyLoadedScene("completed-scene",roots,false);
        auto* glass=bootstrap?dynamic_cast<::CameraComponent*>(bootstrap->get_Components()->get_Item(0)):nullptr;
        if (selected=="legacy") {
            core.Draw();
            if (scene.get_WindowCount()!=1 || !scene.IsOutputReady(1) || scene.GetLastPixels().size()!=32*24*4) { throw std::runtime_error("Single-output Core default camera dispatch or completed GPU readback changed."); }
        } else if (selected=="readiness") {
            core.Draw();
            if (!scene.IsOutputReady(1) || scene.IsOutputReady(2) || scene.IsOutputReady(3)) { throw std::runtime_error("Loading-only glass inherited readiness from an unrelated completed main scene."); }
            mainRoot->AddChild(bootstrap); core.Draw();
            if (!scene.IsOutputReady(2)) { throw std::runtime_error("Completed-scene descendant camera failed readiness."); }
            scene.OnWindowResize(2,48,32);
            if (scene.IsOutputReady(2) || !scene.IsOutputReady(1)) { throw std::runtime_error("Resize did not independently revoke the affected output readiness."); }
            core.Draw();
            if (!scene.IsOutputReady(2) || scene.GetOutputPixels(2).size()!=48*32*4) { throw std::runtime_error("Resized output failed to re-render and restore readiness at its actual new dimensions."); }
            bootstrap->set_Enabled(false); core.Draw();
            if (scene.IsOutputReady(2)) { throw std::runtime_error("Disabled scene camera retained readiness."); }
        } else if (selected=="alpha") {
            mainRoot->AddChild(bootstrap);
            auto clear=glass->get_ClearSettings(); clear.ClearColor=::float4(1.0f,0.5f,0.25f,0.5f); glass->set_ClearSettings(clear); core.Draw();
            auto* mainCamera=dynamic_cast<::CameraComponent*>(mainRoot->get_Components()->get_Item(0));
            auto mainClear=mainCamera->get_ClearSettings(); mainClear.ClearColor=::float4(1.0f,0.5f,0.25f,0.5f); mainCamera->set_ClearSettings(mainClear); core.Draw();
            const auto& opaque=scene.GetOutputPixels(1);
            if (opaque[0]!=255 || opaque[1]<126 || opaque[1]>129 || opaque[2]<63 || opaque[2]>65 || opaque[3]!=255) { throw std::runtime_error("Opaque output incorrectly premultiplied RGB by an authored transparent clear alpha."); }
            const auto half=scene.GetOutputPixels(2);
            if (half[0]<126 || half[0]>129 || half[1]<63 || half[1]>65 || half[2]<31 || half[2]>33 || half[3]<126 || half[3]>129) { throw std::runtime_error("Transparent colored clear is not premultiplied RGB/A on the actual GPU target."); }
            clear.ClearColor=::float4(1.0f,0.5f,0.25f,0.0f); glass->set_ClearSettings(clear); core.Draw();
            const auto& empty=scene.GetOutputPixels(2);
            for (const auto byte : empty) { if (byte) { throw std::runtime_error("Zero-alpha colored clear leaked RGB into the target."); } }
        } else { throw std::invalid_argument("Unknown scene output test case."); }
        core.Dispose();
        if (context.GetValidationErrorCount()) { throw std::runtime_error("Actual Core/GPU scene-output test emitted Vulkan validation errors."); }
        std::cout << "PASS actual Core scene-output " << selected << '\n'; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
      catch (const std::exception* error) { std::cerr << "FAIL generated: " << (error?error->what():"null exception") << '\n'; delete error; return 1; }
}
