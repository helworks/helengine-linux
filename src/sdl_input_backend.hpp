#pragma once
#include "IInputBackend.hpp"
#include "KeyboardState.hpp"
#include "MouseState.hpp"
#include "Keys.hpp"
#include <SDL3/SDL.h>
#include <array>

namespace helengine::linux {
/// <summary>Converts SDL events and device snapshots into the generated desktop input contract without sampling another application's controls.</summary>
class SdlInputBackend final : public ::IInputBackend {
public:
    /// <summary>Creates a provider; offscreen execution accepts only explicitly injected events and never polls desktop devices.</summary>
    explicit SdlInputBackend(bool offscreen);
    /// <summary>Closes gamepads opened by this provider.</summary>
    ~SdlInputBackend() override;
    /// <summary>Reports whether the application explicitly permits keyboard/button input without focus.</summary>
    bool get_ReceiveInputInBackground() override { return BackgroundInput; }
    /// <summary>Changes the foreground input policy through the generated interface.</summary>
    void set_ReceiveInputInBackground(bool value) override { BackgroundInput = value; }
    /// <summary>Accumulates keyboard, wheel, focus and controller lifecycle events exactly once in the host event loop.</summary>
    void HandleEvent(const SDL_Event& event);
    /// <summary>Captures one frame in physical drawable coordinates while preserving portable key and gamepad IDs.</summary>
    ::InputFrameState CaptureFrame() override;
    /// <summary>Maps SDL physical scancodes to engine desktop keys without relying on Win32 virtual-key APIs.</summary>
    static ::Keys TranslateScancode(SDL_Scancode scancode);
private:
    /// <summary>Opens a new attached gamepad into the first available stable slot.</summary>
    void OpenGamepad(SDL_JoystickID id);
    /// <summary>Borrows no desktop input APIs during explicit offscreen execution.</summary>
    bool Offscreen;
    /// <summary>Stores the generated foreground input preference.</summary>
    bool BackgroundInput = false;
    /// <summary>Stores keyboard events independently from SDL's global state for deterministic isolated tests.</summary>
    std::array<bool,SDL_SCANCODE_COUNT> Pressed {};
    /// <summary>Accumulates vertical wheel detents in the engine's desktop 120-unit convention.</summary>
    int32_t VerticalWheel = 0;
    /// <summary>Accumulates horizontal wheel detents independently of the vertical axis.</summary>
    int32_t HorizontalWheel = 0;
    /// <summary>Retains the previous physical cursor coordinate for portable pointer delta events.</summary>
    int32_t PreviousX = 0, PreviousY = 0;
    /// <summary>Retains the previous accumulated wheel value for one-frame portable scroll deltas.</summary>
    int32_t PreviousWheel = 0;
    /// <summary>Suppresses an invented cursor delta on the first capture.</summary>
    bool HasPreviousPointer = false;
    /// <summary>Stores native controller ownership in stable portable slots.</summary>
    std::array<SDL_Gamepad*,4> Gamepads {};
    /// <summary>Owns captured portable controller values for the duration of the host frame.</summary>
    Array<::InputGamepadState> GamepadFrames {4};
};
}
