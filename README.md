# IJ2 Frame Lab

Live frame data, hitboxes and hurtboxes for **Injustice 2** (PC, Steam) Practice mode.

> **Practice mode only.** The overlay and every input feature turn themselves off in any other game mode. Never use mods online.

Download the ready-to-use build from [Nexus Mods](https://www.nexusmods.com/games/injustice2/mods/13). This repository is the source code.

## Features

- A frame data panel styled after the game's own move list: start-up, active, recovery, hit/block advantage, cancel and total, all measured live.
- The game's official move-list data for every normal, string, special and super of every loaded character, read from memory and shown in pad notation.
- Knockdown advantage, meaty and lingering projectile handling, and gaps in strings.
- Hitboxes and hurtboxes, including projectiles, with separate opacity controls.
- Freeze and frame-step, HDR-safe screenshots, and an optional extended input buffer.
- A controller-friendly in-game menu (F2).

## Building

Requirements: CMake 3.16+ and a 64-bit C++17 compiler.

- **MinGW-w64** (the toolchain the releases are built with), cross-compiling from Linux:
  ```
  cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
  ```
- **Visual Studio 2022** (x64) should also work but hasn't been tested yet:
  ```
  cmake -B build -A x64
  cmake --build build --config Release
  ```

The build produces two files:

| File | What it is |
|---|---|
| `IJ2FrameLab.asi` | A small loader picked up by [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader). |
| `IJ2FrameLab_core.dll` | The mod itself. The loader can hot-reload it (Ctrl+F5) without restarting the game. |

Copy both, plus `dist/IJ2FrameLab.ini`, into `Injustice2\Binaries\Retail`.

## Source layout

| File | Purpose |
|---|---|
| `src/loader.cpp` | The `.asi` loader and hot reload |
| `src/dllmain.cpp` | DX11 Present hook, ImGui setup, window/input hooks |
| `src/ij2.cpp` | Game integration: player objects, collision objects, hurt/hit shapes, game mode, freeze/step |
| `src/game.cpp` | Frame meter, move segmentation, advantage, knockdowns, gaps, move identification |
| `src/movelist.cpp` | Reads the game's move-list frame data tables and turns names into notation |
| `src/ui.cpp` | Frame data panel, F2 menu, advanced window |
| `src/boxes.cpp` | Hitbox and hurtbox drawing |
| `src/buffer.cpp` | XInput hook: extended input buffer, menu input capture |
| `src/dinput_block.cpp` | Blocks keyboard input to the game while the menu is open |
| `src/screenshot.cpp` | Back-buffer PNG screenshots |
| `src/bot.cpp` | Scripted test input and diagnostics (command file), practice mode only |

## Third-party code and credits

| Credit | Project | How IJ2 Frame Lab uses it | Licence |
|---|---|---|---|
| [ermaccer](https://github.com/ermaccer) | [I2Hook](https://github.com/ermaccer/I2Hook) | The function signatures for the game's core systems (game tick, GameInfo, player and object lookup, positions, bones, camera, game speed) and the freeze-world hook behind freeze / frame-step, in `src/ij2.cpp`. | - |
| [ThirteenAG](https://github.com/ThirteenAG) | [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) | Loads the mod into the game. Required, but not included. | MIT |
| [Omar Cornut](https://github.com/ocornut) | [Dear ImGui](https://github.com/ocornut/imgui) | The overlay UI: frame data panel, menus, drawing. Included in `external/imgui`. | MIT |
| [Tsuda Kageyu](https://github.com/TsudaKageyu) | [MinHook](https://github.com/TsudaKageyu/minhook) | Hooks the game's rendering and input. Included in `external/minhook`. | BSD 2-clause |

The licences of the included libraries are in their folders.

## Licence

[MIT](LICENSE). Copyright (c) 2026 Pluralmite.
