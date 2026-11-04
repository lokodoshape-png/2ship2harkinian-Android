#!/usr/bin/env python3
"""Draws the quest menu's PNG art into mm/assets/custom/overlays/questmenu/.

Everything here is original art made from simple shapes and noise, so nothing from the game or other packs is
redistributed. The files end up in 2ship.o2r under overlays/questmenu/. Run it again after changing a drawing:
    python3 mm/2s2h/Enhancements/QuestMenu/make_assets.py
"""

import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "..", "assets", "custom", "overlays", "questmenu")
SS = 4  # supersampling

GOLD = (244, 204, 98)
GOLD_DARK = (150, 105, 40)
GOLD_LIGHT = (255, 240, 180)
OUTLINE = (40, 20, 10)
PURPLE_DEEP = (28, 10, 44)
PURPLE = (58, 26, 86)
PURPLE_LIGHT = (110, 60, 140)
WINE = (104, 30, 48)


def canvas(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def finish(img, name):
    w, h = img.size
    img = img.resize((w // SS, h // SS), Image.LANCZOS)
    img.save(os.path.join(OUT, name), optimize=True)


def s(v):
    return int(round(v * SS))


def gold_band(d, box, width, radius):
    x0, y0, x1, y1 = box
    d.rounded_rectangle([s(x0), s(y0), s(x1), s(y1)], s(radius), outline=OUTLINE + (255,), width=s(width + 3))
    d.rounded_rectangle([s(x0 + 1), s(y0 + 1), s(x1 - 1), s(y1 - 1)], s(radius), outline=GOLD_DARK + (255,),
                        width=s(width + 1))
    d.rounded_rectangle([s(x0 + 1.5), s(y0 + 1.5), s(x1 - 1.5), s(y1 - 1.5)], s(radius), outline=GOLD + (255,),
                        width=s(width - 1))
    hi = width * 0.35
    d.rounded_rectangle([s(x0 + hi), s(y0 + hi), s(x1 - hi), s(y1 - hi)], s(radius), outline=GOLD_LIGHT + (200,),
                        width=s(1.2))


def diamond(d, cx, cy, r, fill, inner=None):
    d.polygon([(s(cx), s(cy - r)), (s(cx + r), s(cy)), (s(cx), s(cy + r)), (s(cx - r), s(cy))], fill=fill,
              outline=OUTLINE + (255,))
    if inner:
        r2 = r * 0.45
        d.polygon([(s(cx), s(cy - r2)), (s(cx + r2), s(cy)), (s(cx), s(cy + r2)), (s(cx - r2), s(cy))], fill=inner)


def curl(d, cx, cy, r, start, end, width=2.0, col=GOLD):
    d.arc([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], start, end, fill=OUTLINE + (255,), width=s(width + 1.6))
    d.arc([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], start, end, fill=col + (255,), width=s(width))


def frame():
    # 192x192, sliced 64 px on each side: an ornate gold border with filigree corners.
    W = 192
    img = canvas(W, W)
    d = ImageDraw.Draw(img)
    gold_band(d, (10, 10, W - 10, W - 10), 9, 14)
    d.rounded_rectangle([s(22), s(22), s(W - 22), s(W - 22)], s(8), outline=GOLD_DARK + (220,), width=s(1.5))
    for (cx, cy, a) in ((14, 14, 0), (W - 14, 14, 90), (W - 14, W - 14, 180), (14, W - 14, 270)):
        diamond(d, cx, cy, 11, GOLD + (255,), PURPLE_LIGHT + (255,))
        # two curls running along each edge from the corner
        dx = 1 if cx < W / 2 else -1
        dy = 1 if cy < W / 2 else -1
        curl(d, cx + dx * 26, cy + dy * 4, 8, 0, 360, 1.6)
        curl(d, cx + dx * 4, cy + dy * 26, 8, 0, 360, 1.6)
        d.ellipse([s(cx + dx * 26 - 2.5), s(cy + dy * 4 - 2.5), s(cx + dx * 26 + 2.5), s(cy + dy * 4 + 2.5)],
                  fill=GOLD_LIGHT + (255,))
        d.ellipse([s(cx + dx * 4 - 2.5), s(cy + dy * 26 - 2.5), s(cx + dx * 4 + 2.5), s(cy + dy * 26 + 2.5)],
                  fill=GOLD_LIGHT + (255,))
    # small studs at the middle of each edge
    for (cx, cy) in ((W / 2, 14), (W / 2, W - 14), (14, W / 2), (W - 14, W / 2)):
        diamond(d, cx, cy, 6, GOLD + (255,), WINE + (255,))
    finish(img, "frame.png")


def button(name, top, bottom, glow):
    # 96x96, sliced 28 px.
    W = 96
    img = canvas(W, W)
    if glow:
        g = Image.new("RGBA", img.size, (0, 0, 0, 0))
        gd = ImageDraw.Draw(g)
        gd.rounded_rectangle([s(6), s(6), s(W - 6), s(W - 6)], s(16), outline=(255, 210, 90, 200), width=s(6))
        g = g.filter(ImageFilter.GaussianBlur(s(4)))
        img.alpha_composite(g)
    fill = Image.new("RGBA", img.size, (0, 0, 0, 0))
    grad = Image.new("RGBA", img.size)
    gp = grad.load()
    for y in range(img.size[1]):
        t = y / img.size[1]
        c = tuple(int(top[i] * (1 - t) + bottom[i] * t) for i in range(3)) + (255,)
        for x in range(img.size[0]):
            gp[x, y] = c
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([s(10), s(10), s(W - 10), s(W - 10)], s(12), fill=255)
    fill.paste(grad, (0, 0), mask)
    img.alpha_composite(fill)
    d = ImageDraw.Draw(img)
    if glow:
        gold_band(d, (10, 10, W - 10, W - 10), 4.5, 12)
    else:
        d.rounded_rectangle([s(10), s(10), s(W - 10), s(W - 10)], s(12), outline=(170, 120, 190, 200), width=s(2))
    # top sheen
    d.rounded_rectangle([s(16), s(14), s(W - 16), s(30)], s(6), fill=(255, 255, 255, 22))
    finish(img, name)


def noise_layer(size, scale, seed):
    rnd = random.Random(seed)
    small = Image.new("L", (max(1, size[0] // scale), max(1, size[1] // scale)))
    small.putdata([rnd.randint(0, 255) for _ in range(small.size[0] * small.size[1])])
    return small.resize(size, Image.BICUBIC)


def parchment():
    W, H = 512, 512
    base = Image.new("RGB", (W, H), (240, 226, 196))
    for scale, amount, seed in ((64, 26, 1), (16, 14, 2), (4, 8, 3)):
        n = noise_layer((W, H), scale, seed)
        dark = Image.new("RGB", (W, H), (150, 110, 60))
        base = Image.composite(dark, base, n.point(lambda v, a=amount: int(v * a / 255)))
    # fibres
    d = ImageDraw.Draw(base)
    rnd = random.Random(9)
    for _ in range(260):
        x, y = rnd.uniform(0, W), rnd.uniform(0, H)
        ang = rnd.uniform(-0.4, 0.4)
        ln = rnd.uniform(8, 40)
        d.line([(x, y), (x + math.cos(ang) * ln, y + math.sin(ang) * ln)], fill=(205, 182, 140), width=1)
    # burnt, uneven edges
    edge = Image.new("L", (W, H), 0)
    ed = ImageDraw.Draw(edge)
    for i in range(40):
        ed.rectangle([i, i, W - 1 - i, H - 1 - i], outline=int(255 * (1 - i / 40) ** 2))
    n = noise_layer((W, H), 8, 5)
    edge = Image.composite(edge, Image.new("L", (W, H), 0), n.point(lambda v: 255 if v > 60 else 140))
    brown = Image.new("RGB", (W, H), (120, 72, 30))
    base = Image.composite(brown, base, edge.point(lambda v: int(v * 0.8)))
    img = base.convert("RGBA")
    a = Image.new("L", (W, H), 255)
    ad = ImageDraw.Draw(a)
    rnd = random.Random(11)
    for i in range(0, W, 3):
        for (x, y) in ((i, 0), (i, H - 1), (0, i), (W - 1, i)):
            r = rnd.uniform(1, 4)
            ad.ellipse([x - r, y - r, x + r, y + r], fill=0)
    img.putalpha(a)
    img.save(os.path.join(OUT, "parchment.png"), optimize=True)


def background():
    # 256x256, tiles: deep purple with a faint gold lattice.
    W = 256
    img = Image.new("RGB", (W, W), PURPLE_DEEP)
    n = noise_layer((W, W), 16, 21)
    lighter = Image.new("RGB", (W, W), (52, 22, 72))
    img = Image.composite(lighter, img, n.point(lambda v: int(v * 0.5)))
    big = img.resize((W * SS, W * SS), Image.BICUBIC).convert("RGBA")
    d = ImageDraw.Draw(big)
    step = 64
    for k in range(-W, 2 * W, step):
        d.line([(s(k), s(0)), (s(k + W), s(W))], fill=(244, 204, 98, 26), width=s(1.2))
        d.line([(s(k), s(W)), (s(k + W), s(0))], fill=(244, 204, 98, 26), width=s(1.2))
    for x in range(0, W + 1, step):
        for y in range(0, W + 1, step):
            for (cx, cy) in ((x, y), (x + step / 2, y + step / 2)):
                r = 3
                d.polygon([(s(cx), s(cy - r)), (s(cx + r), s(cy)), (s(cx), s(cy + r)), (s(cx - r), s(cy))],
                          fill=(244, 204, 98, 40))
    big.resize((W, W), Image.LANCZOS).save(os.path.join(OUT, "background.png"), optimize=True)


def banner():
    W, H = 768, 96
    img = canvas(W, H)
    d = ImageDraw.Draw(img)
    pts = [(40, 12), (W - 40, 12), (W - 6, 48), (W - 40, 84), (40, 84), (6, 48)]
    d.polygon([(s(x), s(y)) for x, y in pts], fill=PURPLE + (255,))
    d.polygon([(s(x), s(y)) for x, y in [(42, 14), (W - 42, 14), (W - 22, 36), (22, 36)]], fill=(110, 50, 130, 255))
    d.line([(s(x), s(y)) for x, y in pts + [pts[0]]], fill=OUTLINE + (255,), width=s(7), joint="curve")
    d.line([(s(x), s(y)) for x, y in pts + [pts[0]]], fill=GOLD + (255,), width=s(4), joint="curve")
    for side in (-1, 1):
        cx = W / 2 + side * (W / 2 - 80)
        curl(d, cx, 48, 12, 0, 360, 2.4)
        curl(d, cx - side * 22, 48, 7, 0, 360, 2.0)
        diamond(d, cx + side * 30, 48, 7, GOLD + (255,), WINE + (255,))
    finish(img, "banner.png")


def divider():
    W, H = 512, 24
    img = canvas(W, H)
    d = ImageDraw.Draw(img)
    for x0, x1 in ((16, W / 2 - 22), (W / 2 + 22, W - 16)):
        d.line([(s(x0), s(12)), (s(x1), s(12))], fill=GOLD_DARK + (255,), width=s(3))
        d.line([(s(x0), s(11.5)), (s(x1), s(11.5))], fill=GOLD + (255,), width=s(1.4))
    diamond(d, W / 2, 12, 9, GOLD + (255,), WINE + (255,))
    diamond(d, W / 2 - 30, 12, 4, GOLD + (255,))
    diamond(d, W / 2 + 30, 12, 4, GOLD + (255,))
    finish(img, "divider.png")


def corner():
    W = 96
    img = canvas(W, W)
    d = ImageDraw.Draw(img)
    curl(d, 30, 30, 18, 180, 450, 3)
    curl(d, 54, 22, 9, 90, 360, 2.2)
    curl(d, 22, 54, 9, 0, 270, 2.2)
    d.line([(s(30), s(12)), (s(88), s(12))], fill=GOLD + (255,), width=s(2.4))
    d.line([(s(12), s(30)), (s(12), s(88))], fill=GOLD + (255,), width=s(2.4))
    diamond(d, 30, 30, 8, GOLD + (255,), PURPLE_LIGHT + (255,))
    finish(img, "corner.png")


# ----- icons, 128x128

def icon_canvas():
    return canvas(128, 128)


def poly(d, pts, fill, width=3):
    d.polygon([(s(x), s(y)) for x, y in pts], fill=fill, outline=OUTLINE + (255,))
    d.line([(s(x), s(y)) for x, y in pts + [pts[0]]], fill=OUTLINE + (255,), width=s(width), joint="curve")


def icon_main():
    img = icon_canvas()
    d = ImageDraw.Draw(img)
    for (x, y) in ((64, 14), (34, 66), (94, 66)):
        poly(d, [(x, y), (x + 30, y + 50), (x - 30, y + 50)], GOLD + (255,))
        poly(d, [(x, y + 10), (x + 18, y + 40), (x - 18, y + 40)], (255, 228, 140, 255), 1)
    finish(img, "cat_main.png")


def icon_side():
    img = icon_canvas()
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([s(26), s(22), s(102), s(106)], s(6), fill=(240, 226, 196, 255), outline=OUTLINE + (255,),
                        width=s(3))
    for cy in (22, 106):
        d.ellipse([s(18), s(cy - 10), s(110), s(cy + 10)], fill=(214, 186, 138, 255), outline=OUTLINE + (255,),
                  width=s(3))
    for y in (44, 58, 72, 86):
        d.line([(s(38), s(y)), (s(90), s(y))], fill=(120, 80, 50, 255), width=s(3))
    diamond(d, 92, 92, 10, WINE + (255,))
    finish(img, "cat_side.png")


def icon_masks():
    img = icon_canvas()
    d = ImageDraw.Draw(img)
    # heart-shaped mask with spikes
    for i in range(9):
        a = math.radians(200 + i * 17.5)
        x, y = 64 + math.cos(a) * 50, 60 + math.sin(a) * 46
        poly(d, [(64 + math.cos(a - 0.12) * 34, 60 + math.sin(a - 0.12) * 30), (x, y),
                 (64 + math.cos(a + 0.12) * 34, 60 + math.sin(a + 0.12) * 30)], (250, 190, 60, 255), 2)
    d.ellipse([s(24), s(30), s(104), s(102)], fill=(150, 60, 170, 255), outline=OUTLINE + (255,), width=s(3))
    poly(d, [(64, 110), (34, 82), (94, 82)], (150, 60, 170, 255), 2)
    for ex in (46, 82):
        d.ellipse([s(ex - 13), s(52), s(ex + 13), s(78)], fill=(250, 190, 60, 255), outline=OUTLINE + (255,),
                  width=s(2))
        d.ellipse([s(ex - 6), s(58), s(ex + 6), s(72)], fill=(60, 170, 60, 255))
        d.ellipse([s(ex - 2.5), s(62), s(ex + 2.5), s(68)], fill=(20, 20, 20, 255))
    finish(img, "cat_masks.png")


def icon_people():
    img = icon_canvas()
    d = ImageDraw.Draw(img)
    for (cx, col) in ((46, (214, 170, 120)), (82, (238, 196, 140))):
        d.ellipse([s(cx - 16), s(26), s(cx + 16), s(58)], fill=col + (255,), outline=OUTLINE + (255,), width=s(3))
        d.pieslice([s(cx - 30), s(60), s(cx + 30), s(130)], 180, 360, fill=PURPLE_LIGHT + (255,),
                   outline=OUTLINE + (255,), width=s(3))
    finish(img, "cat_people.png")


def icon_places():
    img = icon_canvas()
    d = ImageDraw.Draw(img)
    # owl statue
    poly(d, [(30, 110), (98, 110), (88, 40), (64, 18), (40, 40)], (170, 160, 150, 255))
    poly(d, [(40, 40), (52, 22), (56, 38)], (150, 140, 130, 255), 2)
    poly(d, [(88, 40), (76, 22), (72, 38)], (150, 140, 130, 255), 2)
    for ex in (52, 76):
        d.ellipse([s(ex - 11), s(42), s(ex + 11), s(64)], fill=(250, 220, 120, 255), outline=OUTLINE + (255,),
                  width=s(2))
        d.ellipse([s(ex - 4), s(49), s(ex + 4), s(57)], fill=(30, 20, 20, 255))
    poly(d, [(64, 62), (70, 72), (64, 80), (58, 72)], (230, 170, 70, 255), 2)
    for y in (88, 98):
        d.arc([s(46), s(y - 8), s(82), s(y + 8)], 20, 160, fill=(120, 110, 100, 255), width=s(3))
    finish(img, "cat_places.png")


def icon_items():
    img = icon_canvas()
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([s(18), s(58), s(110), s(108)], s(4), fill=(150, 90, 40, 255), outline=OUTLINE + (255,),
                        width=s(3))
    d.pieslice([s(18), s(28), s(110), s(88)], 180, 360, fill=(170, 105, 48, 255), outline=OUTLINE + (255,),
               width=s(3))
    for x in (34, 94):
        d.rectangle([s(x - 5), s(42), s(x + 5), s(108)], fill=GOLD + (255,), outline=OUTLINE + (255,), width=s(2))
    d.rectangle([s(18), s(56), s(110), s(64)], fill=GOLD + (255,), outline=OUTLINE + (255,), width=s(2))
    d.rounded_rectangle([s(54), s(56), s(74), s(80)], s(3), fill=GOLD_LIGHT + (255,), outline=OUTLINE + (255,),
                        width=s(2))
    finish(img, "cat_items.png")


def icon_records():
    img = icon_canvas()
    d = ImageDraw.Draw(img)
    poly(d, [(64, 34), (110, 24), (110, 100), (64, 110)], (240, 226, 196, 255))
    poly(d, [(64, 34), (18, 24), (18, 100), (64, 110)], (226, 208, 170, 255))
    for i in range(4):
        y = 44 + i * 14
        d.line([(s(28), s(y - 2)), (s(56), s(y + 4))], fill=(120, 80, 50, 255), width=s(2.4))
    # a music note on the right page
    d.ellipse([s(76), s(78), s(92), s(92)], fill=(110, 50, 160, 255))
    d.line([(s(90), s(84)), (s(90), s(46))], fill=(110, 50, 160, 255), width=s(4))
    d.line([(s(90), s(46)), (s(102), s(54))], fill=(110, 50, 160, 255), width=s(4))
    finish(img, "cat_records.png")


def state_icons():
    # locked: padlock
    img = canvas(64, 64)
    d = ImageDraw.Draw(img)
    d.arc([s(18), s(8), s(46), s(40)], 180, 360, fill=OUTLINE + (255,), width=s(9))
    d.arc([s(18), s(8), s(46), s(40)], 180, 360, fill=(170, 170, 180, 255), width=s(5))
    d.line([(s(20), s(24)), (s(20), s(30))], fill=(170, 170, 180, 255), width=s(5))
    d.line([(s(44), s(24)), (s(44), s(30))], fill=(170, 170, 180, 255), width=s(5))
    d.rounded_rectangle([s(12), s(28), s(52), s(58)], s(5), fill=GOLD + (255,), outline=OUTLINE + (255,),
                        width=s(3))
    d.ellipse([s(28), s(36), s(36), s(44)], fill=OUTLINE + (255,))
    d.line([(s(32), s(42)), (s(32), s(50))], fill=OUTLINE + (255,), width=s(3))
    finish(img, "state_locked.png")

    # available: gold exclamation in a circle
    img = canvas(64, 64)
    d = ImageDraw.Draw(img)
    d.ellipse([s(6), s(6), s(58), s(58)], fill=(70, 30, 100, 255), outline=GOLD + (255,), width=s(4))
    d.rounded_rectangle([s(28), s(14), s(36), s(40)], s(3), fill=GOLD_LIGHT + (255,))
    d.ellipse([s(27), s(44), s(37), s(54)], fill=GOLD_LIGHT + (255,))
    finish(img, "state_available.png")

    # in progress: hourglass
    img = canvas(64, 64)
    d = ImageDraw.Draw(img)
    d.ellipse([s(6), s(6), s(58), s(58)], fill=(40, 60, 110, 255), outline=GOLD + (255,), width=s(4))
    poly(d, [(20, 16), (44, 16), (32, 32)], (240, 226, 196, 255), 2)
    poly(d, [(32, 32), (44, 48), (20, 48)], (240, 226, 196, 255), 2)
    poly(d, [(25, 44), (39, 44), (32, 36)], (230, 180, 70, 255), 1)
    d.line([(s(18), s(15)), (s(46), s(15))], fill=GOLD + (255,), width=s(3))
    d.line([(s(18), s(49)), (s(46), s(49))], fill=GOLD + (255,), width=s(3))
    finish(img, "state_progress.png")

    # done: green check on a gold seal
    img = canvas(64, 64)
    d = ImageDraw.Draw(img)
    pts = []
    for i in range(24):
        a = math.radians(i * 15)
        r = 28 if i % 2 == 0 else 24
        pts.append((32 + math.cos(a) * r, 32 + math.sin(a) * r))
    poly(d, pts, GOLD + (255,), 2)
    d.ellipse([s(13), s(13), s(51), s(51)], fill=(60, 150, 60, 255), outline=OUTLINE + (255,), width=s(2))
    d.line([(s(21), s(33)), (s(29), s(42)), (s(44), s(22))], fill=(20, 60, 20, 255), width=s(9), joint="curve")
    d.line([(s(21), s(33)), (s(29), s(42)), (s(44), s(22))], fill=(230, 255, 220, 255), width=s(5), joint="curve")
    finish(img, "state_done.png")

    # objective marks: empty / ticked boxes
    for done in (False, True):
        img = canvas(48, 48)
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([s(6), s(6), s(42), s(42)], s(6), fill=(255, 248, 230, 255), outline=(110, 70, 40, 255),
                            width=s(3))
        if done:
            d.line([(s(12), s(25)), (s(21), s(34)), (s(38), s(12))], fill=(20, 70, 20, 255), width=s(8),
                   joint="curve")
            d.line([(s(12), s(25)), (s(21), s(34)), (s(38), s(12))], fill=(84, 214, 70, 255), width=s(4),
                   joint="curve")
        finish(img, "mark_done.png" if done else "mark_todo.png")


def main():
    os.makedirs(OUT, exist_ok=True)
    frame()
    button("button.png", (86, 44, 112), (48, 20, 70), False)
    button("button_selected.png", (130, 66, 140), (78, 30, 84), True)
    parchment()
    background()
    banner()
    divider()
    corner()
    icon_main()
    icon_side()
    icon_masks()
    icon_people()
    icon_places()
    icon_items()
    icon_records()
    state_icons()


if __name__ == "__main__":
    main()
