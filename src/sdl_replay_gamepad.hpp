#pragma once
#include <SDL3/SDL.h>

namespace helengine::linux {
/// <summary>Owns one explicitly requested SDL virtual gamepad for deterministic validation through ordinary SDL device snapshots.</summary>
class SdlReplayGamepad final {
public:
    /// <summary>Initializes the gamepad subsystem and attaches one virtual standard controller without creating or reading a desktop window.</summary>
    SdlReplayGamepad();
    /// <summary>Closes and detaches the virtual controller before releasing this object's subsystem initialization reference.</summary>
    ~SdlReplayGamepad();
    /// <summary>Prevents copied SDL joystick ownership.</summary>
    SdlReplayGamepad(const SdlReplayGamepad&)=delete;
    /// <summary>Prevents assignment of a second virtual-controller owner.</summary>
    SdlReplayGamepad& operator=(const SdlReplayGamepad&)=delete;
    /// <summary>Exposes only this explicitly created virtual device to the normal input backend.</summary>
    SDL_JoystickID GetId() const { return Id; }
    /// <summary>Applies one authored button event to SDL and updates its ordinary gamepad snapshot before Core.Update captures input.</summary>
    void Apply(const SDL_Event& event);
private:
    /// <summary>Releases handles successfully created during full or partial construction.</summary>
    void Release() noexcept;
    /// <summary>Stores this object's explicit virtual-controller instance identifier.</summary>
    SDL_JoystickID Id=0;
    /// <summary>Owns the joystick handle used only to set virtual button state.</summary>
    SDL_Joystick* Joystick=nullptr;
    /// <summary>Tracks this object's successful gamepad subsystem initialization reference.</summary>
    bool Initialized=false;
};
}
