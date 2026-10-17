#!/usr/bin/env python3
"""Gera boot.img Android header v0/v1 (sem depender do mkbootimg da AOSP)."""
import argparse, hashlib, struct

p = argparse.ArgumentParser()
for a in ("kernel", "ramdisk", "output", "cmdline"):
    p.add_argument("--" + a, required=a != "cmdline", default="")
p.add_argument("--pagesize", type=lambda x: int(x, 0), default=4096)
p.add_argument("--header_version", type=int, choices=(0, 1), default=0)
for a, d in (("base", 0x10000000), ("kernel_offset", 0x8000), ("ramdisk_offset", 0x1000000),
             ("second_offset", 0xF00000), ("tags_offset", 0x100)):
    p.add_argument("--" + a, type=lambda x: int(x, 0), default=d)
a = p.parse_args()

kernel = open(a.kernel, "rb").read()
ramdisk = open(a.ramdisk, "rb").read()
cmd = a.cmdline.encode()
if len(cmd) > 512 + 1024 - 1:
    raise SystemExit("cmdline grande demais")

sha = hashlib.sha1()
for blob in (kernel, ramdisk, b""):
    sha.update(blob)
    sha.update(struct.pack("<I", len(blob)))

header = struct.pack(
    "<8s10I16s512s32s1024s", b"ANDROID!",
    len(kernel), a.base + a.kernel_offset,
    len(ramdisk), a.base + a.ramdisk_offset,
    0, a.base + a.second_offset,
    a.base + a.tags_offset, a.pagesize,
    a.header_version, 0,  # os_version
    b"lumna", cmd[:512], sha.digest(), cmd[512:])
if a.header_version == 1:
    # recovery_dtbo_size, recovery_dtbo_offset, header_size (sem recovery dtbo no boot)
    header += struct.pack("<IQI", 0, 0, len(header) + 16)

def pad(b):
    return b + b"\0" * (-len(b) % a.pagesize)

with open(a.output, "wb") as f:
    f.write(pad(header) + pad(kernel) + pad(ramdisk))
