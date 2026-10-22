#include "sdl_input_backend.hpp"
#include "InputGamepadButton.hpp"
#include "InputPointerButton.hpp"
#include "ButtonState.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace helengine::linux {
SdlInputBackend::SdlInputBackend(bool offscreen) : Offscreen(offscreen) {
    if (!Offscreen) {
        int count = 0; SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (!ids) { throw std::runtime_error(std::string("SDL_GetGamepads: ") + SDL_GetError()); }
        try { for (int index = 0; index < count; ++index) { OpenGamepad(ids[index]); } }
        catch (...) { SDL_free(ids); for (auto* gamepad : Gamepads) { if (gamepad) { SDL_CloseGamepad(gamepad); } } throw; }
        SDL_free(ids);
    }
}

SdlInputBackend::~SdlInputBackend() { for (auto* gamepad : Gamepads) { if (gamepad) { SDL_CloseGamepad(gamepad); } } }

void SdlInputBackend::OpenGamepad(SDL_JoystickID id) {
    for (auto* gamepad : Gamepads) { if (gamepad && SDL_GetGamepadID(gamepad) == id) { return; } }
    for (auto& slot : Gamepads) { if (!slot) { slot = SDL_OpenGamepad(id); if (!slot) { throw std::runtime_error(std::string("SDL_OpenGamepad: ") + SDL_GetError()); } return; } }
}

void SdlInputBackend::HandleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) { if (event.key.scancode >= 0 && event.key.scancode < SDL_SCANCODE_COUNT) { Pressed[event.key.scancode] = event.type == SDL_EVENT_KEY_DOWN; } }
    else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) { Pressed.fill(false); }
    else if (event.type == SDL_EVENT_MOUSE_WHEEL) { VerticalWheel += static_cast<int32_t>(std::lround(event.wheel.y * 120)); HorizontalWheel += static_cast<int32_t>(std::lround(event.wheel.x * 120)); }
    else if (event.type == SDL_EVENT_GAMEPAD_ADDED && !Offscreen) { OpenGamepad(event.gdevice.which); }
    else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) { for (auto& slot : Gamepads) { if (slot && SDL_GetGamepadID(slot) == event.gdevice.which) { SDL_CloseGamepad(slot); slot = nullptr; } } }
}

::Keys SdlInputBackend::TranslateScancode(SDL_Scancode scancode) {
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) { return static_cast<::Keys>('A' + scancode - SDL_SCANCODE_A); }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) { return static_cast<::Keys>('1' + scancode - SDL_SCANCODE_1); }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12) { return static_cast<::Keys>(112 + scancode - SDL_SCANCODE_F1); }
    switch (scancode) {
        case SDL_SCANCODE_0: return ::Keys::D0; case SDL_SCANCODE_RETURN: return ::Keys::Enter; case SDL_SCANCODE_ESCAPE: return ::Keys::Escape;
        case SDL_SCANCODE_SPACE: return ::Keys::Space; case SDL_SCANCODE_TAB: return ::Keys::Tab; case SDL_SCANCODE_BACKSPACE: return ::Keys::Back;
        case SDL_SCANCODE_UP: return ::Keys::Up; case SDL_SCANCODE_DOWN: return ::Keys::Down; case SDL_SCANCODE_LEFT: return ::Keys::Left; case SDL_SCANCODE_RIGHT: return ::Keys::Right;
        case SDL_SCANCODE_LSHIFT: return ::Keys::LeftShift; case SDL_SCANCODE_RSHIFT: return ::Keys::RightShift;
        case SDL_SCANCODE_LCTRL: return ::Keys::LeftControl; case SDL_SCANCODE_RCTRL: return ::Keys::RightControl;
        case SDL_SCANCODE_LALT: return ::Keys::LeftAlt; case SDL_SCANCODE_RALT: return ::Keys::RightAlt;
        case SDL_SCANCODE_HOME: return ::Keys::Home; case SDL_SCANCODE_END: return ::Keys::End; case SDL_SCANCODE_PAGEUP: return ::Keys::PageUp; case SDL_SCANCODE_PAGEDOWN: return ::Keys::PageDown;
        case SDL_SCANCODE_DELETE: return ::Keys::Delete; case SDL_SCANCODE_INSERT: return ::Keys::Insert;
        default: return ::Keys::None;
    }
}

::InputFrameState SdlInputBackend::CaptureFrame() {
    ::InputFrameState frame;
    const bool active = BackgroundInput || (!Offscreen && SDL_GetKeyboardFocus());
    List<::Keys> keys;
    if (active) { for (int index = 0; index < SDL_SCANCODE_COUNT; ++index) { if (Pressed[index]) { auto key = TranslateScancode(static_cast<SDL_Scancode>(index)); if (key != ::Keys::None) { keys.Add(key); } } } }
    const SDL_Keymod modifiers = Offscreen ? SDL_KMOD_NONE : SDL_GetModState();
    frame.Keyboard = ::KeyboardState(&keys,(modifiers & SDL_KMOD_CAPS) != 0,(modifiers & SDL_KMOD_NUM) != 0);
    float x = 0,y = 0; const auto buttons = Offscreen ? 0 : SDL_GetMouseState(&x,&y);
    if (!Offscreen) {
        auto* window = SDL_GetMouseFocus(); if (!window) { window = SDL_GetKeyboardFocus(); }
        if (window) { int width=0,height=0,pixelsX=0,pixelsY=0; if (!SDL_GetWindowSize(window,&width,&height) || !SDL_GetWindowSizeInPixels(window,&pixelsX,&pixelsY)) { throw std::runtime_error(std::string("SDL input window extent: ") + SDL_GetError()); } if (width>0 && height>0) { x *= static_cast<float>(pixelsX)/width; y *= static_cast<float>(pixelsY)/height; } }
    }
    frame.Mouse = ::MouseState(static_cast<int32_t>(x),static_cast<int32_t>(y),VerticalWheel,
        active && (buttons & SDL_BUTTON_LMASK) ? ::ButtonState::Pressed : ::ButtonState::Released,
        active && (buttons & SDL_BUTTON_MMASK) ? ::ButtonState::Pressed : ::ButtonState::Released,
        active && (buttons & SDL_BUTTON_RMASK) ? ::ButtonState::Pressed : ::ButtonState::Released,
        active && (buttons & SDL_BUTTON_X1MASK) ? ::ButtonState::Pressed : ::ButtonState::Released,
        active && (buttons & SDL_BUTTON_X2MASK) ? ::ButtonState::Pressed : ::ButtonState::Released,HorizontalWheel);
    frame.Pointer.Connected = !Offscreen; frame.Pointer.X = frame.Mouse.get_X(); frame.Pointer.Y = frame.Mouse.get_Y();
    frame.Pointer.DeltaX=HasPreviousPointer ? frame.Pointer.X-PreviousX : 0; frame.Pointer.DeltaY=HasPreviousPointer ? frame.Pointer.Y-PreviousY : 0;
    frame.Pointer.ScrollDelta=VerticalWheel-PreviousWheel; PreviousX=frame.Pointer.X; PreviousY=frame.Pointer.Y; PreviousWheel=VerticalWheel; HasPreviousPointer=true;
    frame.Pointer.SetButtonDown(::InputPointerButton::Primary,frame.Mouse.get_LeftButton() == ::ButtonState::Pressed);
    frame.Pointer.SetButtonDown(::InputPointerButton::Secondary,frame.Mouse.get_RightButton() == ::ButtonState::Pressed);
    frame.Pointer.SetButtonDown(::InputPointerButton::Middle,frame.Mouse.get_MiddleButton() == ::ButtonState::Pressed);
    frame.Pointer.SetButtonDown(::InputPointerButton::Back,frame.Mouse.get_XButton1() == ::ButtonState::Pressed);
    frame.Pointer.SetButtonDown(::InputPointerButton::Forward,frame.Mouse.get_XButton2() == ::ButtonState::Pressed);
    for (size_t index=0;index<Gamepads.size();++index) {
        auto& portable = GamepadFrames[static_cast<int32_t>(index)]; portable = ::InputGamepadState(); auto* gamepad = Gamepads[index]; if (!gamepad || !SDL_GamepadConnected(gamepad) || !active) { continue; }
        portable.Connected = true;
        portable.LeftStickX=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTX); portable.LeftStickY=static_cast<int16_t>(std::clamp(-static_cast<int>(SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTY)),-32768,32767));
        portable.RightStickX=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_RIGHTX); portable.RightStickY=static_cast<int16_t>(std::clamp(-static_cast<int>(SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_RIGHTY)),-32768,32767));
        portable.LeftTrigger=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER); portable.RightTrigger=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        portable.SetButtonDown(::InputGamepadButton::South,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_SOUTH)); portable.SetButtonDown(::InputGamepadButton::East,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_EAST));
        portable.SetButtonDown(::InputGamepadButton::West,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_WEST)); portable.SetButtonDown(::InputGamepadButton::North,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_NORTH));
        portable.SetButtonDown(::InputGamepadButton::DPadUp,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_UP)); portable.SetButtonDown(::InputGamepadButton::DPadDown,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        portable.SetButtonDown(::InputGamepadButton::DPadLeft,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_LEFT)); portable.SetButtonDown(::InputGamepadButton::DPadRight,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
        portable.SetButtonDown(::InputGamepadButton::Start,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_START)); portable.SetButtonDown(::InputGamepadButton::Select,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_BACK));
        portable.SetButtonDown(::InputGamepadButton::LeftShoulder,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)); portable.SetButtonDown(::InputGamepadButton::RightShoulder,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        portable.SetButtonDown(::InputGamepadButton::LeftStick,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_LEFT_STICK)); portable.SetButtonDown(::InputGamepadButton::RightStick,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_RIGHT_STICK));
        portable.SetButtonDown(::InputGamepadButton::LeftTrigger,portable.LeftTrigger>16383); portable.SetButtonDown(::InputGamepadButton::RightTrigger,portable.RightTrigger>16383); portable.SetButtonDown(::InputGamepadButton::Home,SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_GUIDE));
    }
    frame.Gamepads=&GamepadFrames; frame.GamepadCount=static_cast<int32_t>(Gamepads.size()); return frame;
}
}
