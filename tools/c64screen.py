# Renders a C64 text screen dump (1000 screen codes + 1000 colors + border
# + background) to a PNG for checking the PRG player screen. Letters use a
# generic font; PETSCII block graphics used by the player are drawn exactly.
import sys
from PIL import Image, ImageDraw, ImageFont
PAL=[(0,0,0),(255,255,255),(136,57,50),(103,182,189),(139,63,150),(85,160,73),(64,49,141),(191,206,114),
     (139,84,41),(87,66,0),(184,105,98),(80,80,80),(120,120,120),(148,224,137),(120,105,196),(159,159,159)]
d=open(sys.argv[1],'rb').read()
scr=d[:1000]; col=d[1000:2000]; border=d[2000]; bg=d[2001]
S=3
img=Image.new('RGB',((320+64)*S,(200+64)*S),PAL[border])
dr=ImageDraw.Draw(img)
dr.rectangle([32*S,32*S,(32+320)*S-1,(32+200)*S-1],fill=PAL[bg])
try:
    font=ImageFont.truetype(sys.argv[3] if len(sys.argv)>3 else 'res/fonts/FiraCode-Bold.ttf', 9*S)
except Exception:
    font=ImageFont.load_default()
for i in range(1000):
    r,c=divmod(i,40); x=(32+c*8)*S; y=(32+r*8)*S
    sc=scr[i]; fg=PAL[col[i]&15]; rev=sc&0x80; code=sc&0x7f
    cellbg=fg if rev else PAL[bg]; cellfg=PAL[bg] if rev else fg
    if rev: dr.rectangle([x,y,x+8*S-1,y+8*S-1],fill=cellbg)
    def rect(x0,y0,x1,y1): dr.rectangle([x+x0*S,y+y0*S,x+x1*S-1,y+y1*S-1],fill=cellfg)
    if code==0x20: continue
    if code==0x61: rect(0,0,4,8)
    elif code==0x62: rect(0,4,8,8)
    elif code==0x40: rect(0,3,8,5)
    elif code==0x5d: rect(3,0,5,8)
    elif code in (0x70,0x6e,0x6d,0x7d):
        if code==0x70: rect(3,3,8,5); rect(3,3,5,8)
        if code==0x6e: rect(0,3,5,5); rect(3,3,5,8)
        if code==0x6d: rect(3,0,5,5); rect(3,3,8,5)
        if code==0x7d: rect(3,0,5,5); rect(0,3,5,5)
    else:
        if code==0: ch='@'
        elif code<27: ch=chr(64+code)
        elif 32<=code<64: ch=chr(code)
        else: ch='#'
        dr.text((x+1*S,y-1*S),ch,fill=cellfg,font=font)
# Sprites (hires, priority: sprite 0 in front)
if len(d) >= 2002 + 65536:
    m = d[2002:2002 + 65536]
    en = m[0xd015]
    for sn in range(7, -1, -1):
        if not en & (1 << sn): continue
        ptr = m[0x7f8 + sn] * 64
        sx = m[0xd000 + sn * 2] + (256 if m[0xd010] & (1 << sn) else 0)
        sy = m[0xd001 + sn * 2]
        ex = 2 if m[0xd01d] & (1 << sn) else 1
        ey = 2 if m[0xd017] & (1 << sn) else 1
        color = PAL[m[0xd027 + sn] & 15]
        for yy in range(21):
            for xx in range(24):
                if m[ptr + yy * 3 + xx // 8] & (0x80 >> (xx % 8)):
                    px0 = (32 + sx - 24 + xx * ex) * S
                    py0 = (32 + sy - 50 + yy * ey) * S
                    dr.rectangle([px0, py0, px0 + ex * S - 1, py0 + ey * S - 1], fill=color)
img.save(sys.argv[2])
