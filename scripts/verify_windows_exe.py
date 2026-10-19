#!/usr/bin/env python3
"""Audit PE32+ x64 regular AND delay imports; needs only Python's standard library.

This checks static packaging, not malware safety or Windows runtime compatibility.
Windows system DLLs are expected; Qt, compiler and third-party DLLs are rejected.
"""
import argparse
import pathlib
import struct
import sys

SYSTEM_DLLS = set("""
advapi32.dll avrt.dll bcrypt.dll bcryptprimitives.dll cabinet.dll cfgmgr32.dll
comctl32.dll comdlg32.dll crypt32.dll cryptbase.dll cryptsp.dll d2d1.dll d3d11.dll
d3d9.dll dcomp.dll dnsapi.dll dwmapi.dll dwrite.dll dxgi.dll dxguid.dll gdi32.dll
hid.dll imm32.dll iphlpapi.dll kernel32.dll kernelbase.dll mf.dll mfplat.dll
mfreadwrite.dll mpr.dll mqrt.dll msimg32.dll msvcrt.dll ncrypt.dll netapi32.dll
normaliz.dll ntdll.dll ole32.dll oleacc.dll oleaut32.dll opengl32.dll powrprof.dll
propsys.dll psapi.dll rpcrt4.dll secur32.dll setupapi.dll shell32.dll shcore.dll
shlwapi.dll synchronization.dll ucrtbase.dll urlmon.dll user32.dll userenv.dll
usp10.dll uxtheme.dll version.dll winhttp.dll wininet.dll winmm.dll winspool.drv
wintrust.dll wldap32.dll ws2_32.dll wtsapi32.dll
""".split())


def imports(path):
    data = path.read_bytes()

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError("Truncated PE structure")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("Not a PE executable")
    pe, = unpack("<I", 0x3C)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    machine, count = unpack("<HH", pe + 4)
    optional_size, = unpack("<H", pe + 20)
    optional = pe + 24
    magic, = unpack("<H", optional)
    if machine != 0x8664 or magic != 0x20B:
        raise ValueError("Expected a Windows x64 PE32+ binary")
    image_base, = unpack("<Q", optional + 24)
    header_size, = unpack("<I", optional + 60)
    directory_count, = unpack("<I", optional + 108)
    if optional_size < 112 or directory_count * 8 > optional_size - 112:
        raise ValueError("Invalid PE data directories")
    sections = []
    for index in range(count):
        start = optional + optional_size + index * 40
        virtual_size, rva, raw_size, raw = unpack("<IIII", start + 8)
        if raw_size and raw + raw_size > len(data):
            raise ValueError("Truncated PE section")
        sections.append((rva, max(virtual_size, raw_size), raw, raw_size))

    def offset(rva):
        if 0 <= rva < min(header_size, len(data)):
            return rva
        for base, size, raw, raw_size in sections:
            if base <= rva < base + size and rva - base < raw_size:
                result = raw + rva - base
                if result < len(data):
                    return result
        raise ValueError(f"Unmapped RVA: {rva:#x}")

    def name(rva):
        start = offset(rva)
        end = data.find(b"\0", start, min(start + 512, len(data)))
        if end < 0:
            raise ValueError("Unterminated DLL name")
        return data[start:end].decode("ascii").lower()

    names = set()
    for directory, stride in ((1, 20), (13, 32)):
        if directory >= directory_count:
            continue
        rva, size = unpack("<II", optional + 112 + directory * 8)
        if not rva and not size:
            continue
        if not rva or size < stride:
            raise ValueError("Invalid import directory")
        terminated = False
        for position in range(0, size - stride + 1, stride):
            descriptor = unpack("<" + "I" * (stride // 4), offset(rva + position))
            if not any(descriptor):
                terminated = True
                break
            if directory == 1:
                name_rva = descriptor[3]
            else:
                # Delay descriptor's grAttrs bit 0 selects RVA instead of virtual address.
                name_rva = descriptor[1] if descriptor[0] & 1 else descriptor[1] - image_base
            names.add(name(name_rva))
        if not terminated:
            raise ValueError("Unterminated import directory")
    return sorted(names)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=pathlib.Path)
    parser.add_argument("--max-mib", type=float, help="Optional measured artifact size budget")
    args = parser.parse_args()
    try:
        names = imports(args.exe)
        unexpected = [dll for dll in names if dll not in SYSTEM_DLLS
                      and not dll.startswith(("api-ms-win-", "ext-ms-win-"))]
        size = args.exe.stat().st_size / (1024 * 1024)
        print(f"{args.exe.name}: {size:.2f} MiB; imported DLLs: {', '.join(names)}")
        if unexpected:
            raise ValueError("Non-system DLL dependencies: " + ", ".join(unexpected))
        if args.max_mib is not None and size > args.max_mib:
            raise ValueError(f"Size {size:.2f} MiB exceeds budget {args.max_mib:.2f} MiB")
        print("PASS: no non-system DLL imports (including delay imports)")
        return 0
    except (OSError, ValueError, UnicodeError, struct.error) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
