# Generates the hires sprites of the C64 GrooveGoat logo (eighth note in
# front of the C64 rainbow) for the runnable PRG player (frontend.s).
# Sprite 0: note (white), sprite 1: note outline (black), 2-6: rainbow stripes.
import math, sys
W, H = 24, 21

def note(x, y):
    # note head: tilted ellipse
    cx, cy = 8.0, 16.0
    dx, dy = x + 0.5 - cx, y + 0.5 - cy
    a = math.radians(-25)
    u = dx * math.cos(a) - dy * math.sin(a)
    v = dx * math.sin(a) + dy * math.cos(a)
    if (u / 3.8) ** 2 + (v / 2.7) ** 2 <= 1.0:
        return True
    # stem
    if 10 <= x <= 11 and 3 <= y <= 15:
        return True
    # flag: thick curve from the stem top down to the right
    for t in range(0, 101):
        t /= 100.0
        px = 11.5 + 5.0 * math.sin(t * math.pi * 0.6)
        py = 3.5 + 7.5 * t
        if (x + 0.5 - px) ** 2 + (y + 0.5 - py) ** 2 <= (1.4 - 0.7 * t) ** 2:
            return True
    return False

def outline(x, y):
    for ox in (-1, 0, 1):
        for oy in (-1, 0, 1):
            if 0 <= x + ox < W and 0 <= y + oy < H and note(x + ox, y + oy):
                return True
    return False

def stripe(k, x, y):
    # 5 diagonal stripes (-30 degrees), each 4 px wide, centred
    a = math.radians(30)
    d = (x + 0.5 - W / 2) * math.sin(a) + (y + 0.5 - H / 2) * math.cos(a)
    lo = -10 + k * 4
    if not (lo <= d < lo + 4):
        return False
    # keep stripes inside a circle so the logo has a round shape
    return (x + 0.5 - W / 2) ** 2 + ((y + 0.5 - H / 2) * 1.0) ** 2 <= 11.5 ** 2

sprites = [lambda x, y: note(x, y), lambda x, y: outline(x, y) and not note(x, y)]
for k in range(5):
    sprites.append(lambda x, y, k=k: stripe(k, x, y) and not outline(x, y))

out = []
for spr in sprites:
    data = []
    for y in range(H):
        for b in range(3):
            v = 0
            for bit in range(8):
                if spr(b * 8 + bit, y):
                    v |= 0x80 >> bit
            data.append(v)
    data.append(0)
    out.append(data)

if len(sys.argv) > 1:
    from PIL import Image
    cols = [(255, 255, 255), (0, 0, 0), (136, 57, 50), (139, 84, 41), (191, 206, 114), (85, 160, 73), (120, 105, 196)]
    img = Image.new('RGB', (W * 2 * 8, H * 2 * 8), (64, 49, 141))
    px = img.load()
    for si in range(len(out) - 1, -1, -1):
        d = out[si]
        for y in range(H):
            for x in range(W):
                if d[y * 3 + x // 8] & (0x80 >> (x % 8)):
                    for yy in range(16):
                        for xx in range(16):
                            px[x * 16 + xx, y * 16 + yy] = cols[si]
    img.save(sys.argv[1])
else:
    for si, d in enumerate(out):
        print("        ;Sprite %d" % si)
        for i in range(0, 64, 16):
            print("                .BYTE (" + ",".join("$%02x" % v for v in d[i:i + 16]) + ")")
