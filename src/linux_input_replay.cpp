#include "linux_input_replay.hpp"
#include <sstream>
#include <string>
#include <stdexcept>

namespace helengine::linux {
LinuxInputReplay::LinuxInputReplay(std::istream& source) {
    std::string line; uint32_t lineNumber=0;
    while (std::getline(source,line)) {
        ++lineNumber; const auto first=line.find_first_not_of(" \t\r"); if (first==std::string::npos || line[first]=='#') { continue; }
        uint64_t frame=0; std::string action,key,extra; std::istringstream fields(line);
        if (!(fields>>frame>>action>>key) || (fields>>extra) || frame>UINT32_MAX || (action!="down" && action!="up")) { throw std::invalid_argument("Invalid SDL input replay line "+std::to_string(lineNumber)+"; expected: frame down|up SDL-scancode-name."); }
        if (!Frames.empty() && frame<Frames.back()) { throw std::invalid_argument("SDL input replay frames must be in ascending order."); }
        const auto scancode=SDL_GetScancodeFromName(key.c_str());
        if (scancode==SDL_SCANCODE_UNKNOWN) { throw std::invalid_argument("Unknown SDL input replay physical key at line "+std::to_string(lineNumber)+": "+key); }
        SDL_Event event {}; event.type=action=="down"?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP; event.key.scancode=scancode; event.key.down=action=="down";
        Frames.push_back(static_cast<uint32_t>(frame)); Events.push_back(event);
    }
    if (!source.eof() || Events.empty()) { throw std::invalid_argument("SDL input replay requires a readable script containing at least one event."); }
}

std::span<const SDL_Event> LinuxInputReplay::EventsForFrame(uint32_t frame) {
    if (Cursor<Frames.size() && Frames[Cursor]<frame) { throw std::logic_error("SDL input replay skipped an authored Core frame."); }
    const size_t begin=Cursor; while (Cursor<Frames.size() && Frames[Cursor]==frame) { ++Cursor; }
    return std::span<const SDL_Event>(Events).subspan(begin,Cursor-begin);
}
}
