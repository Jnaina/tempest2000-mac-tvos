#!/usr/bin/env python3
"""Draws the Tempest 2000 app icon (neon vector web + claw) and writes T2K.icns."""
import math, os, subprocess, shutil, sys
from PIL import Image, ImageDraw, ImageFilter

S = 1024
def rr_mask(size, r):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size - 1, size - 1), r, fill=255)
    return m

def glow_layer(draw_fn, blur):
    l = Image.new("RGBA", (S, S), (0, 0, 0, 0)); draw_fn(ImageDraw.Draw(l))
    return l.filter(ImageFilter.GaussianBlur(blur)), l

def make():
    # background: deep space gradient
    bg = Image.new("RGBA", (S, S))
    px = bg.load(); c = S / 2
    for y in range(S):
        for x in range(S):
            d = min(1, math.hypot(x - c, y - c) / (S * .7))
            px[x, y] = (int(30 * (1 - d) + 3), int(8 * (1 - d) + 2), int(70 * (1 - d) + 12), 255)
    cx, cy = S / 2, S / 2 + 10
    # tube web: 16 lanes, 5 rings, perspective (near rim big, far rim small)
    n = 16
    def pt(i, t):  # t=0 far, 1 near
        r = 60 + 400 * t
        a = 2 * math.pi * i / n - math.pi / 2
        return cx + r * math.cos(a), cy + r * math.sin(a) * .93
    def web(dr):
        for i in range(n):
            dr.line([pt(i, 0), pt(i, 1)], fill=(60, 140, 255, 255), width=7)
        for k in range(6):
            t = (k / 5) ** 1.6
            pts = [pt(i, t) for i in range(n)]
            dr.line(pts + [pts[0]], fill=(60, 140, 255, 255), width=7)
    def hot(dr):  # near rim in hot colours, lanes tinted
        pts = [pt(i, 1) for i in range(n)]
        for i in range(n):
            col = [(255, 60, 60), (255, 200, 40), (60, 255, 120), (60, 200, 255)][i % 4]
            dr.line([pt(i, 1), pt((i + 1) % n, 1)], fill=col + (255,), width=12)
    # claw (yellow) on top rim segment at the bottom
    def claw(dr):
        a, b = pt(8, 1), pt(9, 1)
        mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        ox, oy = cx - mx, cy - my; L = math.hypot(ox, oy); ox, oy = ox / L, oy / L
        px_, py_ = -oy, ox
        loc = [(-1.15, -.1), (-.95, .9), (-.4, .4), (0, 1.15), (.4, .4), (.95, .9), (1.15, -.1)]
        pts = [(mx + px_ * u * 95 + ox * v * 95, my + py_ * u * 95 + oy * v * 95) for u, v in loc]
        dr.line(pts, fill=(255, 235, 60, 255), width=20, joint="curve")
    img = bg
    for fn, cols in ((web, 1), (hot, 1), (claw, 1)):
        g, sharp = glow_layer(fn, 22)
        g2, _ = glow_layer(fn, 8)
        img = Image.alpha_composite(img, g); img = Image.alpha_composite(img, g2)
        img = Image.alpha_composite(img, sharp)
    # bright core lines
    core = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(core)
    for i in range(n):
        d.line([pt(i, 0), pt(i, 1)], fill=(190, 225, 255, 200), width=2)
    img = Image.alpha_composite(img, core)
    # "T2K" wordmark
    from PIL import ImageFont
    for path in ("/System/Library/Fonts/Supplemental/Impact.ttf", "/System/Library/Fonts/Helvetica.ttc"):
        if os.path.exists(path):
            f = ImageFont.truetype(path, 190); break
    else: f = ImageFont.load_default()
    t = Image.new("RGBA", (S, S), (0, 0, 0, 0)); td = ImageDraw.Draw(t)
    w = td.textlength("T2K", font=f)
    td.text(((S - w) / 2, 60), "T2K", font=f, fill=(255, 90, 200, 255), stroke_width=4, stroke_fill=(255, 230, 250, 255))
    img = Image.alpha_composite(img, t.filter(ImageFilter.GaussianBlur(18)))
    img = Image.alpha_composite(img, t)
    # macOS-style rounded square with margin
    m = 100
    icon = img.resize((S - 2 * m, S - 2 * m), Image.LANCZOS)
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(icon, (m, m), rr_mask(S - 2 * m, 185))
    return out

def main(dest):
    base = make(); base.save("icon_1024.png")
    d = "T2K.iconset"; shutil.rmtree(d, ignore_errors=True); os.mkdir(d)
    for sz in (16, 32, 128, 256, 512):
        base.resize((sz, sz), Image.LANCZOS).save(f"{d}/icon_{sz}x{sz}.png")
        base.resize((sz * 2, sz * 2), Image.LANCZOS).save(f"{d}/icon_{sz}x{sz}@2x.png")
    subprocess.check_call(["iconutil", "-c", "icns", d, "-o", dest])
    shutil.rmtree(d)
if __name__ == "__main__": main(sys.argv[1] if len(sys.argv) > 1 else "T2K.icns")
