#!/usr/bin/env python3
"""Rewrites the Windows version resource (VS_VERSIONINFO) of a .NET apphost .exe built on macOS.

The .NET SDK only updates the apphost's version resource when building on Windows, so a Mac-built
exe would show "Microsoft® .NET 8.0.x" in Explorer → Properties. This replaces that block in place
(the new block must fit in the old one's space), changing nothing else in the file.

usage: set-version-info.py <exe> <product name> <version X.YY> <original filename>
"""
import struct
import sys


def pad4(b: bytes) -> bytes:
    return b + b"\0" * (-len(b) % 4)


def node(key: str, value: bytes, value_length: int, wtype: int, children=()) -> bytes:
    body = pad4(struct.pack("<HHH", 0, value_length, wtype) + (key + "\0").encode("utf-16-le"))
    body = pad4(body + value)
    for child in children:
        body = pad4(body) + child
    return struct.pack("<H", len(body)) + body[2:]


def string(key: str, text: str) -> bytes:
    return node(key, (text + "\0").encode("utf-16-le"), len(text) + 1, 1)


def version_info(product: str, version: str, filename: str) -> bytes:
    major, minor = (int(x) for x in version.split("."))
    ms, ls = (major << 16) | minor, 0
    fixed = struct.pack("<13I", 0xFEEF04BD, 0x00010000, ms, ls, ms, ls, 0x3F, 0, 0x40004, 1, 0, 0, 0)
    strings = [string("CompanyName", "OpenGlance"), string("FileDescription", product), string("FileVersion", version),
               string("InternalName", product.replace(" ", "")), string("OriginalFilename", filename),
               string("ProductName", product), string("ProductVersion", version)]
    table = node("000004b0", b"", 0, 1, strings)
    string_info = node("StringFileInfo", b"", 0, 1, [table])
    var_info = node("VarFileInfo", b"", 0, 1, [node("Translation", struct.pack("<HH", 0x0000, 0x04B0), 4, 0)])
    return node("VS_VERSION_INFO", fixed, len(fixed), 0, [string_info, var_info])


def main() -> None:
    path, product, version, filename = sys.argv[1:5]
    data = bytearray(open(path, "rb").read())
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe:pe + 4] == b"PE\0\0", "not a PE file"
    count = struct.unpack_from("<H", data, pe + 6)[0]
    sections = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
    rsrc = None
    for i in range(count):
        s = sections + 40 * i
        if data[s:s + 8].rstrip(b"\0") == b".rsrc":
            rsrc = struct.unpack_from("<IIII", data, s + 8)   # VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData
    assert rsrc, "no resource section"
    _, rva, _, raw = rsrc

    def entries(offset):
        named, ids = struct.unpack_from("<HH", data, raw + offset + 12)
        for i in range(named + ids):
            yield struct.unpack_from("<II", data, raw + offset + 16 + 8 * i)

    def first_leaf(offset):
        for _, target in entries(offset):
            return first_leaf(target & 0x7FFFFFFF) if target & 0x80000000 else target
        raise SystemExit("empty resource directory")

    version_dir = next((t for name, t in entries(0) if name == 16), None)   # RT_VERSION
    assert version_dir is not None and version_dir & 0x80000000, "no version resource"
    leaf = first_leaf(version_dir & 0x7FFFFFFF)
    data_rva, size = struct.unpack_from("<II", data, raw + leaf)
    at = data_rva - rva + raw
    assert data[at + 6:at + 6 + 30] == "VS_VERSION_INFO".encode("utf-16-le"), "unexpected version block"
    block = version_info(product, version, filename)
    assert len(block) <= size, f"new version block ({len(block)} bytes) does not fit in {size} bytes"
    data[at:at + size] = block + b"\0" * (size - len(block))
    struct.pack_into("<I", data, raw + leaf + 4, len(block))
    open(path, "wb").write(data)
    print(f"Version resource set: {product} {version} ({len(block)} of {size} bytes)")


if __name__ == "__main__":
    main()
