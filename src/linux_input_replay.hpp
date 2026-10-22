#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <istream>
#include <span>
#include <vector>

namespace helengine::linux {
/// <summary>Reads an explicit frame-indexed validation script and emits ordinary SDL keyboard events for the real platform input provider.</summary>
class LinuxInputReplay final {
public:
    /// <summary>Parses ascending frame/down-or-up/scancode-name lines; rejects malformed or unknown keys before the player starts.</summary>
    explicit LinuxInputReplay(std::istream& source);
    /// <summary>Returns authored events for one monotonically advancing Core frame; events are never dispatched to scene/menu APIs directly.</summary>
    std::span<const SDL_Event> EventsForFrame(uint32_t frame);
    /// <summary>Reports whether every authored event has entered the same SDL input provider used by the host.</summary>
    bool IsComplete() const { return Cursor==Events.size(); }
private:
    /// <summary>Retains ascending authored frame numbers parallel to the SDL event stream.</summary>
    std::vector<uint32_t> Frames;
    /// <summary>Owns ordinary SDL press/release events, preserving simultaneous authored order.</summary>
    std::vector<SDL_Event> Events;
    /// <summary>Identifies the first event not yet returned to the host.</summary>
    size_t Cursor=0;
};
}
