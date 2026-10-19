"""Make libtune.so load libgcp.so.

The DT_SONAME entry is turned into a DT_NEEDED entry and its string
("libtune.so") is overwritten with "libgcp.so". No section sizes change.
usage: patch_libtune.py <in libtune.so> <out libtune.so>
"""
import struct
import sys

from elftools.elf.elffile import ELFFile

DT_NEEDED, DT_SONAME = 1, 14
NEW_NAME = b"libgcp.so"

src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src, "rb").read())
elf = ELFFile(open(src, "rb"))

dyn = elf.get_section_by_name(".dynamic")
strtab_off = elf.get_section_by_name(".dynstr")["sh_offset"]

for i in range(dyn["sh_size"] // 16):
    off = dyn["sh_offset"] + i * 16
    tag, val = struct.unpack_from("<qQ", data, off)
    if tag == DT_NEEDED:
        name = bytes(data[strtab_off + val:]).split(b"\0")[0]
        if name == NEW_NAME:
            sys.exit("already patched")
    if tag == DT_SONAME:
        old = bytes(data[strtab_off + val:]).split(b"\0")[0]
        assert old == b"libtune.so", old
        assert len(NEW_NAME) <= len(old)
        data[strtab_off + val:strtab_off + val + len(old)] = NEW_NAME.ljust(len(old), b"\0")
        struct.pack_into("<q", data, off, DT_NEEDED)
        break
else:
    sys.exit("DT_SONAME not found")

open(dst, "wb").write(data)
print("patched ->", dst)
