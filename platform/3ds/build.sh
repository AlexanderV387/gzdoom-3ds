#!/bin/sh
# Builds UZDoom for the Nintendo 3DS and packages uzdoom.3dsx (engine data inside its RomFS).
#
# Needs: devkitPro with devkitARM, libctru, citro3d and the 3DS SDL2 portlib
#   (dkp-pacman -S 3ds-dev 3ds-sdl), and for the host tools a native gcc/g++, cmake,
#   python3, zip and SDL2 development files (Linux: libsdl2-dev).
# Usage, from the repository root:   platform/3ds/build.sh
# Output: build-3ds/uzdoom.3dsx
#
# With Docker instead of a local devkitPro:
#   docker run --rm -v "$PWD":/src -w /src devkitpro/devkitarm:latest platform/3ds/build.sh
# (the image has no SDL2 for the 3DS nor for the host: set SDL2_3DS and SDL2_HOST to
# install prefixes of them, see the README next to this file).
set -eu

DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
SDL2_3DS=${SDL2_3DS:-$DEVKITPRO/portlibs/3ds}
JOBS=${JOBS:-$(nproc)}
COMMON="-DZMUSIC_FLUIDSYNTH=OFF -DHAVE_VULKAN=OFF -DHAVE_GLES2=OFF -DNO_OPENAL=ON"

# 1. Host tools (zipdir, lemon, re2c), once.
if [ ! -f build-host/ImportExecutables.cmake ]; then
	${SDL2_HOST:+env SDL2DIR=$SDL2_HOST} cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release -DNO_LIBVPX=ON $COMMON
	cmake --build build-host --target zipdir lemon re2c -j"$JOBS"
fi

# 2. The 3DS build.
if [ ! -f build-3ds/CMakeCache.txt ]; then
	SDL2DIR=$SDL2_3DS cmake -S . -B build-3ds -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" \
		-DFORCE_CROSSCOMPILE=ON -DIMPORT_EXECUTABLES="$PWD/build-host/ImportExecutables.cmake" \
		-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF $COMMON
fi
cmake --build build-3ds -j"$JOBS"

# 3. Package: engine data in the RomFS, without the CJK dynamic fonts (too large for the 3DS).
mkdir -p build-3ds/romfs/data
cp build-3ds/uzdoom.pk3 build-3ds/game_support.pk3 build-3ds/romfs/data/
zip -q -d build-3ds/romfs/data/uzdoom.pk3 'fonts/dynamic/IBM_Plex_Sans_JP/*' 'fonts/dynamic/IBM_Plex_Sans_KR/*' \
	'fonts/dynamic/Kosugi_Maru/*' 'fonts/dynamic/ZCOOLQingKeHaungYou/*' 'ui/*' || true
"$DEVKITPRO/tools/bin/smdhtool" --create "UZDoom 3DS" "UZDoom for New 3DS (test build)" "AlexanderV387" \
	platform/3ds/icon-48.png build-3ds/uzdoom.smdh
"$DEVKITPRO/tools/bin/3dsxtool" build-3ds/uzdoom.elf build-3ds/uzdoom.3dsx --smdh=build-3ds/uzdoom.smdh --romfs=build-3ds/romfs
ls -la build-3ds/uzdoom.3dsx
