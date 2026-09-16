#!/usr/bin/env python3
# Generate HideUtil.ico entirely with the Python standard library (no external
# drawing tools). Design: rounded Windows-11-style tile + white "eye with a
# diagonal slash" (the universal "hidden / private" glyph) for a one-click
# hide/restore (Boss Key) tool.
#
# Output: resources/HideUtil.ico  (primary, blue)  and
#         resources/HideUtil_dark.ico (dark variant)
#         resources/HideUtil_preview.png / HideUtil_dark_preview.png (256 preview)

import struct
import zlib
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def png_encode_rgba(px, w, h):
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw.extend(px[y * stride:(y + 1) * stride])
    comp = zlib.compress(bytes(raw), 9)

    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", comp) +
            chunk(b"IEND", b""))


def make_ico(pngs):
    out = bytearray()
    out += struct.pack("<HHH", 0, 1, len(pngs))
    offset = 6 + 16 * len(pngs)
    for size, png in pngs:
        bw = 0 if size >= 256 else size
        out += struct.pack("<BBBB", bw, bw, 0, 0)   # w, h, colors, reserved
        out += struct.pack("<HH", 1, 32)             # planes, bitcount (PNG-in-ICO)
        out += struct.pack("<II", len(png), offset)
        offset += len(png)
    for size, png in pngs:
        out += png
    return bytes(out)


def lerp(a, b, t):
    return a + (b - a) * t


def in_rounded(u, v):
    L, R, T, B, rad = 0.07, 0.93, 0.07, 0.93, 0.16
    if u < L or u > R or v < T or v > B:
        return False
    qx = max(L, min(R, u))
    qy = max(T, min(B, v))
    dx, dy = u - qx, v - qy
    if (qx == L or qx == R) and (qy == T or qy == B):
        if dx * dx + dy * dy > rad * rad:
            return False
    return True


def render_hires(size, S, top, bottom, slash):
    N = size * S
    buf = [0.0] * (N * N * 4)
    cx, cy = 0.5, 0.52
    rx, ry = 0.30, 0.17
    d = (0.70710678, 0.70710678)   # slash direction (top-left -> bottom-right)
    halfw, ext = 0.052, 0.42
    sr, sg, sb, sa = slash
    for y in range(N):
        for x in range(N):
            u = (x + 0.5) / N
            v = (y + 0.5) / N
            inside = in_rounded(u, v)
            if not inside:
                continue
            t = v
            r = lerp(top[0], bottom[0], t)
            g = lerp(top[1], bottom[1], t)
            b = lerp(top[2], bottom[2], t)
            a = 1.0
            # white eye (solid ellipse)
            ex, ey = (u - cx) / rx, (v - cy) / ry
            if ex * ex + ey * ey <= 1.0:
                r, g, b = 255.0, 255.0, 255.0
            # diagonal slash over everything inside the tile
            fx, fy = u - cx, v - cy
            proj = fx * d[0] + fy * d[1]
            px_, py_ = fx - proj * d[0], fy - proj * d[1]
            if abs(proj) <= ext and (px_ * px_ + py_ * py_) <= halfw * halfw:
                r = sr * sa + r * (1 - sa)
                g = sg * sa + g * (1 - sa)
                b = sb * sa + b * (1 - sa)
            i = (y * N + x) * 4
            buf[i], buf[i + 1], buf[i + 2], buf[i + 3] = r, g, b, a
    return buf, N


def downscale(buf, N, S, size):
    out = bytearray(size * size * 4)
    for ty in range(size):
        for tx in range(size):
            rs = gs = bs = as_ = 0.0
            for sy in range(S):
                for sx in range(S):
                    i = ((ty * S + sy) * N + (tx * S + sx)) * 4
                    rs += buf[i]
                    gs += buf[i + 1]
                    bs += buf[i + 2]
                    as_ += buf[i + 3]
            n = S * S
            j = (ty * size + tx) * 4
            out[j] = int(round(rs / n))
            out[j + 1] = int(round(gs / n))
            out[j + 2] = int(round(bs / n))
            out[j + 3] = int(round(as_ / n * 255))
    return out


def build_icon(top, bottom, slash, name):
    sizes = [16, 24, 32, 48, 64, 128, 256]
    S = 4
    pngs = []
    preview = None
    for size in sizes:
        hr, N = render_hires(size, S, top, bottom, slash)
        rgba = downscale(hr, N, S, size)
        png = png_encode_rgba(rgba, size, size)
        pngs.append((size, png))
        if size == 256:
            preview = rgba
    ico = make_ico(pngs)
    with open(os.path.join(ROOT, "resources", name), "wb") as f:
        f.write(ico)
    if preview is not None:
        png = png_encode_rgba(preview, 256, 256)
        base = name.rsplit(".", 1)[0]
        with open(os.path.join(ROOT, "resources", base + "_preview.png"), "wb") as f:
            f.write(png)
    print("wrote", name, len(ico), "bytes")


if __name__ == "__main__":
    # Variant A: Windows-blue tile (primary)
    build_icon((59, 130, 246), (29, 78, 216), (15, 23, 42, 0.88), "HideUtil.ico")
    # Variant B: dark slate tile
    build_icon((51, 65, 85), (15, 23, 42), (226, 232, 240, 0.85), "HideUtil_dark.ico")
    print("done")
