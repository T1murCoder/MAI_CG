"""Generate original material maps and the shared black fallback.

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


def metal_region(x, y):
    """Seams (0), polished metal (1) and matte coating (2), shared by both maps."""
    tx, ty = x % 128, y % 128
    if min(tx, ty, 127 - tx, 127 - ty) < 6:
        return 0
    return 1 if (x // 128 + y // 128) % 2 == 0 else 2


def metal_albedo(x, y):
    region = metal_region(x, y)
    if region == 0:
        return (28, 34, 40)
    if region == 1:
        # Horizontal brush marks are albedo detail, not a normal map.
        grain = (y * 7 + x // 8) % 11 - 5
        return tuple(c + grain for c in (174, 186, 198))
    return (42, 70, 92)


def metal_specular(x, y):
    # Linear reflectance: coated panels and seams are fully matte.
    value = 240 if metal_region(x, y) == 1 else 0
    return (value, value, value)


def panel_emissive(x, y):
    """Shared light geometry: frame, three strips and status indicators."""
    cyan, amber = (32, 216, 255), (255, 132, 24)
    if 52 <= x < 460 and 52 <= y < 460:
        if min(x - 52, 459 - x, y - 52, 459 - y) < 12:
            return cyan
    if 104 <= x < 408 and 144 <= y < 336 and (y - 144) % 64 < 24:
        return amber if (y - 144) // 64 == 1 else cyan
    for index, center in enumerate((112, 160, 208)):
        if (x - center) ** 2 + (y - 400) ** 2 < 9 ** 2:
            return amber if index == 2 else (64, 255, 128)
    return (0, 0, 0)


def panel_albedo(x, y):
    emission = panel_emissive(x, y)
    if any(emission):
        return tuple(int(c * 0.55) for c in emission)
    if min(x, y, 511 - x, 511 - y) < 24:
        return (16, 21, 27)
    # Screws belong to the housing and must stay dark without external light.
    for cx, cy in ((36, 36), (475, 36), (36, 475), (475, 475)):
        if (x - cx) ** 2 + (y - cy) ** 2 < 6 ** 2:
            return (94, 104, 112)
    return (36, 45, 54)


if __name__ == "__main__":
    write_png("brick.png", 512, brick)
    write_png("tiles.png", 512, tiles)
    write_png("black.png", 1, lambda x, y: (0, 0, 0))
    write_png("metal_albedo.png", 512, metal_albedo)
    write_png("metal_specular.png", 512, metal_specular)
    write_png("panel_albedo.png", 512, panel_albedo)
    write_png("panel_emissive.png", 512, panel_emissive)
