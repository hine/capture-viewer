#!/usr/bin/env python3
"""Build a multi-size ICO from a non-interlaced 8-bit RGBA PNG.

This intentionally uses only the Python standard library so regenerating the
CaptureView icon does not add an image-tool dependency to the project.
"""

from __future__ import annotations

import binascii
import struct
import sys
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def read_png(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("input is not a PNG")
    position = len(PNG_SIGNATURE)
    compressed = bytearray()
    width = height = 0
    while position < len(data):
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4 : position + 8]
        payload = data[position + 8 : position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise ValueError("expected a non-interlaced 8-bit RGBA PNG")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break

    raw = zlib.decompress(bytes(compressed))
    stride = width * 4
    output = bytearray(height * stride)
    previous = bytearray(stride)
    source = 0
    for y in range(height):
        filter_type = raw[source]
        source += 1
        row = bytearray(raw[source : source + stride])
        source += stride
        for x in range(stride):
            left = row[x - 4] if x >= 4 else 0
            above = previous[x]
            upper_left = previous[x - 4] if x >= 4 else 0
            if filter_type == 1:
                row[x] = (row[x] + left) & 0xFF
            elif filter_type == 2:
                row[x] = (row[x] + above) & 0xFF
            elif filter_type == 3:
                row[x] = (row[x] + ((left + above) >> 1)) & 0xFF
            elif filter_type == 4:
                estimate = left + above - upper_left
                distances = (abs(estimate - left), abs(estimate - above),
                             abs(estimate - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
                row[x] = (row[x] + predictor) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter {filter_type}")
        output[y * stride : (y + 1) * stride] = row
        previous = row
    return width, height, bytes(output)


def resize_square(pixels: bytes, width: int, height: int, size: int) -> bytes:
    output = bytearray(size * size * 4)
    for y in range(size):
        y0 = y * height // size
        y1 = max(y0 + 1, (y + 1) * height // size)
        for x in range(size):
            x0 = x * width // size
            x1 = max(x0 + 1, (x + 1) * width // size)
            alpha_sum = red_sum = green_sum = blue_sum = 0
            samples = (x1 - x0) * (y1 - y0)
            for source_y in range(y0, y1):
                for source_x in range(x0, x1):
                    source = (source_y * width + source_x) * 4
                    alpha = pixels[source + 3]
                    red_sum += pixels[source] * alpha
                    green_sum += pixels[source + 1] * alpha
                    blue_sum += pixels[source + 2] * alpha
                    alpha_sum += alpha
            target = (y * size + x) * 4
            output[target + 3] = alpha_sum // samples
            if alpha_sum:
                output[target] = red_sum // alpha_sum
                output[target + 1] = green_sum // alpha_sum
                output[target + 2] = blue_sum // alpha_sum
    return bytes(output)


def chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", binascii.crc32(body))


def write_png(size: int, pixels: bytes) -> bytes:
    rows = b"".join(
        b"\0" + pixels[y * size * 4 : (y + 1) * size * 4]
        for y in range(size)
    )
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(
        b"IDAT", zlib.compress(rows, 9)
    ) + chunk(b"IEND", b"")


def make_ico(source: Path, destination: Path) -> None:
    width, height, pixels = read_png(source)
    sizes = (16, 24, 32, 48, 64, 128, 256)
    images = [write_png(size, resize_square(pixels, width, height, size)) for size in sizes]
    offset = 6 + 16 * len(images)
    entries = []
    for size, image in zip(sizes, images):
        encoded_size = 0 if size == 256 else size
        entries.append(struct.pack("<BBBBHHII", encoded_size, encoded_size, 0, 0,
                                   1, 32, len(image), offset))
        offset += len(image)
    destination.write_bytes(struct.pack("<HHH", 0, 1, len(images)) +
                            b"".join(entries) + b"".join(images))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: make_icon.py INPUT.png OUTPUT.ico")
    make_ico(Path(sys.argv[1]), Path(sys.argv[2]))
