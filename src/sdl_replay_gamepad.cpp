#include "sdl_replay_gamepad.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace helengine::linux {
SdlReplayGamepad::SdlReplayGamepad() {
    try {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) { throw std::runtime_error(std::string("SDL_InitSubSystem(gamepad replay): ")+SDL_GetError()); }
        Initialized=true; SDL_VirtualJoystickDesc description; SDL_INIT_INTERFACE(&description);
        description.type=SDL_JOYSTICK_TYPE_GAMEPAD; description.naxes=SDL_GAMEPAD_AXIS_COUNT; description.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
        description.axis_mask=(1U<<SDL_GAMEPAD_AXIS_COUNT)-1; description.button_mask=(1U<<SDL_GAMEPAD_BUTTON_COUNT)-1; description.name="HelEngine explicit validation replay";
        Id=SDL_AttachVirtualJoystick(&description);
        if (!Id) { throw std::runtime_error(std::string("SDL_AttachVirtualJoystick(replay): ")+SDL_GetError()); }
        Joystick=SDL_OpenJoystick(Id);
        if (!Joystick) { throw std::runtime_error(std::string("SDL_OpenJoystick(replay): ")+SDL_GetError()); }
    } catch (...) { Release(); throw; }
}

SdlReplayGamepad::~SdlReplayGamepad() { Release(); }

void SdlReplayGamepad::Release() noexcept {
    if (Joystick) { SDL_CloseJoystick(Joystick); Joystick=nullptr; }
    if (Id) { if (!SDL_DetachVirtualJoystick(Id)) { std::cerr << "SDL replay detach failed: " << SDL_GetError() << '\n'; } Id=0; }
    if (Initialized) { SDL_QuitSubSystem(SDL_INIT_GAMEPAD); Initialized=false; }
}

void SdlReplayGamepad::Apply(const SDL_Event& event) {
    if (event.type!=SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.type!=SDL_EVENT_GAMEPAD_BUTTON_UP) { throw std::invalid_argument("SDL replay gamepad only accepts authored button press/release events."); }
    if (!SDL_SetJoystickVirtualButton(Joystick,event.gbutton.button,event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN)) { throw std::runtime_error(std::string("SDL_SetJoystickVirtualButton(replay): ")+SDL_GetError()); }
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
}
}
