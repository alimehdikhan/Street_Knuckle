// =============================================================================
//  platform.h - window, input and timing, on SDL3.
//
//  The window is created without a graphics context: Diligent takes the raw
//  HWND and owns the device and swapchain itself.
// =============================================================================
#pragma once

#include <SDL3/SDL.h>

namespace P {

bool Init(const char *title, int width, int height);
void Shutdown();

// HWND for the created window, handed straight to the renderer.
void *NativeWindow();

// Drains the event queue and rolls the keyboard edge state forward.
// Returns false once the window has been closed.
bool PumpEvents();

void Size(int &width, int &height);
bool Focused();
bool Resized();            // true for one frame after a size change
double Time();             // seconds since init

bool KeyDown(SDL_Scancode sc);
bool KeyPressed(SDL_Scancode sc);

bool PadAvailable(int pad);
bool PadDown(int pad, SDL_GamepadButton b);
bool PadPressed(int pad, SDL_GamepadButton b);
float PadAxis(int pad, SDL_GamepadAxis a);

}  // namespace P
