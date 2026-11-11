# UZDoom for the Nintendo 3DS (work in progress)

Branch `uzdoom-3ds` of [AlexanderV387/gzdoom-3ds](https://github.com/AlexanderV387/gzdoom-3ds), part of [n3ds-ports](https://github.com/AlexanderV387/n3ds-ports). UZDoom (the GZDoom successor, `trunk` of November 2026, preparing 4.15) with its whole hardware renderer drawn by the PICA200 through citro3d: a new video backend, not a software renderer.

**Status (2026-11-10):** the engine starts and reaches the first frame, but the first frame that draws something hangs the GPU (it never finishes). Test 25 (`build-test.sh 25`) tells the draw from the rest of the frame and the texture from the vertices. Where things stand, the open question and the record of every test: [`STATUS.md`](STATUS.md).

## Building (Linux or WSL Ubuntu)

Once:

```sh
sudo apt install -y build-essential cmake ninja-build python3 zip git wget libsdl2-dev
wget https://apt.devkitpro.org/install-devkitpro-pacman && chmod +x install-devkitpro-pacman
sudo ./install-devkitpro-pacman && sudo dkp-pacman -S 3ds-dev
source /etc/profile.d/devkit-env.sh      # DEVKITPRO, DEVKITARM (new shells get them by themselves)
platform/3ds/build-sdl2-3ds.sh           # SDL2 for the 3DS (devkitPro has none) into build-deps/
```

Each test for the console:

```sh
platform/3ds/build-test.sh 25            # builds, checks, stores in ~/work/3ds-ports/pruebas/25/
```

`build-test.sh` writes the test number and the commit into the build (the log's `Test:` line), runs `build.sh`, checks the result with `tools/check-3dsx.py` (a real 3DSX with SMDH and RomFS, both PK3s under `data/`, the same test id in the 3DSX and the ELF) and copies `uzdoom-pruebaNN.3dsx`, `uzdoom-pruebaNN.elf`, `info.txt` and the uncommitted diff to `~/work/3ds-ports/pruebas/NN/`. It never overwrites a test (`FORCE=1` to do it).

`build.sh` alone builds the host tools once (`build-host/`), then the 3DS build (`build-3ds/`), then packages the 3DSX with `uzdoom.pk3` and `game_support.pk3` in `data/` of its RomFS (the CJK dynamic fonts and the desktop `ui/` files are removed from the copy).

With Docker instead of a local devkitPro (the devkitPro installer may answer 403): `docker pull devkitpro/devkitarm:latest`, then `platform/3ds/docker-build-test.sh NN`. It runs `build-host-sdl2.sh` (a static host SDL2, which the image lacks), `build-sdl2-3ds.sh` and `build-test.sh` inside the image, as your user, and stores the test in the same `~/work/3ds-ports/pruebas/NN/`.

Do not build from Windows (MSYS2) on a copy in `C:`: the path translation and the hand-made packaging produced two invalid tests (17 and 19).

## Diagnostics (citro3d backend)

`src/common/rendering/c3d/c3d_diag.cpp`. Lines starting with `[C3D]` in `log.txt`, written to the SD card at once:

- `switches:` which control files were found;
- `frame N:` for the first 12 frames: whether the frame could start, the retries, the draws sent;
- `draw …`: the first 8 draws in full (attributes, permutation, physical addresses, texture, blend, depth);
- `GPU HANG`: once, when a frame cannot start after 2 s.

Control files, empty, in `sdmc:/3ds/gzdoom/` (read at start): `c3d-nodraw.txt` (no draw command reaches the GPU), `c3d-notex.txt` (draws without textures), `c3d-triangle.txt` (one fixed red triangle per frame instead of the engine's 2D).

The log is synced to the SD card every quarter of a second and after every `[C3D]` line (`fsync`): a power-off loses little.

## Running

Copy `uzdoom.3dsx` anywhere on the SD card and the game to `sdmc:/3ds/gzdoom/` (`doom2.wad`, or the one named in `iwad.txt`); mods go in `sdmc:/3ds/gzdoom/mods/` (all loaded, by name). Logs: `sdmc:/3ds/gzdoom/log.txt` (each line starts with the heap in use, in KB; allocations of 1 MB or more are listed with their callers) and `fatal.log`. The first lines say which test and commit the log comes from (`Test:`). Read it with the agent's log analyzer (`analiza-log.py log.txt uzdoom-pruebaNN.elf`), which resolves the callers with the ELF of the same test.

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

## Packaging and validation checklist

Before shipping a `.3dsx` artifact:

1. **RomFS structure:** `i_main_3ds.cpp` looks for `romfs:/data/BASEWAD`. Files must be under `<romfsdir>/data/uzdoom.pk3` and `<romfsdir>/data/game_support.pk3`, not at the RomFS root. Verify with `strings <3dsx> | grep data/` or by inspecting the archive.
2. **Filters:** apply the same PK3 filters as `platform/3ds/build.sh` (remove desktop `ui/` and CJK fonts). Unfiltered PK3s inflate the 3DSX unnecessarily (~11 MB extra).
3. **Build command:** `3dsxtool <elf> <output>.3dsx --romfs=<romfsdir>` (the `--romfs` flag is mandatory; note the argument order).
4. **Versioned ELF:** keep a dated copy of the ELF alongside the 3DSX (e.g., `uzdoom-prueba13.elf`). Use it with `arm-none-eabi-addr2line -f -C -e <elf> <address>` to resolve crash traces from `log.txt`.
5. **String markers:** run `strings <3dsx> | grep TEST` to confirm diagnostic markers (TEST13) are present in the artifact.

## Known open items

- First image on screen: the first frame with a draw hangs the GPU (see STATUS.md); orientation, depth and colors were written without seeing them.
- Lines and points (automap) are not drawn yet; no fog, light diminishing, portals (depth range), dynamic lights.
- No sound (`NO_OPENAL`); the GZDoom 4.7.1 branch has OpenAL Soft over NDSP to bring over.
- Input is SDL's generic joystick; the 3DS controls, bottom screen and CIA packaging are still to do.
- Memory: about 49 MB of the 72 MB heap in use after ZScript; ZScript compiles in ~6.6 s.
- UZDoom says 32-bit builds are unsupported and may show visual artifacts (forum.zdoom.org t=75673): to check once the world draws.
