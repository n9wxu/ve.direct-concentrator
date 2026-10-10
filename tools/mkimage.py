#!/usr/bin/env python3
"""Finish firmware images for the VE.Direct concentrator.

  mkimage.py image   in.raw out.bin --slot-size N
      Write the image length into vector slot 8, pad to a multiple of four
      bytes and append the CRC-32 that stage0 and the bootloader check.

  mkimage.py factory out.bin stage0.bin boot.bin app.bin
      Lay the three out at their flash addresses for programming over SWD.

The layout constants mirror common/image.h.
"""
import argparse
import struct
import sys
import zlib

LENGTH_OFFSET = 4 * 8
BOOT_BASE = 0x0800
APP_BASE = 0x4800
STAGE0_SIZE = 0x0800
BOOT_SIZE = 0x4000


def make_image(source, target, slot_size):
    data = bytearray(open(source, "rb").read())
    data += b"\xff" * (-len(data) % 4)
    if len(data) + 4 > slot_size:
        sys.exit(f"{source}: {len(data) + 4} bytes do not fit the {slot_size}-byte slot")
    struct.pack_into("<I", data, LENGTH_OFFSET, len(data))
    data += struct.pack("<I", zlib.crc32(data))
    open(target, "wb").write(data)
    print(f"{target}: {len(data)} bytes of {slot_size}")


def make_factory(target, stage0, boot, app):
    stage0, boot, app = (open(name, "rb").read() for name in (stage0, boot, app))
    if len(stage0) > STAGE0_SIZE or len(boot) > BOOT_SIZE:
        sys.exit("stage0 or bootloader larger than its slot")
    image = stage0.ljust(BOOT_BASE, b"\xff") + boot
    image = image.ljust(APP_BASE, b"\xff") + app
    open(target, "wb").write(image)
    print(f"{target}: {len(image)} bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    image = commands.add_parser("image")
    image.add_argument("source")
    image.add_argument("target")
    image.add_argument("--slot-size", type=lambda text: int(text, 0), required=True)
    factory = commands.add_parser("factory")
    factory.add_argument("target")
    factory.add_argument("stage0")
    factory.add_argument("boot")
    factory.add_argument("app")
    arguments = parser.parse_args()
    if arguments.command == "image":
        make_image(arguments.source, arguments.target, arguments.slot_size)
    else:
        make_factory(arguments.target, arguments.stage0, arguments.boot, arguments.app)


if __name__ == "__main__":
    main()
