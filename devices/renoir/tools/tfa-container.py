#!/usr/bin/env python3
"""Dump an NXP TFA98xx container (.cnt, "PM3_01"): its devices, profiles and what each writes.

usage: tfa-container.py tfa98xx.cnt

Layout (NXP tfa98xx driver, tfa_container.h / tfa_dsp_fw.h): a 0x38-byte header (id "PM3_01", size,
CRC, customer, application, type, 2 x 2-byte counts: devices, profiles), then the device lists and the
profile lists. A list is a 4-byte head (name offset, bus/addr) and then descriptors of 4 bytes each:
type (1 byte) and offset (3 bytes, little endian, from the start of the file). Types used here:
0x00 device, 0x01 profile, 0x02 register, 0x03 string (a name), 0x04 file, 0x05 patch, 0x06 marker,
0x07 mode, 0x10 bitfield (at the offset: a 16-bit value, then the 16-bit field id
register << 8 | position << 4 | (length - 1)), 0x11 default bitfield.
"""
import struct
import sys

TYPES = {0: "device", 1: "profile", 2: "register", 3: "string", 4: "file", 5: "patch", 6: "marker",
         7: "mode", 8: "input-select", 0x10: "bitfield", 0x11: "default-bitfield", 0x12: "cf-mem",
         0x13: "sr-ondemand", 0x14: "sr", 0x17: "group", 0x18: "delay", 0x19: "tfa-register"}


def cstr(data, at):
    end = data.index(b"\0", at)
    return data[at:end].decode("latin-1")


def field(fid):
    return fid >> 8, (fid >> 4) & 0xf, (fid & 0xf) + 1


def desc(data, at):
    """A descriptor: 24-bit offset, then the type in the top byte."""
    word, = struct.unpack_from("<I", data, at)
    return word >> 24, word & 0xffffff


def show_item(data, kind, offset, indent):
    name = TYPES.get(kind, f"type {kind:#x}")
    if kind == 3:
        print(f"{indent}{name:16s} {cstr(data, offset)!r}")
    elif kind in (0x10, 0x11):
        value, fid = struct.unpack_from("<HH", data, offset)
        reg, pos, length = field(fid)
        print(f"{indent}{name:16s} reg {reg:#04x} bits {pos + length - 1:2d}:{pos:<2d} = {value:#06x}   (field {fid:#06x})")
    elif kind in (4, 5):
        print(f"{indent}{name:16s} at {offset:#x}: {data[offset:offset + 16].hex()}")
    else:
        print(f"{indent}{name:16s} -> {offset:#x}")


def main():
    data = open(sys.argv[1], "rb").read()
    ndev, nprof, nlive = struct.unpack_from("<HHH", data, 0x28)
    print(f"{data[:6].decode()}, {len(data)} bytes, customer {cstr(data, 0x10)!r} app {cstr(data, 0x18)!r} type "
          f"{cstr(data, 0x20)!r}: {ndev} devices, {nprof} profiles")
    for d in range(ndev):
        kind, off = desc(data, 0x2e + 4 * d)
        length, bus, dev, func = data[off], data[off + 1], data[off + 2], data[off + 3]
        nkind, noff = desc(data, off + 8)
        print(f"device {d}: I2C bus {bus} address {dev:#x}, {cstr(data, noff)!r}, {length} items")
        profiles = []
        for i in range(length):
            k, o = desc(data, off + 12 + 4 * i)
            if k == 1:
                profiles.append(o)
            else:
                show_item(data, k, o, "   ")
        for poff in profiles:
            plen = data[poff]
            pk, pn = desc(data, poff + 4)
            print(f"  profile {cstr(data, pn)!r} ({plen} items)")
            for i in range(plen):
                k, o = desc(data, poff + 8 + 4 * i)
                if k == 3 and i == 0:
                    continue
                show_item(data, k, o, "     ")


if __name__ == "__main__":
    main()
