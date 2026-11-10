#!/bin/sh
# build-test.sh inside the official devkitpro/devkitarm Docker image: for machines
# where devkitPro cannot be installed (its package server may answer 403).
#
#   platform/3ds/docker-build-test.sh 25          (from the repository root)
#
# The first run also builds SDL2 for the host and for the 3DS into build-deps/.
# The test lands in $PRUEBAS_DIR/NN (default ~/work/3ds-ports/pruebas/NN), as with build-test.sh.
set -eu

if [ $# -ne 1 ]; then
	echo "usage: platform/3ds/docker-build-test.sh <test number>" >&2
	exit 2
fi
IMAGE=${IMAGE:-devkitpro/devkitarm:latest}
PRUEBAS_DIR=${PRUEBAS_DIR:-$HOME/work/3ds-ports/pruebas}
mkdir -p "$PRUEBAS_DIR"

exec docker run --rm -t --user "$(id -u):$(id -g)" -e HOME=/tmp -e FORCE -e JOBS \
	-e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0='*' \
	-e PRUEBAS_DIR=/out -e PRUEBAS_SHOWN="$PRUEBAS_DIR" \
	-v "$PWD":/src -v "$PRUEBAS_DIR":/out -w /src ${DOCKER_ARGS:-} "$IMAGE" sh -c "
		set -e
		platform/3ds/build-host-sdl2.sh
		platform/3ds/build-sdl2-3ds.sh
		SDL2_HOST=/src/build-deps/host-sdl2 platform/3ds/build-test.sh '$1'"
