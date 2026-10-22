#include "sdl_input_backend.hpp"
#include <iostream>
#include <stdexcept>

/// <summary>Exercises the real generated desktop keyboard snapshot through injected SDL events without polling or controlling a desktop.</summary>
int main() {
    try {
        helengine::linux::SdlInputBackend input(true); input.set_ReceiveInputInBackground(true);
        if (helengine::linux::SdlInputBackend::TranslateScancode(SDL_SCANCODE_UP)!=::Keys::Up || helengine::linux::SdlInputBackend::TranslateScancode(SDL_SCANCODE_RETURN)!=::Keys::Enter || helengine::linux::SdlInputBackend::TranslateScancode(SDL_SCANCODE_W)!=::Keys::W) { throw std::runtime_error("SDL-to-engine desktop key identifiers changed."); }
        SDL_Event event {}; event.type=SDL_EVENT_KEY_DOWN; event.key.scancode=SDL_SCANCODE_DOWN; input.HandleEvent(event);
        auto frame=input.CaptureFrame(); if (!frame.Keyboard.IsKeyDown(::Keys::Down) || frame.Keyboard.IsKeyDown(::Keys::Up)) { throw std::runtime_error("Injected menu-navigation key was not captured correctly."); }
        event.type=SDL_EVENT_KEY_UP; input.HandleEvent(event); frame=input.CaptureFrame();
        if (frame.Keyboard.IsKeyDown(::Keys::Down)) { throw std::runtime_error("Released SDL key remained pressed in generated input."); }
        event.type=SDL_EVENT_KEY_DOWN; input.HandleEvent(event); event.type=SDL_EVENT_WINDOW_FOCUS_LOST; input.HandleEvent(event); frame=input.CaptureFrame();
        if (frame.Keyboard.IsKeyDown(::Keys::Down)) { throw std::runtime_error("Focus loss leaked a pressed keyboard gesture."); }
        event.type=SDL_EVENT_MOUSE_WHEEL; event.wheel.y=1; input.HandleEvent(event); frame=input.CaptureFrame();
        if (frame.Mouse.get_ScrollWheelValue()!=120 || frame.Pointer.ScrollDelta!=120) { throw std::runtime_error("SDL wheel did not preserve desktop units and one-frame pointer scroll delta."); }
        frame=input.CaptureFrame(); if (frame.Pointer.ScrollDelta!=0) { throw std::runtime_error("Portable scroll delta repeated on the next capture."); }
        input.set_ReceiveInputInBackground(false); event.type=SDL_EVENT_KEY_DOWN; input.HandleEvent(event); frame=input.CaptureFrame();
        if (frame.Keyboard.IsKeyDown(::Keys::Down)) { throw std::runtime_error("Offscreen input polled or accepted an unapproved background gesture."); }
        std::cout << "PASS generated desktop input SDL mapping press release focus scroll policy no-desktop-poll\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
