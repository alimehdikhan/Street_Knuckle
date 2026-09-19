#include "platform.h"

#include <cstdio>
#include <cstring>

namespace {

const int MAX_PADS = 2;

struct PState {
    SDL_Window *window = nullptr;
    void *hwnd = nullptr;
    bool running = true;
    bool resized = false;
    bool focused = true;
    bool keyEdge[SDL_SCANCODE_COUNT]{};
    int width = 0, height = 0;
    Uint64 startCounter = 0;
    double counterFreq = 1.0;

    bool keyCur[SDL_SCANCODE_COUNT]{};
    bool keyPrev[SDL_SCANCODE_COUNT]{};

    SDL_Gamepad *pads[MAX_PADS]{};
    bool padCur[MAX_PADS][SDL_GAMEPAD_BUTTON_COUNT]{};
    bool padPrev[MAX_PADS][SDL_GAMEPAD_BUTTON_COUNT]{};
};

PState S;

void OpenPads() {
    for (int i=0;i<MAX_PADS;++i) {
        if (S.pads[i] && !SDL_GamepadConnected(S.pads[i])) {
            SDL_CloseGamepad(S.pads[i]); S.pads[i] = nullptr;
            memset(S.padCur[i],0,sizeof(S.padCur[i]));
            memset(S.padPrev[i],0,sizeof(S.padPrev[i]));
        }
    }
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids) return;
    for (int i = 0; i < MAX_PADS; i++) {
        if (S.pads[i]) continue;
        for (int j=0;j<count;++j) {
            bool used = false;
            for (auto *pad : S.pads) if (pad && SDL_GetGamepadID(pad) == ids[j]) used = true;
            if (!used) { S.pads[i] = SDL_OpenGamepad(ids[j]); break; }
        }
    }
    SDL_free(ids);
}

}  // namespace

namespace P {

bool Init(const char *title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::printf("[platform] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    // No SDL_WINDOW_OPENGL: the renderer creates its own device on this HWND.
    S.window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
    if (!S.window) {
        std::printf("[platform] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    S.hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(S.window),
                                    SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!S.hwnd) {
        std::printf("[platform] could not obtain a native window handle\n");
        return false;
    }
    S.width = width;
    S.height = height;
    S.counterFreq = (double)SDL_GetPerformanceFrequency();
    S.startCounter = SDL_GetPerformanceCounter();
    OpenPads();
    return true;
}

void Shutdown() {
    for (int i = 0; i < MAX_PADS; i++) {
        if (S.pads[i]) SDL_CloseGamepad(S.pads[i]);
        S.pads[i] = nullptr;
    }
    if (S.window) SDL_DestroyWindow(S.window);
    S.window = nullptr;
    SDL_Quit();
}

void *NativeWindow() { return S.hwnd; }

bool PumpEvents() {
    memcpy(S.keyPrev, S.keyCur, sizeof(S.keyCur));
    memcpy(S.padPrev, S.padCur, sizeof(S.padCur));
    S.resized = false;
    memset(S.keyEdge, 0, sizeof(S.keyEdge));

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
                S.running = false;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                S.running = false;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_RESIZED:
                S.width = e.window.data1;
                S.height = e.window.data2;
                S.resized = true;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                S.focused = false;
                memset(S.keyCur,0,sizeof(S.keyCur));
                memset(S.keyEdge,0,sizeof(S.keyEdge));
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                S.focused = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!e.key.repeat && e.key.scancode < SDL_SCANCODE_COUNT) {
                    S.keyCur[e.key.scancode] = true;
                    S.keyEdge[e.key.scancode] = true;
                }
                break;
            case SDL_EVENT_KEY_UP:
                if (e.key.scancode < SDL_SCANCODE_COUNT) S.keyCur[e.key.scancode] = false;
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_REMOVED:
                OpenPads();
                break;
            default:
                break;
        }
    }

    for (int i = 0; i < MAX_PADS; i++) {
        if (!S.pads[i]) continue;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
            S.padCur[i][b] = SDL_GetGamepadButton(S.pads[i], (SDL_GamepadButton)b);
    }
    return S.running;
}

void Size(int &width, int &height) {
    width = S.width;
    height = S.height;
}
bool Resized() { return S.resized; }
bool Focused() { return S.focused; }

double Time() {
    return (double)(SDL_GetPerformanceCounter() - S.startCounter) / S.counterFreq;
}

bool KeyDown(SDL_Scancode sc) {
    return sc >= 0 && sc < SDL_SCANCODE_COUNT && S.keyCur[sc];
}
bool KeyPressed(SDL_Scancode sc) {
    return sc >= 0 && sc < SDL_SCANCODE_COUNT && S.keyEdge[sc];
}

bool PadAvailable(int pad) { return pad >= 0 && pad < MAX_PADS && S.pads[pad] != nullptr; }

bool PadDown(int pad, SDL_GamepadButton b) {
    return PadAvailable(pad) && b >= 0 && b < SDL_GAMEPAD_BUTTON_COUNT && S.padCur[pad][b];
}
bool PadPressed(int pad, SDL_GamepadButton b) {
    return PadAvailable(pad) && b >= 0 && b < SDL_GAMEPAD_BUTTON_COUNT &&
           S.padCur[pad][b] && !S.padPrev[pad][b];
}
float PadAxis(int pad, SDL_GamepadAxis a) {
    if (!PadAvailable(pad)) return 0.0f;
    return SDL_GetGamepadAxis(S.pads[pad], a) / 32767.0f;
}

}  // namespace P
