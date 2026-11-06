# UZDoom for the Nintendo 3DS (work in progress)

Branch `uzdoom-3ds` of [AlexanderV387/gzdoom-3ds](https://github.com/AlexanderV387/gzdoom-3ds), part of [n3ds-ports](https://github.com/AlexanderV387/n3ds-ports). UZDoom (the GZDoom successor, `trunk` of November 2026, preparing 4.15) with its whole hardware renderer drawn by the PICA200 through citro3d: a new video backend, not a software renderer.

**Status (2026-11-06): the engine starts on a New 3DS and reaches "Init complete"** (IWAD, ZScript, menus, playsim, backend). The first frame of the title screen had not been seen yet: test 8 (`uzdoom-prueba08.3dsx`), which fixes the 2D list never being drawn or cleared, was not reported.

## Building

```sh
platform/3ds/build.sh          # makes build-3ds/uzdoom.3dsx
```

Needs devkitPro (`dkp-pacman -S 3ds-dev 3ds-sdl`) plus, for the host tools, gcc/g++, cmake, python3, zip and SDL2 development files. The script builds the host tools once (`build-host/`), then the 3DS build (`build-3ds/`), then packages the 3DSX with `uzdoom.pk3` and `game_support.pk3` in its RomFS (the CJK dynamic fonts and the desktop `ui/` files are removed from the copy).

With the `devkitpro/devkitarm` Docker image, which lacks both SDL2s: build a static host SDL2 (2.32.x, video and audio off is enough) and use a 3DS SDL2 install prefix, then pass `SDL2_HOST=<host prefix> SDL2_3DS=<3ds prefix>`. In the cloud sessions the 3DS SDL2 came from the GZDoom 4.7.1 branch's build (`build-3ds/sdl2-3ds-prefix`), the network needs `--network host` and the proxy's CA.

## Running

Copy `uzdoom.3dsx` anywhere on the SD card and the game to `sdmc:/3ds/gzdoom/` (`doom2.wad`, or the one named in `iwad.txt`); mods go in `sdmc:/3ds/gzdoom/mods/` (all loaded, by name). Logs: `sdmc:/3ds/gzdoom/log.txt` (each line starts with the heap in use, in KB; allocations of 1 MB or more are listed with their callers) and `fatal.log`. Resolve the callers with `arm-none-eabi-addr2line -f -C -e build-3ds/uzdoom.elf <addresses>` on the same build.

## What the port changes

| Where | What |
|---|---|
| `CMakeLists.txt` (top) | 3DS: GNU extensions (newlib), `int32_t` is an `int` (devkitARM makes it a `long`: thousands of overload errors otherwise), no libdl, no abseil, no ZWidget |
| `src/CMakeLists.txt` | 3DS platform sources, no desktop GL or `widgets/`, the backend's shader (picasso + bin2s), libvpx optional |
| `src/common/platform/3ds/` | entry point (New 3DS speed, IWAD/mods from the SD card, log, system error dialog), system services, SD paths, startup screen, no-op desktop windows, memory diagnostics (`--wrap=malloc...`) |
| `src/common/rendering/c3d/` | the video backend: buffers (vertices in linear memory, 32-bit indices narrowed to 16 bits per draw), textures (power-of-two, tiled RGBA8), render state on texture combiners, blending, alpha test, depth, stencil, one vertex shader with the screen's 90 degree tilt, deferred frees (the GPU reads a frame late) |
| `libraries/ZMusic` | optional FluidSynth (needs glib), no `dlopen`, 32-bit `long` byte-swap fixes |
| `libraries/lzma` | no ARMv8 CRC or CPU probing |
| Engine | `sdmc:/` and `romfs:/` are absolute paths; smaller vertex (8 MB), light and bone buffers; hex fonts keep Latin/Greek/Cyrillic/symbols (all of Unifont took ~45 MB); no CJK TrueType fonts (atlases of tens of MB); enums read as ints by ZScript or the serializer get `: int` (ARM EABI makes them 1 byte); the 64-bit-only assert skipped; VP8 movies only with libvpx |

## Known open items

- First image on screen (test 8): orientation, depth and colors were written without seeing them.
- Lines and points (automap) are not drawn yet; no fog, light diminishing, portals (depth range), dynamic lights.
- No sound (`NO_OPENAL`); the GZDoom 4.7.1 branch has OpenAL Soft over NDSP to bring over.
- Input is SDL's generic joystick; the 3DS controls, bottom screen and CIA packaging are still to do.
- Memory: about 49 MB of the 72 MB heap in use after ZScript; ZScript compiles in ~6.6 s.
- UZDoom says 32-bit builds are unsupported and may show visual artifacts (forum.zdoom.org t=75673): to check once the world draws.
