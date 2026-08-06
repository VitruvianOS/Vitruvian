#!/usr/bin/env python3
#
# Author: Vláďa Janeček <vlada@janecek.cloud>
#
# Clear the DF_1_PIE marker from an ELF executable's DT_FLAGS_1 entry.
#
# Replicants (and BeOS dual-mode binaries in general) require dlopen()ing
# application binaries: BShelf revives an archived BView by loading the
# donor application and resolving its instantiation function, exactly as
# load_add_on() does on Haiku. glibc (>= 2.30) refuses to dlopen any
# object whose DT_FLAGS_1 carries DF_1_PIE ("cannot dynamically load
# position-independent executable"), even though a PIE binary is already
# a perfectly relocatable ET_DYN shared object. The check tests only this
# marker, so clearing it makes the binary dlopen-able again while leaving
# execve(), ASLR and debugging untouched — the kernel never reads the flag.
#
# The file is patched in place. A binary without PT_DYNAMIC, DT_FLAGS_1 or
# the DF_1_PIE bit is left as is (exit 0), so the tool is safe to run on
# every linked target.

import struct
import sys

DF_1_PIE = 0x08000000
DT_NULL = 0
DT_FLAGS_1 = 0x6FFFFFFB
PT_DYNAMIC = 2


def fail(msg):
    print(f"clear_df1_pie: {msg}", file=sys.stderr)
    return 1


def main():
    if len(sys.argv) != 2:
        return fail("usage: clear_df1_pie.py <elf-binary>")
    path = sys.argv[1]

    with open(path, "r+b") as f:
        ident = f.read(16)
        if len(ident) != 16 or ident[:4] != b"\x7fELF":
            return fail(f"{path}: not an ELF file")
        ei_class, ei_data = ident[4], ident[5]
        if ei_class != 2:
            return fail(f"{path}: only ELF64 is supported")
        endian = "<" if ei_data == 1 else ">"

        # Elf64_Ehdr tail: type, machine, version, entry, phoff, shoff,
        # flags, ehsize, phentsize, phnum, shentsize, shnum, shstrndx
        ehdr = struct.unpack(endian + "HHIQQQIHHHHHH", f.read(48))
        e_phoff, e_phentsize, e_phnum = ehdr[4], ehdr[8], ehdr[9]

        # Find PT_DYNAMIC among the program headers.
        dyn_off = dyn_size = None
        for i in range(e_phnum):
            f.seek(e_phoff + i * e_phentsize)
            p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz = \
                struct.unpack(endian + "IIQQQQ", f.read(40))
            if p_type == PT_DYNAMIC:
                dyn_off, dyn_size = p_offset, p_filesz
                break
        if dyn_off is None:
            return 0  # static or unusual binary: nothing to do

        # Walk Elf64_Dyn entries until DT_NULL, patch DT_FLAGS_1 in place.
        for off in range(dyn_off, dyn_off + dyn_size, 16):
            f.seek(off)
            d_tag, d_val = struct.unpack(endian + "qQ", f.read(16))
            if d_tag == DT_NULL:
                break
            if d_tag == DT_FLAGS_1 and (d_val & DF_1_PIE):
                f.seek(off + 8)
                f.write(struct.pack(endian + "Q", d_val & ~DF_1_PIE))
                break
    return 0


if __name__ == "__main__":
    sys.exit(main())
