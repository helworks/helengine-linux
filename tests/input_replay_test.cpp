#include "linux_input_replay.hpp"
#include <sstream>
#include <iostream>
#include <stdexcept>

/// <summary>Verifies deterministic SDL event timing, press/release identity and malformed script rejection without a Core, GPU or desktop.</summary>
int main() {
    try {
        std::istringstream script("# frame action SDL-scancode-name\n2 down Down\n2 down Return\n3 up Return\n4 up Down\n");
        helengine::linux::LinuxInputReplay replay(script);
        if (!replay.EventsForFrame(0).empty() || !replay.EventsForFrame(1).empty()) { throw std::runtime_error("Replay invented early input."); }
        const auto frame=replay.EventsForFrame(2);
        if (frame.size()!=2 || frame[0].type!=SDL_EVENT_KEY_DOWN || frame[0].key.scancode!=SDL_SCANCODE_DOWN || frame[1].key.scancode!=SDL_SCANCODE_RETURN) { throw std::runtime_error("Replay changed authored simultaneous SDL keys."); }
        if (replay.EventsForFrame(3)[0].type!=SDL_EVENT_KEY_UP || replay.EventsForFrame(4)[0].key.scancode!=SDL_SCANCODE_DOWN || !replay.IsComplete()) { throw std::runtime_error("Replay release timing or completion changed."); }
        bool rejected=false; try { std::istringstream bad("3 down Down\n2 up Down\n"); helengine::linux::LinuxInputReplay invalid(bad); } catch (const std::invalid_argument&) { rejected=true; }
        if (!rejected) { throw std::runtime_error("Replay accepted unordered event frames."); }
        rejected=false; try { std::istringstream bad("0 down NotAKey\n"); helengine::linux::LinuxInputReplay invalid(bad); } catch (const std::invalid_argument&) { rejected=true; }
        if (!rejected) { throw std::runtime_error("Replay accepted an unknown physical key."); }
        std::cout << "PASS SDL input replay frame-timing simultaneous press release invalid-order invalid-key\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
