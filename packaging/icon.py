#!/usr/bin/env python3
"""Generate the Wave app icon — a 1024x1024 master PNG.

Draws a macOS-style rounded "squircle" with a blue->teal vertical gradient and
three stacked white waves. Output feeds the .icns pipeline (see `make icon`).
"""
import math
import sys
from PIL import Image, ImageDraw, ImageFilter

S = 1024                      # master canvas
MARGIN = 100                  # transparent padding around the squircle
BOX = S - 2 * MARGIN          # squircle side
RADIUS = 200                  # corner radius
TOP = (56, 132, 255)          # gradient top   (blue)
BOTTOM = (38, 208, 206)       # gradient bottom (teal)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def rounded_mask(size, box, margin, radius):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle(
        [margin, margin, margin + box, margin + box], radius=radius, fill=255
    )
    return m


def gradient(size, top, bottom):
    g = Image.new("RGB", (size, size))
    px = g.load()
    for y in range(size):
        c = lerp(top, bottom, y / (size - 1))
        for x in range(size):
            px[x, y] = c
    return g


def wave_points(cx, cy, width, amp, phase, n=400):
    pts = []
    for i in range(n + 1):
        x = cx - width / 2 + width * i / n
        y = cy + amp * math.sin(2 * math.pi * (i / n) + phase)
        pts.append((x, y))
    return pts


def main(out):
    icon = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    grad = gradient(S, TOP, BOTTOM).convert("RGBA")
    mask = rounded_mask(S, BOX, MARGIN, RADIUS)
    icon.paste(grad, (0, 0), mask)

    # Three stacked waves on a transparent overlay, then masked to the squircle.
    overlay = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    cx = S / 2
    span = BOX * 0.66
    amp = BOX * 0.085
    stroke = int(BOX * 0.052)
    rows = [S * 0.40, S * 0.52, S * 0.64]
    alphas = [255, 220, 180]
    for cy, a in zip(rows, alphas):
        pts = wave_points(cx, cy, span, amp, phase=0.0)
        od.line(pts, fill=(255, 255, 255, a), width=stroke, joint="curve")
        # round the stroke ends
        for end in (pts[0], pts[-1]):
            r = stroke / 2
            od.ellipse([end[0] - r, end[1] - r, end[0] + r, end[1] + r],
                       fill=(255, 255, 255, a))

    overlay.putalpha(Image.composite(overlay.getchannel("A"),
                                     Image.new("L", (S, S), 0), mask))
    icon = Image.alpha_composite(icon, overlay)

    # Subtle top sheen for depth.
    sheen = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sheen)
    sd.rounded_rectangle([MARGIN, MARGIN, MARGIN + BOX, MARGIN + BOX // 2],
                         radius=RADIUS, fill=(255, 255, 255, 26))
    sheen.putalpha(Image.composite(sheen.getchannel("A"),
                                   Image.new("L", (S, S), 0), mask))
    sheen = sheen.filter(ImageFilter.GaussianBlur(40))
    icon = Image.alpha_composite(icon, sheen)

    icon.save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "packaging/icon-master.png")
