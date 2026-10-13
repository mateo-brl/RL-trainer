#!/usr/bin/env python3
"""Sanity-check a built BakkesMod plugin DLL (stdlib only; runs on Windows and Linux).

Checks:
  * PE32+ / x64 (machine 0x8664) and a DLL
  * exports the three symbols emitted by BAKKESMOD_PLUGIN(): exports, getPlugin, deleteMe
  * imports pluginsdk.dll (i.e. it was linked against the SDK import library)
  * does NOT import the dynamic CRT (msvcp140/vcruntime140/ucrtbase/api-ms-win-crt-*) => built /MT
  * does NOT import any debug CRT (msvcp140d/vcruntime140d/ucrtbased) => no _ITERATOR_DEBUG_LEVEL 2
usage: verify_dll.py path/to/rltrainer.dll   (exit code 0 = OK)
"""
import struct
import sys

REQUIRED_EXPORTS = {"exports", "getPlugin", "deleteMe"}
REQUIRED_IMPORTS = {"pluginsdk.dll"}
FORBIDDEN_PREFIXES = ("msvcp140", "vcruntime140", "ucrtbase", "api-ms-win-crt-", "concrt140")


def parse(path):
    data = open(path, "rb").read()
    if data[:2] != b"MZ":
        raise ValueError("not a PE file (no MZ)")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file (no PE signature)")
    machine, nsec, _, _, _, opt_size, characteristics = struct.unpack_from("<HHIIIHH", data, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic != 0x20B:
        raise ValueError("not PE32+ (64-bit)")
    ndirs = struct.unpack_from("<I", data, opt + 108)[0]
    dirs = [struct.unpack_from("<II", data, opt + 112 + 8 * i) for i in range(min(ndirs, 16))]
    sections = []
    sec = opt + opt_size
    for i in range(nsec):
        _, vsize, vaddr, rsize, rptr = struct.unpack_from("<8sIIII", data, sec + 40 * i)
        sections.append((vaddr, max(vsize, rsize), rptr))

    def off(rva):
        for vaddr, size, rptr in sections:
            if vaddr <= rva < vaddr + size:
                return rva - vaddr + rptr
        raise ValueError(f"RVA {rva:#x} not in any section")

    def cstr(rva):
        o = off(rva)
        return data[o:data.index(b"\0", o)].decode("ascii", "replace")

    exports = set()
    if len(dirs) > 0 and dirs[0][0]:
        o = off(dirs[0][0])
        nnames = struct.unpack_from("<I", data, o + 24)[0]      # NumberOfNames
        names_rva = struct.unpack_from("<I", data, o + 32)[0]   # AddressOfNames
        for i in range(nnames):
            exports.add(cstr(struct.unpack_from("<I", data, off(names_rva) + 4 * i)[0]))
    imports = []
    if len(dirs) > 1 and dirs[1][0]:
        o = off(dirs[1][0])
        while True:
            ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", data, o)
            if not (ilt or name_rva or iat):
                break
            imports.append(cstr(name_rva))
            o += 20
    return machine, characteristics, exports, imports


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    machine, characteristics, exports, imports = parse(argv[1])
    lower = [i.lower() for i in imports]
    errors = []
    if machine != 0x8664:
        errors.append(f"machine {machine:#x} is not x64 (0x8664)")
    if not characteristics & 0x2000:
        errors.append("IMAGE_FILE_DLL flag not set")
    missing = REQUIRED_EXPORTS - exports
    if missing:
        errors.append(f"missing exports {sorted(missing)} (BAKKESMOD_PLUGIN macro not compiled in?)")
    for req in REQUIRED_IMPORTS:
        if req not in lower:
            errors.append(f"does not import {req}")
    bad = [i for i in lower if i.startswith(FORBIDDEN_PREFIXES)]
    if bad:
        errors.append(f"imports dynamic/debug CRT {bad}: build with /MT (CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded)")
    print(f"{argv[1]}: machine={machine:#x} exports={sorted(exports)}")
    print(f"  imports={imports}")
    for e in errors:
        print(f"ERROR: {e}")
    print("OK" if not errors else "FAILED")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
