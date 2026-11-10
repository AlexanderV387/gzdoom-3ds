#!/bin/sh
# Builds SDL2 for the Nintendo 3DS (devkitPro has no 3DS SDL2 package) into
# build-deps/sdl2-3ds-prefix, at the same revision and with the same patch as the
# GZDoom 4.7.1 branch (n3ds). Run once, from the repository root:
#
#   platform/3ds/build-sdl2-3ds.sh
#
# build-test.sh and build.sh use that prefix by themselves when it exists.
set -eu

DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
SDL2_URL=https://github.com/libsdl-org/SDL.git
SDL2_REV=5d249570393f7a37e037abf22cd6012a4cc56a71
ROOT=$(pwd)
SRC=$ROOT/build-deps/SDL2
PREFIX=$ROOT/build-deps/sdl2-3ds-prefix

if [ -f "$PREFIX/include/SDL2/SDL_main.h" ] && [ -f "$PREFIX/lib/libSDL2.a" ]; then
	echo "SDL2 for the 3DS is already in $PREFIX"
	exit 0
fi

if [ ! -d "$SRC/.git" ]; then
	git clone "$SDL2_URL" "$SRC"
fi
git -C "$SRC" checkout -q "$SDL2_REV"
git -C "$SRC" checkout -q -- .
git -C "$SRC" apply "$ROOT/platform/3ds/patches/sdl2-3ds-clean-exit.patch"

cmake -S "$SRC" -B "$ROOT/build-deps/sdl2-3ds-build" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
	-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_AUDIO=ON -DSDL_TEST=OFF
cmake --build "$ROOT/build-deps/sdl2-3ds-build"
cmake --install "$ROOT/build-deps/sdl2-3ds-build"

test -f "$PREFIX/include/SDL2/SDL_main.h"
test -f "$PREFIX/lib/libSDL2.a"
echo "SDL2 for the 3DS: $PREFIX"
