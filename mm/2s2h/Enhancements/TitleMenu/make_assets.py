#!/usr/bin/env python3
"""Builds the title menu's art in mm/assets/custom/overlays/titlemenu/ from Rafael's title screen picture.

The picture (1672x941, with the menu drawn on it) is scaled to the 1920x1080 page the menu is laid out on. The
buttons and the button hints are painted out of the background, and the icons are lifted from the picture so the
game can draw the buttons itself (and move the highlight). The labels are rendered with Inter (SIL Open Font
License). Run it again after changing the picture:
    python3 mm/2s2h/Enhancements/TitleMenu/make_assets.py <picture.png> [concept art...]
The concept art after the picture (Rafael's other concept pictures) goes to the Extras gallery as concept_N.jpg.
"""

import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "..", "assets", "custom", "overlays", "titlemenu")
W, H = 1920, 1080
FONT = "/usr/share/fonts/opentype/inter/Inter-Medium.otf"


def scale_box(box, k):
    return tuple(int(round(v * k)) for v in box)


def push_pull(a, w):
    """Fills where w == 0 from the known pixels around, smoothly, over an image pyramid."""
    levels = []
    col, wt = a * w[..., None], w.copy()
    while min(wt.shape) > 4:
        levels.append((col, wt))
        h2, w2 = wt.shape[0] // 2 * 2, wt.shape[1] // 2 * 2
        col = col[:h2, :w2].reshape(h2 // 2, 2, w2 // 2, 2, 3).sum(axis=(1, 3))
        wt = wt[:h2, :w2].reshape(h2 // 2, 2, w2 // 2, 2).sum(axis=(1, 3))
    filled = col / np.maximum(wt, 1e-6)[..., None]
    for col, wt in reversed(levels):
        up = np.asarray(Image.fromarray(np.clip(filled, 0, 255).astype(np.uint8)).resize(
            (wt.shape[1], wt.shape[0]), Image.BILINEAR), dtype=np.float64)
        known = col / np.maximum(wt, 1e-6)[..., None]
        t = np.clip(wt, 0, 1)[..., None]
        filled = known * t + up * (1 - t)
    return filled


def inpaint(img, boxes):
    """Paints the boxes out. Inside each box every row runs from the colour at its left edge to the colour at its
    right edge, so the sky keeps its bands (dark at the top, the sunset glow lower down); that is blended with a
    smooth fill from all sides, softened at the edges and given a little grain so it does not look flat."""
    a = np.asarray(img, dtype=np.float64)
    out = a.copy()
    rng = np.random.default_rng(7)
    for (x0, y0, x1, y1) in boxes:
        left = a[y0:y1, max(x0 - 10, 0):x0].mean(axis=1)
        right = a[y0:y1, x1:x1 + 10].mean(axis=1)
        t = np.linspace(0, 1, x1 - x0)[None, :, None]
        rows = left[:, None, :] * (1 - t) + right[:, None, :] * t
        rows = np.asarray(Image.fromarray(np.clip(rows, 0, 255).astype(np.uint8)).filter(
            ImageFilter.GaussianBlur(12)), dtype=np.float64)
        w = np.ones(a.shape[:2])
        w[y0:y1, x0:x1] = 0
        smooth = push_pull(a, w)[y0:y1, x0:x1]
        # Next to the box's edges the smooth fill (which meets the edges exactly) takes over.
        yy, xx = np.mgrid[y0:y1, x0:x1]
        edge = np.minimum(np.minimum(xx - x0, x1 - 1 - xx), np.minimum(yy - y0, y1 - 1 - yy))
        rows_share = (np.clip(edge / 60.0, 0, 1) * 0.6)[..., None]
        fill = rows * rows_share + smooth * (1 - rows_share)
        fill += rng.normal(0, 2.0, fill.shape[:2])[..., None]
        patch = np.zeros(a.shape)
        patch[y0:y1, x0:x1] = fill
        m = Image.new("L", (a.shape[1], a.shape[0]), 0)
        ImageDraw.Draw(m).rectangle((x0 + 12, y0 + 12, x1 - 12, y1 - 12), fill=255)
        soft = np.asarray(m.filter(ImageFilter.GaussianBlur(7)), dtype=np.float64)[..., None] / 255.0
        out = out * (1 - soft) + patch * soft
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def lift_icon(img, box, name, size=64):
    """Takes a light icon off the dark sky: alpha from how much brighter each pixel is than the surroundings."""
    crop = np.asarray(img.crop(box).convert("RGB"), dtype=np.float64)
    lum = crop @ np.array([0.299, 0.587, 0.114])
    border = np.concatenate([lum[0], lum[-1], lum[:, 0], lum[:, -1]])
    bg = np.median(border)
    top = np.percentile(lum, 99)
    alpha = np.clip((lum - bg - 12) / max(top - bg - 12, 1), 0, 1)
    rgba = np.zeros(crop.shape[:2] + (4,), dtype=np.uint8)
    rgba[..., :3] = 255
    rgba[..., 3] = (alpha * 255).astype(np.uint8)
    icon = Image.fromarray(rgba, "RGBA")
    bbox = icon.getbbox()
    icon = icon.crop(bbox)
    side = max(icon.size)
    sq = Image.new("RGBA", (side, side), (255, 255, 255, 0))
    sq.paste(icon, ((side - icon.width) // 2, (side - icon.height) // 2))
    sq.resize((size, size), Image.LANCZOS).save(os.path.join(OUT, name), optimize=True)


def label(text, name, px=40):
    font = ImageFont.truetype(FONT, px)
    l, t, r, b = font.getbbox(text)
    pad = 8
    img = Image.new("RGBA", (r - l + pad * 2, px + pad * 2 + 6), (0, 0, 0, 0))
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).text((pad - l + 1, pad + 2), text, font=font, fill=(0, 0, 0, 170))
    shadow = shadow.filter(ImageFilter.GaussianBlur(2))
    img.alpha_composite(shadow)
    ImageDraw.Draw(img).text((pad - l, pad), text, font=font, fill=(255, 255, 255, 255))
    img.save(os.path.join(OUT, name), optimize=True)


def highlight(name, w=664, h=86, glow=26):
    """The selected button: a long purple plate with pointed ends, a light rim, a glow and two sparkles."""
    ss = 4
    W2, H2 = (w + glow * 2) * ss, (h + glow * 2) * ss
    x0, y0, x1, y1 = glow * ss, glow * ss, (glow + w) * ss, (glow + h) * ss
    tip = 26 * ss
    cy = (y0 + y1) / 2
    shape = [(x0, cy), (x0 + tip, y0), (x1 - tip, y0), (x1, cy), (x1 - tip, y1), (x0 + tip, y1)]

    base = Image.new("RGBA", (W2, H2), (0, 0, 0, 0))
    g = Image.new("L", (W2, H2), 0)
    ImageDraw.Draw(g).polygon(shape, fill=255)
    g = g.filter(ImageFilter.GaussianBlur(14 * ss))
    glow_layer = Image.new("RGBA", (W2, H2), (190, 90, 255, 0))
    glow_layer.putalpha(g.point(lambda v: int(v * 0.85)))
    base.alpha_composite(glow_layer)

    # Fill: left-to-right purple gradient, brighter in the middle row.
    fill = np.zeros((H2, W2, 4), dtype=np.float64)
    xs = np.linspace(0, 1, W2)[None, :]
    ys = np.linspace(0, 1, H2)[:, None]
    left = np.array([150, 60, 200])
    right = np.array([80, 30, 130])
    t = np.clip((xs * W2 - x0) / (x1 - x0), 0, 1)
    rgb = left[None, None, :] * (1 - t[..., None]) + right[None, None, :] * t[..., None]
    band = 1.0 - np.abs((ys * H2 - cy) / ((y1 - y0) / 2)) * 0.35
    rgb = rgb * band[..., None]
    fill[..., :3] = rgb
    m = Image.new("L", (W2, H2), 0)
    ImageDraw.Draw(m).polygon(shape, fill=235)
    fill[..., 3] = np.asarray(m, dtype=np.float64)
    base.alpha_composite(Image.fromarray(fill.astype(np.uint8), "RGBA"))

    d = ImageDraw.Draw(base)
    d.line(shape + [shape[0]], fill=(225, 175, 255, 255), width=3 * ss, joint="curve")
    inner = [(x0 + 7 * ss, cy), (x0 + tip + 4 * ss, y0 + 5 * ss), (x1 - tip - 4 * ss, y0 + 5 * ss),
             (x1 - 7 * ss, cy), (x1 - tip - 4 * ss, y1 - 5 * ss), (x0 + tip + 4 * ss, y1 - 5 * ss)]
    d.line(inner + [inner[0]], fill=(200, 140, 255, 110), width=1 * ss)
    for sx in (x0 + 17 * ss, x1 - 17 * ss):
        r = 7 * ss
        d.polygon([(sx, cy - r), (sx + r * 0.35, cy - r * 0.35), (sx + r, cy), (sx + r * 0.35, cy + r * 0.35),
                   (sx, cy + r), (sx - r * 0.35, cy + r * 0.35), (sx - r, cy), (sx - r * 0.35, cy - r * 0.35)],
                  fill=(240, 220, 255, 255))
    base = base.resize((W2 // ss, H2 // ss), Image.LANCZOS)
    base.save(os.path.join(OUT, name), optimize=True)


def white_icon(name, draw, size=64):
    """A white pictogram drawn at 4x on a 256 canvas, like the icons lifted from the picture."""
    img = Image.new("L", (256, 256), 0)
    draw(ImageDraw.Draw(img))
    icon = Image.new("RGBA", (256, 256), (255, 255, 255, 0))
    icon.putalpha(img)
    icon.resize((size, size), Image.LANCZOS).save(os.path.join(OUT, name), optimize=True)


def draw_book(d):
    # An open notebook: two pages with lines and a spine.
    d.polygon([(20, 60), (124, 80), (124, 230), (20, 210)], fill=255)
    d.polygon([(236, 60), (132, 80), (132, 230), (236, 210)], fill=255)
    for i in range(4):
        y = 108 + i * 26
        d.line([(42, y - 6), (106, y + 6)], fill=0, width=8)
        d.line([(214, y - 6), (150, y + 6)], fill=0, width=8)
    d.rounded_rectangle([60, 20, 100, 70], 6, fill=255)  # bookmark ribbon
    d.polygon([(60, 70), (80, 56), (100, 70)], fill=0)


def draw_scroll(d):
    # A quest scroll: rolled ends and a check mark.
    d.rounded_rectangle([50, 40, 206, 216], 14, fill=255)
    d.ellipse([28, 24, 92, 72], fill=255)
    d.ellipse([164, 184, 228, 232], fill=255)
    d.ellipse([44, 36, 76, 60], fill=0)
    d.line([(84, 130), (116, 162), (176, 92)], fill=0, width=20, joint="curve")


def draw_bag(d):
    # The item bag: a pouch with a tied neck.
    d.ellipse([34, 84, 222, 238], fill=255)
    d.polygon([(86, 96), (170, 96), (190, 40), (66, 40)], fill=255)
    d.rectangle([80, 86, 176, 104], fill=0)
    d.ellipse([110, 140, 146, 176], fill=0)


def draw_hourglass(d):
    # Continue: an hourglass, for the three days.
    d.rectangle([52, 22, 204, 44], fill=255)
    d.rectangle([52, 212, 204, 234], fill=255)
    d.polygon([(70, 44), (186, 44), (136, 128), (186, 212), (70, 212), (120, 128)], fill=255)
    d.polygon([(92, 60), (164, 60), (128, 112)], fill=0)
    d.polygon([(128, 150), (170, 196), (86, 196)], fill=0)
    d.polygon([(128, 166), (150, 190), (106, 190)], fill=255)


def draw_gamepad(d):
    # Controls: a controller with a d-pad and two buttons.
    d.rounded_rectangle([18, 74, 238, 190], 52, fill=255)
    d.ellipse([18, 120, 98, 230], fill=255)
    d.ellipse([158, 120, 238, 230], fill=255)
    d.rectangle([56, 116, 76, 160], fill=0)
    d.rectangle([44, 128, 88, 148], fill=0)
    d.ellipse([168, 106, 194, 132], fill=0)
    d.ellipse([190, 130, 216, 156], fill=0)


def draw_door(d):
    # Quit: a door with an arrow going out.
    d.rectangle([40, 24, 150, 232], fill=255)
    d.rectangle([62, 46, 128, 232], fill=0)
    d.polygon([(62, 46), (128, 62), (128, 222), (62, 232)], fill=255)
    d.ellipse([108, 130, 122, 144], fill=0)
    d.rectangle([150, 118, 210, 138], fill=255)
    d.polygon([(200, 92), (244, 128), (200, 164)], fill=255)


def concept(path, name):
    img = Image.open(path).convert("RGB")
    img.thumbnail((1600, 1600), Image.LANCZOS)
    img.save(os.path.join(OUT, name), quality=88, optimize=True)


def moon_layer(bg, name):
    """The moon on its own, so the menu can let it drift and breathe. The clock tower in front of it is left out."""
    cx, cy, r = 1492, -43, 462
    x0, x1, y1 = cx - r - 8, W, cy + r + 8
    mask = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(mask)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    d.polygon([(1515, 460), (1520, 360), (1560, 320), (1580, 300), (1600, 320), (1640, 360), (1650, 460)], fill=0)
    mask = mask.filter(ImageFilter.GaussianBlur(4))
    layer = bg.convert("RGBA")
    layer.putalpha(mask)
    layer.crop((x0, 0, x1, y1)).save(os.path.join(OUT, name), optimize=True)
    return x0


def edition_title(name):
    """DEFINITIVE EDITION under the logo, in the logo's serif capitals with a gold edge."""
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", 30)
    text = "DEFINITIVE  EDITION"
    spacing = 6
    widths = [font.getbbox(c)[2] - font.getbbox(c)[0] if c != " " else 10 for c in text]
    w = sum(widths) + spacing * (len(text) - 1) + 40
    img = Image.new("RGBA", (w, 70), (0, 0, 0, 0))
    glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    x = 20
    for c, cw in zip(text, widths):
        if c != " ":
            ImageDraw.Draw(glow).text((x - font.getbbox(c)[0], 16), c, font=font, fill=(190, 90, 255, 255))
            ImageDraw.Draw(img).text((x - font.getbbox(c)[0] + 1, 18), c, font=font, fill=(40, 10, 50, 255))
            ImageDraw.Draw(img).text((x - font.getbbox(c)[0], 16), c, font=font, fill=(236, 214, 160, 255))
        x += cw + spacing
    glow = glow.filter(ImageFilter.GaussianBlur(6))
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.alpha_composite(glow)
    out.alpha_composite(img)
    # Thin gold rules on both sides.
    d = ImageDraw.Draw(out)
    out.save(os.path.join(OUT, name), optimize=True)


def main():
    src = Image.open(sys.argv[1]).convert("RGB")
    k = W / src.width
    up = src.resize((W, H), Image.LANCZOS)

    # The buttons (with the glow around the highlighted one) and the hints in the corner.
    boxes = [scale_box((70, 368, 725, 818), k), scale_box((1280, 848, 1648, 918), k)]

    # Icons first, from the untouched picture.
    lift_icon(up, scale_box((140, 495, 210, 555), k), "icon_triforce.png")
    lift_icon(up, scale_box((140, 575, 210, 630), k), "icon_folder.png")
    lift_icon(up, scale_box((140, 650, 210, 710), k), "icon_gear.png")
    lift_icon(up, scale_box((140, 728, 210, 785), k), "icon_mask.png")

    bg = inpaint(up, boxes)
    bg.save(os.path.join(OUT, "background.jpg"), quality=90, optimize=True)
    print("moon layer starts at x =", moon_layer(bg, "moon.png"))
    edition_title("edition.png")
    white_icon("icon_book.png", draw_book)
    white_icon("icon_scroll.png", draw_scroll)
    white_icon("icon_bag.png", draw_bag)
    white_icon("icon_hourglass.png", draw_hourglass)
    white_icon("icon_gamepad.png", draw_gamepad)
    white_icon("icon_door.png", draw_door)
    for i, path in enumerate(sys.argv[2:]):
        concept(path, "concept_%d.jpg" % (i + 1))
    bg.resize((240, 135), Image.LANCZOS).filter(ImageFilter.GaussianBlur(3)).save(
        os.path.join(OUT, "background_blur.jpg"), quality=85)

    for text, name in (("Continuar", "label_continue.png"), ("Novo Jogo", "label_new.png"),
                       ("Carregar Jogo", "label_load.png"), ("Opções", "label_options.png"),
                       ("Extras", "label_extras.png"), ("Música", "label_music.png"),
                       ("Som e mira (originais)", "label_sound.png"), ("Abertura original", "label_opening.png"),
                       ("Voltar", "label_back.png"), ("Bombers Book", "label_bombers.png"),
                       ("Missões", "label_quests.png"), ("Inventário", "label_inventory.png"),
                       ("Créditos", "label_credits.png"), ("Versão", "label_version.png"), ("Sim", "label_yes.png"),
                       ("Não", "label_no.png"), ("Iniciar Jogo", "label_start.png"),
                       ("Arquivos de Jogo", "label_files.png"), ("Configurações", "label_settings.png"),
                       ("Controles", "label_controls.png"), ("Sair", "label_quit.png")):
        label(text, name)
    for text, name in (("Tema de Majora", "music_majora.png"), ("Últimas Horas", "music_final_hours.png"),
                       ("Torre do Relógio", "music_clock_tower.png"), ("Castelo de Ikana", "music_ikana_castle.png"),
                       ("Seleção original", "music_file_select.png")):
        label(text, name, px=32)
    label("Selecionar", "hint_select.png", px=32)
    label("Voltar", "hint_back.png", px=32)
    highlight("highlight.png")


if __name__ == "__main__":
    main()
