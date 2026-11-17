#!/bin/sh
# Builds OpenAL Soft for the Nintendo 3DS (NDSP backend) into build-deps/openal-3ds-prefix:
# the sound output of the engine (effects and music). Same fork, revision and patches as
# the GZDoom 4.7.1 port for the 3DS (EstebanPdN/legend-of-doom-3ds), where it runs on the
# console. Run once, from the repository root:
#
#   platform/3ds/build-openal-3ds.sh
#
# build.sh uses that prefix by itself when it exists; without it the engine has no sound.
set -eu

DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
OPENAL_URL=https://github.com/efimandreev0/openal-soft-3ds.git
OPENAL_REV=35420d558a001660140033aa70eeee88b0224f3a
ROOT=$(pwd)
SRC=$ROOT/build-deps/openal-soft-3ds
PREFIX=$ROOT/build-deps/openal-3ds-prefix

if [ -f "$PREFIX/include/AL/al.h" ] && [ -f "$PREFIX/lib/libopenal.a" ]; then
	echo "OpenAL Soft for the 3DS is already in $PREFIX"
	exit 0
fi

if [ ! -d "$SRC/.git" ]; then
	git clone "$OPENAL_URL" "$SRC"
fi
git -C "$SRC" checkout -q "$OPENAL_REV"
git -C "$SRC" checkout -q -- .
for p in openal-soft-3ds-core1 openal-soft-3ds-audio-stability openal-soft-cmake-empty-deps; do
	git -C "$SRC" apply "$ROOT/platform/3ds/patches/$p.patch"
done

cmake -S "$SRC" -B "$ROOT/build-deps/openal-3ds-build" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
	-DPLATFORM=3ds -DLIBTYPE=STATIC -DALSOFT_DLOPEN=OFF -DALSOFT_DLOPEN_NOTES=OFF \
	-DALSOFT_ENABLE_MODULES=OFF -DALSOFT_UTILS=OFF -DALSOFT_NO_CONFIG_UTIL=ON \
	-DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_EAX=OFF \
	-DALSOFT_BACKEND_NDSP=ON -DALSOFT_BACKEND_WAVE=OFF -DALSOFT_EMBED_HRTF_DATA=OFF \
	-DALSOFT_CPUEXT_NEON=OFF -DALSOFT_UPDATE_BUILD_VERSION=OFF -DALSOFT_INSTALL=ON \
	-DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF \
	-DALSOFT_INSTALL_AMBDEC_PRESETS=OFF -DALSOFT_INSTALL_EXAMPLES=OFF -DALSOFT_INSTALL_UTILS=OFF
cmake --build "$ROOT/build-deps/openal-3ds-build" --target OpenAL
cmake --install "$ROOT/build-deps/openal-3ds-build"

test -f "$PREFIX/include/AL/al.h"
test -f "$PREFIX/lib/libopenal.a"
echo "OpenAL Soft for the 3DS: $PREFIX"
