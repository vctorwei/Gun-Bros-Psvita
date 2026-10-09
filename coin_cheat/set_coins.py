#!/usr/bin/env python3
"""Edit Coins/Glu Coins in a local copy of a Classic GBVP/v1 profile."""

import argparse
from pathlib import Path
import struct
import sys


def checksum(data):
    payload = bytearray(data)
    struct.pack_into("<I", payload, 0x14, 0)
    value = 2166136261
    for byte in payload:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value or 1


def validate(data):
    if len(data) != 1224:
        raise ValueError("Expected a 1,224-byte Classic GBVP/v1 profile.")
    magic, version, config_size, progress_size, count, saved = (
        struct.unpack_from("<6I", data, 0)
    )
    if (magic, version, config_size, progress_size) != (0x50564247, 1, 0x78, 0x38):
        raise ValueError("Unsupported profile format. Check the file and port version.")
    if count > 256 or saved != checksum(data):
        raise ValueError("Invalid input profile checksum or item count. Download a fresh copy.")


def amount(text):
    if text.lower() == "keep":
        return None
    try:
        return int(text, 10)
    except ValueError:
        raise argparse.ArgumentTypeError("Use a whole number or 'keep'.") from None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", type=Path, default=Path("vita_profile_v1.dat"))
    parser.add_argument("--output", type=Path, default=Path("vita_profile_v1.modified.dat"))
    parser.add_argument("--coins", type=amount, default=99999, help="ordinary Coins (default: 99999), or keep")
    parser.add_argument("--glu-coins", type=amount, default=99999, help="golden Glu Coins (default: 99999), or keep")
    args = parser.parse_args()

    try:
        if args.input.resolve() == args.output.resolve():
            raise ValueError("Input and output must be different files.")
        original = args.input.read_bytes()
        validate(original)
        modified = bytearray(original)
        for value, offset, fmt, bits, label in (
            (args.coins, 0xA0, "<Q", 64, "Coins"),
            (args.glu_coins, 0xA8, "<I", 32, "Glu Coins"),
        ):
            if value is not None:
                if not 0 <= value < (1 << bits):
                    raise ValueError("{} must be between 0 and {}.".format(label, (1 << bits) - 1))
                struct.pack_into(fmt, modified, offset, value)
        struct.pack_into("<I", modified, 0x14, checksum(modified))
        validate(modified)
        with args.output.open("xb") as handle:
            handle.write(modified)
    except (OSError, ValueError) as error:
        print("Error: {}".format(error), file=sys.stderr)
        return 1

    print("Created:", args.output)
    print("Coins:", struct.unpack_from("<Q", modified, 0xA0)[0])
    print("Glu Coins:", struct.unpack_from("<I", modified, 0xA8)[0])
    print("The original file was preserved. Existing output files are never overwritten.")
    print("With the game fully closed, upload the output as vita_profile_v1.dat.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
