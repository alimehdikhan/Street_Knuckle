# Steel Knuckle

A 3D fighting game in C++17. SDL3 handles the window, input and audio;
[Diligent Engine](https://github.com/DiligentGraphics/DiligentCore) does the rendering on its
OpenGL or Vulkan backend. Combat mixes 3D arena footwork and limb strings with motion-command
specials, a six-stock Drive gauge and a Heat system. The move data and tuning are original.

## Layout

| Path | What it holds |
|---|---|
| `steel_knuckle/src` | Game source: platform, renderer, mesh generation, font, audio, scene, character, UI, and the combat simulation in `game.cpp` |
| `steel_knuckle/tests` | Combat simulation tests and the `check.ps1` harness |
| `steel_knuckle/*.fbx`, `91-anime_character/` | Character models and texture atlas, staged next to the executable at build time |
| `third_party/ufbx`, `third_party/stb` | Vendored single-file FBX and image loaders |
| `3d fighter.cpp` | The earlier single-file raylib prototype, kept for reference |

## Prerequisites

The two large SDKs are not in this repository. Put them under `third_party/` before configuring:

- `third_party/DiligentCore` — clone [DiligentCore](https://github.com/DiligentGraphics/DiligentCore)
  with its submodules. CMake builds it from source; the Direct3D backends are off because the
  toolchain here is MinGW.
- `third_party/SDL3-3.4.16` — the prebuilt SDL3 MinGW development package. CMake expects
  `third_party/SDL3-3.4.16/x86_64-w64-mingw32/include/SDL3/SDL.h`.

Also needed: CMake 3.19 or newer and a MinGW-w64 GCC toolchain. Point `-DTHIRD_PARTY=...` at a
different directory if you keep the SDKs elsewhere.

## Build

```sh
cmake -S steel_knuckle -B build/game -G "MinGW Makefiles"
cmake --build build/game --target steel_knuckle -j 4
```

The executable lands in `build/game/`, with `SDL3.dll` and an `assets/` folder copied beside it.
Release builds compile at `-O1` — GCC 9 and newer miscompile parts of Diligent at `-O2` and above,
and the engine's own build drops to `-O1` for the same reason.

## Run

```sh
build/game/steel_knuckle.exe
```

Flags: `--demo` for CPU versus CPU from the first frame, `--vulkan` or `--opengl` to pick a
backend, `--quality 0|1|2` for performance, balanced or cinematic, `--nofx` to start with
post-processing off, and `--shot N` to write `shot.png` on frame N and exit.

## Tests

```sh
powershell -File steel_knuckle/tests/check.ps1
```

The harness reuses the compiler and include flags from `build/game`, so configure the game once
first. It builds `build/combat_tests.exe` against `game.cpp` and runs the simulation suite. Add
`-BuildGame` to build the game afterwards.

## Controls

[`steel_knuckle/COMBAT.md`](steel_knuckle/COMBAT.md) has the full control table and the rules for
motion commands, Drive, Heat, juggles and teching. In short: player 1 moves on A/D, jumps with W,
crouches with S, sidesteps with Q/E, punches with U/I, kicks with J/K, throws with O and fires
Rage Art with L. Player 2 uses the arrow keys, comma/period and the numpad. Esc or P opens the
pause menu, which includes the move list and a training readout.
