#!/usr/bin/env python3
"""Generate MSIX logo assets from the canonical CaptureView PNG."""

from __future__ import annotations

import sys
from pathlib import Path

from make_icon import read_png, resize_square, write_png


def write_wide_logo(destination: Path, source_pixels: bytes, source_size: int) -> None:
    width, height = 310, 150
    icon_size = 128
    icon = resize_square(source_pixels, source_size, source_size, icon_size)
    canvas = bytearray(width * height * 4)
    left = (width - icon_size) // 2
    top = (height - icon_size) // 2
    for y in range(icon_size):
        source = y * icon_size * 4
        target = ((top + y) * width + left) * 4
        canvas[target : target + icon_size * 4] = icon[source : source + icon_size * 4]

    # write_png accepts square images, so encode the rectangular canvas here.
    import binascii
    import struct
    import zlib

    signature = b"\x89PNG\r\n\x1a\n"

    def chunk(kind: bytes, payload: bytes) -> bytes:
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", binascii.crc32(body))

    rows = b"".join(
        b"\0" + canvas[y * width * 4 : (y + 1) * width * 4]
        for y in range(height)
    )
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    destination.write_bytes(
        signature
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(rows, 9))
        + chunk(b"IEND", b"")
    )


def main(source: Path, destination: Path) -> None:
    width, height, pixels = read_png(source)
    if width != height:
        raise ValueError("the canonical CaptureView logo must be square")

    destination.mkdir(parents=True, exist_ok=True)
    sizes = {
        "StoreLogo.png": 50,
        "Square44x44Logo.png": 44,
        "Square150x150Logo.png": 150,
        "Square310x310Logo.png": 310,
    }
    for name, size in sizes.items():
        resized = resize_square(pixels, width, height, size)
        (destination / name).write_bytes(write_png(size, resized))
    write_wide_logo(destination / "Wide310x150Logo.png", pixels, width)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: make_msix_assets.py INPUT.png OUTPUT_DIRECTORY")
    main(Path(sys.argv[1]), Path(sys.argv[2]))
