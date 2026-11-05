"""Generate the original, seamless albedo patterns and the shared black fallback.

Run with Python 3; no third-party packages are needed. uv_grid.png is kept as-is.
"""

from pathlib import Path
import struct
import zlib


def write_png(name, size, pixel):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))

    rows = b"".join(b"\0" + bytes(channel for x in range(size)
                                for channel in (*pixel(x, y), 255))
                    for y in range(size))
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    Path(__file__).with_name(name).write_bytes(data)


def brick(x, y):
    row = y // 64
    bx, by = (x + (row % 2) * 64) % 128, y % 64
    grain = ((x * 13 + y * 7 + x * y * 3) % 13) - 6
    if bx < 5 or by < 5:
        return tuple(c + grain for c in (162, 153, 133))
    variation = ((x + (row % 2) * 64) // 128 % 4 + row * 3) % 5 * 5
    edge = 15 if bx < 8 or by < 8 else 0
    return tuple(c + grain - variation - edge for c in (181, 81, 49))


def tiles(x, y):
    tx, ty = x % 128, y % 128
    if tx < 5 or ty < 5:
        return (76, 84, 88)
    light = (x // 128 + y // 128) % 2 == 0
    base = (221, 214, 178) if light else (36, 112, 141)
    if min(tx, ty, 127 - tx, 127 - ty) < 11:
        base = tuple(max(0, c - 30) for c in base)
    if abs(tx - 66) + abs(ty - 66) < 18:
        base = (36, 112, 141) if light else (221, 214, 178)
    return base


if __name__ == "__main__":
    write_png("brick.png", 512, brick)
    write_png("tiles.png", 512, tiles)
    write_png("black.png", 1, lambda x, y: (0, 0, 0))
