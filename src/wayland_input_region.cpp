#include "wayland_input_region.hpp"
#include <cstring>
#include <stdexcept>

namespace helengine::linux {
WaylandInputRegion::WaylandInputRegion(SDL_Window* window) {
    try {
        const char* driver=SDL_GetCurrentVideoDriver();
        if (!window || !driver || std::strcmp(driver,"wayland")!=0) { throw std::runtime_error("Noninteractive outputs require native Wayland input regions."); }
        const auto properties=SDL_GetWindowProperties(window);
        Display=static_cast<wl_display*>(SDL_GetPointerProperty(properties,SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,nullptr));
        auto* surface=static_cast<wl_surface*>(SDL_GetPointerProperty(properties,SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,nullptr));
        if (!Display || !surface) { throw std::runtime_error("SDL did not expose the hidden Wayland display and surface for noninteractive initialization."); }
        Queue=wl_display_create_queue(Display);
        if (!Queue) { throw std::runtime_error("Unable to create the Wayland input initialization queue."); }
        void* wrapper=wl_proxy_create_wrapper(Display);
        if (!wrapper) { throw std::runtime_error("Unable to wrap the SDL Wayland connection for initialization."); }
        wl_proxy_set_queue(static_cast<wl_proxy*>(wrapper),Queue);
        Registry=wl_display_get_registry(static_cast<wl_display*>(wrapper));
        wl_proxy_wrapper_destroy(wrapper);
        if (!Registry) { throw std::runtime_error("Unable to request the Wayland initialization registry."); }
        static const wl_registry_listener Listener {Global,GlobalRemove};
        if (wl_registry_add_listener(Registry,&Listener,this)<0 || wl_display_roundtrip_queue(Display,Queue)<0) { throw std::runtime_error("Wayland compositor discovery failed during noninteractive initialization."); }
        if (!Compositor) { throw std::runtime_error("Noninteractive initialization requires the Wayland compositor interface."); }
        Region=wl_compositor_create_region(Compositor);
        if (!Region) { throw std::runtime_error("Unable to create the empty Wayland input region."); }
        wl_surface_set_input_region(surface,Region);
        wl_region_destroy(Region); Region=nullptr;
        if (wl_display_roundtrip_queue(Display,Queue)<0) { throw std::runtime_error("Wayland rejected the noninteractive surface input region."); }
    } catch (...) { Release(); throw; }
}

WaylandInputRegion::~WaylandInputRegion() { Release(); }

void WaylandInputRegion::Global(void* data,wl_registry* registry,uint32_t name,const char* interface,uint32_t version) {
    auto* instance=static_cast<WaylandInputRegion*>(data);
    if (!instance->Compositor && version>=1 && std::strcmp(interface,wl_compositor_interface.name)==0) {
        instance->Compositor=static_cast<wl_compositor*>(wl_registry_bind(registry,name,&wl_compositor_interface,1));
    }
}

void WaylandInputRegion::GlobalRemove(void* data,wl_registry* registry,uint32_t name) {
    (void)data; (void)registry; (void)name;
}

void WaylandInputRegion::Release() noexcept {
    if (Region) { wl_region_destroy(Region); Region=nullptr; }
    if (Compositor) { wl_compositor_destroy(Compositor); Compositor=nullptr; }
    if (Registry) { wl_registry_destroy(Registry); Registry=nullptr; }
    if (Queue) { wl_event_queue_destroy(Queue); Queue=nullptr; }
}
}
