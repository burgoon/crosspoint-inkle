#!/usr/bin/env python3
"""Generate src/apps_local/inkle/InkleIcon.h: the Home-menu / Cover Grid tab icon.

Six letter tiles in the game's three states (outline, filled, thick border),
drawn upright on a 32x32 canvas and then rotated 90 degrees counter-clockwise,
which is the orientation GfxRenderer::drawIcon expects (the stock icons in
src/components/icons/ are stored the same way). Packing matches those files:
1 bit per pixel, MSB first, 0 = ink.

Usage: python3 tools/inkle/gen_icon.py
"""
import os

W = 32
TILE = 8
GAP = 3


def main():
    img = [[1] * W for _ in range(W)]

    def px(x, y):
        if 0 <= x < W and 0 <= y < W:
            img[y][x] = 0

    def tile(x, y, style):
        thickness = 4 if style == "thick" else 2
        for yy in range(y, y + TILE):
            for xx in range(x, x + TILE):
                edge = xx < x + thickness or xx >= x + TILE - thickness or yy < y + thickness or yy >= y + TILE - thickness
                if style == "fill" or edge:
                    px(xx, yy)

    x0 = (W - (3 * TILE + 2 * GAP)) // 2
    y0 = (W - (2 * TILE + GAP)) // 2
    styles = [["line", "fill", "line"], ["thick", "line", "fill"]]
    for r in range(2):
        for c in range(3):
            tile(x0 + c * (TILE + GAP), y0 + r * (TILE + GAP), styles[r][c])

    rotated = [[img[x][W - 1 - y] for x in range(W)] for y in range(W)]
    rows = []
    for y in range(W):
        bits = "".join(str(b) for b in rotated[y])
        rows.append([int(bits[i:i + 8], 2) for i in range(0, W, 8)])

    out = [
        "#pragma once",
        "#include <cstdint>",
        "",
        "// size: 32x32, same packing as src/components/icons/*.h (1 bpp, MSB first, 0 = ink),",
        "// pre-rotated 90 CCW for drawIcon like the stock icons. Six letter tiles in the",
        "// game's three states (outline, filled, thick border); see tools/inkle/gen_icon.py.",
        "static const uint8_t InkleIcon[] = {",
    ]
    for y in range(W):
        out.append("    " + ", ".join(f"0x{b:02X}" for b in rows[y]) + ",")
    out.append("};")

    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.normpath(os.path.join(here, "..", "..", "src", "apps_local", "inkle", "InkleIcon.h"))
    with open(path, "w", encoding="ascii") as fh:
        fh.write("\n".join(out) + "\n")
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
