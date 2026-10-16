#!/usr/bin/env python3
# C64 GrooveGoat logo: an eighth note in front of the C64 rainbow stripes.
import sys, math
from PIL import Image, ImageDraw, ImageFilter

S = 1024  # master size
SS = 2    # supersampling

RAINBOW = [(226, 35, 26), (247, 148, 29), (255, 222, 23), (57, 181, 74), (0, 174, 239)]
BG_TOP = (62, 49, 162)     # C64 blue (lighter)
BG_BOT = (30, 24, 92)      # C64 blue (darker)

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def logo(size=S, rounded=True):
    W = size * SS
    img = Image.new('RGBA', (W, W), (0, 0, 0, 0))
    # background gradient
    import numpy as np
    t = np.linspace(0, 1, W)[:, None]
    arr = np.zeros((W, W, 4), dtype=np.uint8)
    for i in range(3):
        arr[:, :, i] = (BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t).astype(np.uint8)
    arr[:, :, 3] = 255
    bg = Image.fromarray(arr, 'RGBA')
    mask = Image.new('L', (W, W), 0)
    md = ImageDraw.Draw(mask)
    r = int(W * 0.21) if rounded else 0
    md.rounded_rectangle([0, 0, W - 1, W - 1], radius=r, fill=255)
    img.paste(bg, (0, 0), mask)

    # rainbow stripes: diagonal band rising from lower left to upper right
    stripes = Image.new('RGBA', (W, W), (0, 0, 0, 0))
    sd = ImageDraw.Draw(stripes)
    sw = W * 0.085            # stripe width
    cx, cy = W * 0.50, W * 0.60
    ang = math.radians(-28)
    dx, dy = math.cos(ang), math.sin(ang)
    nx, ny = -dy, dx
    n = len(RAINBOW)
    for i, col in enumerate(RAINBOW):
        off = (i - (n - 1) / 2) * sw
        ox, oy = cx + nx * off, cy + ny * off
        L = W * 1.5
        p1 = (ox - dx * L, oy - dy * L)
        p2 = (ox + dx * L, oy + dy * L)
        hw = sw / 2 + 1
        poly = [(p1[0] + nx * hw, p1[1] + ny * hw), (p2[0] + nx * hw, p2[1] + ny * hw),
                (p2[0] - nx * hw, p2[1] - ny * hw), (p1[0] - nx * hw, p1[1] - ny * hw)]
        sd.polygon(poly, fill=col + (255,))
    img.paste(stripes, (0, 0), Image.composite(stripes, Image.new('RGBA', (W, W), (0,0,0,0)), mask).split()[3])

    # eighth note
    note = Image.new('L', (W, W), 0)
    nd = ImageDraw.Draw(note)
    hx, hy = W * 0.40, W * 0.70      # note head centre
    hrx, hry = W * 0.150, W * 0.110
    head = Image.new('L', (W, W), 0)
    hd = ImageDraw.Draw(head)
    hd.ellipse([hx - hrx, hy - hry, hx + hrx, hy + hry], fill=255)
    head = head.rotate(25, center=(hx, hy), resample=Image.BICUBIC)
    note.paste(255, (0, 0), head)
    stw = W * 0.055
    sx = hx + hrx * 0.86 - stw
    top = W * 0.17
    nd.rectangle([sx, top, sx + stw, hy - hry * 0.2], fill=255)
    # flag
    fx = sx + stw
    flag = [(sx, top)]
    steps = 40
    for k in range(steps + 1):
        t = k / steps
        flag.append((fx + W * 0.20 * math.sin(t * math.pi * 0.9) + W * 0.02 * t,
                     top + W * 0.06 + W * 0.33 * t))
    for k in range(steps, -1, -1):
        t = k / steps
        flag.append((fx + W * 0.14 * math.sin(t * math.pi * 0.85) - W * 0.005,
                     top + W * 0.16 + W * 0.20 * t))
    nd.polygon(flag, fill=255)

    # shadow + outline
    shadow = note.filter(ImageFilter.GaussianBlur(W * 0.02))
    sh = Image.new('RGBA', (W, W), (10, 6, 40, 0))
    sh.putalpha(shadow.point(lambda v: int(v * 0.75)))
    img.alpha_composite(sh, (int(W * 0.018), int(W * 0.022)))
    outline = note.filter(ImageFilter.GaussianBlur(W * 0.012)).point(lambda v: 255 if v > 8 else int(v * 30))
    ol = Image.new('RGBA', (W, W), (20, 16, 60, 255))
    ol.putalpha(outline)
    img.alpha_composite(ol)
    white = Image.new('RGBA', (W, W), (255, 255, 255, 255))
    white.putalpha(note)
    img.alpha_composite(white)
    # clip everything to rounded square
    a = img.split()[3]
    a = Image.composite(a, Image.new('L', (W, W), 0), mask)
    img.putalpha(a)
    return img.resize((size, size), Image.LANCZOS)

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'logo.png'
    logo(1024).save(out)
