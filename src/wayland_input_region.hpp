#pragma once
#include <SDL3/SDL.h>
#include <wayland-client.h>

namespace helengine::linux {
/// <summary>Installs a compositor-level empty input region on one hidden SDL Wayland surface without dispatching SDL's event queue.</summary>
class WaylandInputRegion final {
public:
    /// <summary>Requires the real SDL Wayland surface and submits its empty input state before the caller shows the window.</summary>
    explicit WaylandInputRegion(SDL_Window* window);
    /// <summary>Releases only the temporary registry, compositor and event queue; SDL retains its display and surface.</summary>
    ~WaylandInputRegion();
    /// <summary>Prevents copying the temporary Wayland protocol ownership.</summary>
    WaylandInputRegion(const WaylandInputRegion&) = delete;
    /// <summary>Prevents assigning duplicate ownership of the initialization objects.</summary>
    WaylandInputRegion& operator=(const WaylandInputRegion&) = delete;
private:
    /// <summary>Binds the compositor advertised to this private registry at core protocol version one.</summary>
    static void Global(void* data,wl_registry* registry,uint32_t name,const char* interface,uint32_t version);
    /// <summary>Leaves already-bound compositor objects alive when their global advertisement is removed.</summary>
    static void GlobalRemove(void* data,wl_registry* registry,uint32_t name);
    /// <summary>Releases partially initialized native objects after either success or an explicit protocol failure.</summary>
    void Release() noexcept;
    /// <summary>Borrows SDL's existing connection rather than connecting to another display.</summary>
    wl_display* Display = nullptr;
    /// <summary>Owns an isolated callback queue so initialization cannot run unrelated SDL callbacks.</summary>
    wl_event_queue* Queue = nullptr;
    /// <summary>Owns the initialization registry used to discover the required core compositor interface.</summary>
    wl_registry* Registry = nullptr;
    /// <summary>Owns the compositor binding used only to create the empty region.</summary>
    wl_compositor* Compositor = nullptr;
    /// <summary>Owns the region until its state has been copied into the SDL surface's pending input state.</summary>
    wl_region* Region = nullptr;
};
}
