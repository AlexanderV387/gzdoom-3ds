#!/bin/sh
# A static SDL2 for the host tools (zipdir, lemon, re2c), video and audio off, into
# build-deps/host-sdl2. Only needed where no libsdl2-dev is installed (the
# devkitpro/devkitarm Docker image). Run from the repository root.
set -eu
ROOT=$(pwd)
PREFIX=$ROOT/build-deps/host-sdl2
[ -f "$PREFIX/lib/libSDL2.a" ] && { echo "host SDL2 already in $PREFIX"; exit 0; }
[ -d "$ROOT/build-deps/SDL2-host-src/.git" ] || \
	git clone --depth 1 -b release-2.32.10 https://github.com/libsdl-org/SDL.git "$ROOT/build-deps/SDL2-host-src"
cmake -S "$ROOT/build-deps/SDL2-host-src" -B "$ROOT/build-deps/SDL2-host-build" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_VIDEO=OFF -DSDL_AUDIO=OFF \
	-DSDL_TEST=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$ROOT/build-deps/SDL2-host-build"
cmake --install "$ROOT/build-deps/SDL2-host-build"
echo "host SDL2: $PREFIX"
