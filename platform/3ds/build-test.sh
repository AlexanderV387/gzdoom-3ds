#!/bin/sh
# Builds numbered test NN for the console, checks it and stores it with its ELF:
#
#   platform/3ds/build-test.sh 25            (from the repository root, in Linux/WSL)
#
# 1. Writes the test id (number, commit, uncommitted changes, date) into the build: the
#    first lines of log.txt say which code it is.
# 2. Runs platform/3ds/build.sh (compile + package with the RomFS).
# 3. Checks the result (platform/3ds/tools/check-3dsx.py): a real 3DSX, the RomFS with
#    both PK3s, the same id in the 3DSX and in the ELF.
# 4. Copies uzdoom-pruebaNN.3dsx, uzdoom-pruebaNN.elf and info.txt (commit, changed files,
#    the uncommitted diff) to $PRUEBAS_DIR/NN (default ~/work/3ds-ports/pruebas/NN).
#
# An existing test folder is never overwritten (FORCE=1 to do it anyway).
set -eu

if [ $# -ne 1 ] || ! echo "$1" | grep -Eq '^[0-9]+$'; then
	echo "usage: platform/3ds/build-test.sh <test number>" >&2
	exit 2
fi
N=$(printf '%02d' "$1")
PRUEBAS_DIR=${PRUEBAS_DIR:-$HOME/work/3ds-ports/pruebas}
OUT=$PRUEBAS_DIR/$N
if [ -e "$OUT" ] && [ "${FORCE:-0}" != 1 ]; then
	echo "$OUT already exists: test $N was already built. Use the next number, or FORCE=1." >&2
	exit 1
fi
[ -f platform/3ds/build.sh ] || { echo "run it from the repository root" >&2; exit 2; }

COMMIT=$(git rev-parse --short=9 HEAD)
CHANGES=""
git diff --quiet HEAD -- . || CHANGES=" +uncommitted changes"
ID="test $N, commit $COMMIT$CHANGES, built $(date '+%Y-%m-%d %H:%M')"

INFO=src/common/platform/3ds/n3ds_testinfo.h
printf '// Written by platform/3ds/build-test.sh; not in Git.\n#define N3DS_TEST_ID "%s"\n' "$ID" > "$INFO"
touch src/common/platform/3ds/i_main_3ds.cpp

echo "== Building $ID"
N3DS_SMDH_DESC="UZDoom test $N ($COMMIT$CHANGES)" platform/3ds/build.sh

python3 platform/3ds/tools/check-3dsx.py build-3ds/uzdoom.3dsx build-3ds/uzdoom.elf "$ID"

mkdir -p "$OUT"
cp build-3ds/uzdoom.3dsx "$OUT/uzdoom-prueba$N.3dsx"
cp build-3ds/uzdoom.elf "$OUT/uzdoom-prueba$N.elf"
{
	echo "$ID"
	echo
	git log -1 --format='%H %s'
	echo
	echo "Uncommitted changes:"
	git status --short
	echo
	sha256sum "$OUT/uzdoom-prueba$N.3dsx" "$OUT/uzdoom-prueba$N.elf"
} > "$OUT/info.txt"
[ -n "$CHANGES" ] && git diff HEAD > "$OUT/uncommitted.diff"

echo
SHOWN=${PRUEBAS_SHOWN:-$PRUEBAS_DIR}/$N	# the host's path when run inside Docker
echo "Test $N ready:"
echo "  $SHOWN/uzdoom-prueba$N.3dsx   (to the SD card)"
echo "  $SHOWN/uzdoom-prueba$N.elf    (keep: to read its log)"
printf 'From Windows: \\\\wsl.localhost\\Ubuntu%s\n' "$(printf '%s' "$SHOWN" | tr '/' '\\\\')"
