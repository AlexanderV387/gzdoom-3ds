#!/usr/bin/env python3
"""Checks a UZDoom 3DSX and its ELF before they go to the console.

    check-3dsx.py build-3ds/uzdoom.3dsx build-3ds/uzdoom.elf "<test id>"

Fails (exit 1) unless:
  - the 3DSX starts with "3DSX" (not an ELF renamed) and has an SMDH and a RomFS;
  - the RomFS holds data/uzdoom.pk3 and data/game_support.pk3;
  - the ELF is an ARM ELF;
  - both carry the test id that build-test.sh compiled in (so they come from the same build).
"""
import struct
import sys


def fail(msg):
    print("CHECK FAILED: " + msg)
    sys.exit(1)


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(2)
    path3dsx, pathelf, test_id = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(path3dsx, "rb").read()
    elf = open(pathelf, "rb").read()

    if data[:4] != b"3DSX":
        fail(f"{path3dsx} does not start with 3DSX (starts with {data[:4]!r}): not a 3DSX")
    header_size = struct.unpack_from("<H", data, 4)[0]
    if header_size < 0x2C:
        fail("the 3DSX has no extended header: no SMDH and no RomFS")
    smdh_off, smdh_size, fs_off = struct.unpack_from("<III", data, 0x20)
    if smdh_size == 0 or data[smdh_off:smdh_off + 4] != b"SMDH":
        fail("no SMDH (icon and title) in the 3DSX")
    # A 3DSX holds the RomFS's level-3 image: it starts with its header size, 0x28 (libctru's
    # romfs_dev.c reads it so). The IVFC levels exist only in CIA/NCCH RomFS.
    if fs_off == 0 or fs_off >= len(data) or struct.unpack_from("<I", data, fs_off)[0] != 0x28:
        fail("no RomFS in the 3DSX (3dsxtool needs --romfs)")
    romfs = data[fs_off:]
    for name in ("data", "uzdoom.pk3", "game_support.pk3"):
        if name.encode("utf-16-le") not in romfs:
            fail(f"the RomFS has no '{name}' (the engine looks for romfs:/data/uzdoom.pk3)")

    if elf[:4] != b"\x7fELF" or elf[18] != 40:   # e_machine 40 = ARM
        fail(f"{pathelf} is not an ARM ELF")

    tid = test_id.encode()
    if tid not in data:
        fail("the 3DSX does not carry the test id: it was not built from this run")
    if tid not in elf:
        fail("the ELF does not carry the test id: it does not belong to this 3DSX")

    print(f"OK: 3DSX {len(data) // 1024} KB with SMDH and RomFS (data/uzdoom.pk3, data/game_support.pk3); "
          f"ELF matches; id: {test_id}")


main()
