#!/usr/bin/env python3
"""Generates T2KTV/Assets.xcassets: layered tvOS app icon (parallax) + top shelf images."""
import json, math, os, shutil
from PIL import Image, ImageDraw, ImageFilter, ImageFont

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon_source.jpg")   # the Tempest 2000 title logo

def art(W, H):
    """returns (back, front) RGBA layers of size WxH: dark starfield / the logo, keyed off black for parallax"""
    from PIL import ImageEnhance, ImageChops
    src = Image.open(SRC).convert("RGB")
    src = src.crop((6, 4, src.width - 6, src.height - 4))            # trim the source image's own border
    # back: dark space with a deterministic scatter of stars (the logo lives on the front layer only)
    import random
    rnd = random.Random(2000)
    back = Image.new("RGBA", (W, H), (2, 2, 10, 255)); d = ImageDraw.Draw(back)
    for _ in range(int(W * H / 5000)):
        x, y = rnd.uniform(0, W), rnd.uniform(0, H); r = rnd.choice((0.8, 1.0, 1.4, 2.0)) * H / 768
        v = rnd.randint(110, 255); d.ellipse((x - r, y - r, x + r, y + r), fill=(v, v, min(255, v + 20), 255))
    # front: the logo scaled to fit, with black made transparent (alpha from the brightest channel)
    fw = int(W * 0.92); fh = int(fw * src.height / src.width)
    if fh > H * 0.92:
        fh = int(H * 0.92); fw = int(fh * src.width / src.height)
    logo = src.resize((fw, fh), Image.LANCZOS)
    r, g, b = logo.split()
    lum = ImageChops.lighter(ImageChops.lighter(r, g), b)
    alpha = lum.point(lambda v: min(255, int(v * 255 / 40)))            # fully opaque above ~16% brightness
    logo = logo.convert("RGBA"); logo.putalpha(alpha)
    front = Image.new("RGBA", (W, H), (0, 0, 0, 0)); front.paste(logo, ((W - fw) // 2, (H - fh) // 2), logo)
    return back, front

def W_(p, o):
    os.makedirs(os.path.dirname(p), exist_ok=True); json.dump(o, open(p, "w"), indent=2)
INFO = {"version": 1, "author": "xcode"}

def stack(root, name, W, H, scales=(1,)):
    st = f"{root}/{name}.imagestack"
    W_(f"{st}/Contents.json", {"layers": [{"filename": "Front.imagestacklayer"}, {"filename": "Back.imagestacklayer"}], "info": INFO})
    for lay, idx in (("Front", 1), ("Back", 0)):
        d = f"{st}/{lay}.imagestacklayer"
        W_(f"{d}/Contents.json", {"info": INFO})
        imgs = []
        for sc in scales:
            im = art(W * sc, H * sc)[idx]
            if lay == "Back": im = im.convert("RGB")
            fn = f"{lay.lower()}{'@2x' if sc == 2 else ''}.png"
            os.makedirs(f"{d}/Content.imageset", exist_ok=True); im.save(f"{d}/Content.imageset/{fn}")
            imgs.append({"idiom": "tv", "filename": fn, "scale": f"{sc}x"})
        W_(f"{d}/Content.imageset/Contents.json", {"images": imgs, "info": INFO})

def shelf(root, name, W, H):
    d = f"{root}/{name}.imageset"; b, f = art(W, H); im = Image.alpha_composite(b, f).convert("RGB")
    os.makedirs(d, exist_ok=True); im.save(f"{d}/shelf.png")
    W_(f"{d}/Contents.json", {"images": [{"idiom": "tv", "filename": "shelf.png", "scale": "1x"}], "info": INFO})

if __name__ == "__main__":
    X = "T2KTV/Assets.xcassets"; shutil.rmtree(X, ignore_errors=True)
    W_(f"{X}/Contents.json", {"info": INFO})
    B = f"{X}/AppIcon.brandassets"
    W_(f"{B}/Contents.json", {"assets": [
        {"size": "1280x768", "idiom": "tv", "filename": "App Icon - Large.imagestack", "role": "primary-app-icon"},
        {"size": "400x240", "idiom": "tv", "filename": "App Icon - Small.imagestack", "role": "primary-app-icon"},
        {"size": "2320x720", "idiom": "tv", "filename": "Top Shelf Image Wide.imageset", "role": "top-shelf-image-wide"},
        {"size": "1920x720", "idiom": "tv", "filename": "Top Shelf Image.imageset", "role": "top-shelf-image"}], "info": INFO})
    stack(B, "App Icon - Large", 1280, 768); stack(B, "App Icon - Small", 400, 240, (1, 2))
    shelf(B, "Top Shelf Image Wide", 2320, 720); shelf(B, "Top Shelf Image", 1920, 720)
    im = Image.alpha_composite(*art(1280, 768)); im.convert("RGB").save("/tmp/tv_preview.png")
