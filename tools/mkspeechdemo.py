#!/usr/bin/env python3
# "Together We Hunt" - music part of the speech demo examples/together_we_hunt.sng
# (1 SID, voices 1+2 music, voice 3 drums + speech). Writes a GTS5 .sng; the
# speech is added afterwards with the command line speech generator:
#
#   python3 tools/mkspeechdemo.py music.sng          (prints the speech patterns a b c)
#   GG=build/linux/groovegoat; T="TOGETHER WE HUNT"; E="GG_SPEECHCHN=2 GG_SPEECHROW=5 GG_SPEECHSETTEMPO=1"
#   env $E GG_SPEECHPATT=a GG_SPEECHMELODY=1 $GG -E1 --speech="$T" --save=t1.sng music.sng
#   env $E GG_SPEECHPATT=b GG_SPEECHMELODY=3 GG_SPEECHNOTE=38 $GG -E1 --speech="$T" --save=t2.sng t1.sng
#   env $E GG_SPEECHPATT=c GG_SPEECHMELODY=1 GG_SPEECHVOICE=1 $GG -E1 --speech="$T" \
#       --save=together_we_hunt.sng t2.sng
import struct, sys

FIRSTNOTE, REST, KEYOFF, ENDPATT = 0x60, 0xBD, 0xBE, 0xFF
NN = {'C-':0,'C#':1,'D-':2,'D#':3,'E-':4,'F-':5,'F#':6,'G-':7,'G#':8,'A-':9,'A#':10,'B-':11}
def N(s): return NN[s[:2]] + 12 * int(s[2])

# ---------------------------------------------------------------- tables
tabs = [[[], []] for _ in range(4)]          # wave, pulse, filter, speed
def prog(t, entries):
    start = len(tabs[t][0]) + 1
    for l, r in entries:
        if l == 'JMP':                       # jump to entry r of this program
            l, r = 0xFF, start + r
        tabs[t][0].append(l); tabs[t][1].append(r)
    return start

# wave programs
w_kick = prog(0, [(0x81, 0xC4), (0x11, 0xA8), (0x11, 0xA0), (0x11, 0x98), (0x41, 0x00), (0xFF, 0x00)])
w_bass = prog(0, [(0x41, 0x00), (0xFF, 0x00)])
w_hat = prog(0, [(0x81, 0xDC), (0x80, 0xDC), (0xFF, 0x00)])
w_snare = prog(0, [(0x41, 0xA4), (0x81, 0xD0), (0x81, 0xC8), (0x81, 0xCC), (0xFF, 0x00)])
w_lead = prog(0, [(0x41, 0x0C), (0x41, 0x00), (0xFF, 0x00)])
w_arpm = prog(0, [(0x21, 0x00), (0x21, 0x03), (0x21, 0x07), (0x21, 0x0C), ('JMP', 0)])
w_arpM = prog(0, [(0x21, 0x00), (0x21, 0x04), (0x21, 0x07), (0x21, 0x0C), ('JMP', 0)])
w_pad = prog(0, [(0x21, 0x00), (0xFF, 0x00)])
# pulse programs
p_bass = prog(1, [(0x82, 0x00), (0x40, 0x10), (0x40, 0xF0), ('JMP', 1)])
p_lead = prog(1, [(0x84, 0x00), (0x30, 0x18), (0x30, 0xE8), ('JMP', 1)])
p_snare = prog(1, [(0x88, 0x00), (0xFF, 0x00)])
# filter "off" for the drums on voice 3: no voice routed to the filter (the
# speech instruments on the same voice route it to themselves)
f_off = prog(2, [(0x80, 0x00), (0xFF, 0x00)])
# speed table: vibrato for the lead (speed, depth)
s_vib = prog(3, [(0x04, 0x30)])

# ---------------------------------------------------------------- instruments
#        name         AD    SR    wave     pulse   filter  speed vibdelay gatetimer firstwave
INS = [("Kick bass",   0x09, 0xA8, w_kick,  p_bass, 0,      0,     0,       0x02,     0x09),
       ("Bass",        0x08, 0xA8, w_bass,  p_bass, 0,      0,     0,       0x02,     0x09),
       ("Hat",         0x02, 0x00, w_hat,   0,      f_off,  0,     0,       0x02,     0x81),
       ("Snare",       0x08, 0x00, w_snare, p_snare, f_off, 0,     0,       0x02,     0x41),
       ("Lead",        0x2A, 0x9A, w_lead,  p_lead, 0,      s_vib, 0x0C,    0x02,     0x09),
       ("Arp minor",   0x05, 0x69, w_arpm,  0,      0,      0,     0,       0x02,     0x09),
       ("Arp major",   0x05, 0x69, w_arpM,  0,      0,      0,     0,       0x02,     0x09),
       ("Dark pad",    0x8B, 0x7C, w_pad,   0,      0,      0,     0,       0x02,     0x21)]
I = {n[0]: i + 1 for i, n in enumerate(INS)}

# ---------------------------------------------------------------- patterns
pats = []
def pattern(rows):
    # written on a 16th grid; speed 03 = 60 ms per row, so a 16th is 2 rows (125 BPM)
    full = []
    for r in rows:
        full.append(r)
        full.append([REST, 0, 0, 0])
    pats.append(full)
    return len(pats) - 1
def empty(n=32): return [[REST, 0, 0, 0] for _ in range(n)]

def bass(chords, tempo=False, duck=False):
    r = empty()
    figure = [0, 0, 12, 0, 0, 12, 0, 10, 0, 0, 12, 0, 7, 0, 12, 5]   # 16ths, relative to the root
    for bar, root in enumerate(chords):
        for i, f in enumerate(figure):
            row = bar * 16 + i
            ins = I["Kick bass"] if i % 4 == 0 else I["Bass"]
            # while the voice speaks (first bar): only the kicks
            if duck and bar == 0 and i % 4: continue
            r[row] = [FIRSTNOTE + N(root) + f, ins, 0, 0]
    if tempo: r[0][2:4] = [0x0F, 0x03]
    return r

def drums(snare=True, fill=False):
    r = empty()
    for row in range(2, 32, 4): r[row] = [FIRSTNOTE + N("C-5"), I["Hat"], 0, 0]
    if snare:
        for row in (4, 12, 20, 28): r[row] = [FIRSTNOTE + N("C-5"), I["Snare"], 0, 0]
    if fill:
        for row in (26, 29, 30, 31): r[row] = [FIRSTNOTE + N("C-5"), I["Snare"], 0, 0]
    return r

def arps(chords):
    r = empty()
    for bar, (root, kind) in enumerate(chords):
        ins = I["Arp minor"] if kind == 'm' else I["Arp major"]
        for i in (0, 3, 6, 10, 12):
            r[bar * 16 + i] = [FIRSTNOTE + N(root), ins, 0, 0]
        r[bar * 16 + 14] = [KEYOFF, 0, 0, 0]
    return r

def melody(notes):
    r = empty()
    for row, n in notes:
        r[row] = [KEYOFF, 0, 0, 0] if n == '===' else [FIRSTNOTE + N(n), I["Lead"], 0, 0]
    return r

def pad(notes):
    r = empty()
    for row, n in notes:
        r[row] = [KEYOFF, 0, 0, 0] if n == '===' else [FIRSTNOTE + N(n), I["Dark pad"], 0, 0]
    return r

# voice 1
b_intro = pattern(bass(["D-1", "A#0"], tempo=True, duck=True))
b_a = pattern(bass(["D-1", "A#0"]))
b_b = pattern(bass(["G-1", "A-1"]))
# voice 2
v_pad = pattern(pad([(0, "D-3"), (16, "A#2"), (30, "===")]))
v_arpa = pattern(arps([("D-3", 'm'), ("A#2", 'M')]))
v_arpb = pattern(arps([("G-3", 'm'), ("A-2", 'M')]))
v_lead1 = pattern(melody([(0, "D-4"), (6, "F-4"), (8, "A-4"), (14, "==="), (16, "A#4"), (20, "A-4"), (24, "F-4"), (28, "D-4"), (31, "===")]))
v_lead2 = pattern(melody([(0, "G-4"), (6, "A#4"), (8, "D-5"), (14, "==="), (16, "C#5"), (22, "E-5"), (24, "A-4"), (30, "===")]))
def duck(rows):
    r = [list(x) for x in rows]
    for row in range(16): r[row] = [REST, 0, 0, 0]
    r[0] = [KEYOFF, 0, 0, 0]
    return r
b_ad = pattern(bass(["D-1", "A#0"], duck=True))
b_bd = pattern(bass(["G-1", "A-1"], duck=True))
v_arpad = pattern(duck(arps([("D-3", 'm'), ("A#2", 'M')])))
v_arpbd = pattern(duck(arps([("G-3", 'm'), ("A-2", 'M')])))
v_lead1d = pattern(duck(melody([(0, "D-4"), (16, "A#4"), (20, "A-4"), (24, "F-4"), (28, "D-4"), (31, "===")])))
v_lead2d = pattern(duck(melody([(16, "C#5"), (22, "E-5"), (24, "A-4"), (30, "===")])))
# voice 3: drums and speech (the speech patterns stay empty here)
d_beat = pattern(drums())
d_fill = pattern(drums(fill=True))
d_hats = pattern(drums(snare=False))
# speech in the first bar (rows 08-1C after the expansion), drums in the second
def speechbar():
    r = drums(fill=True)
    for row in range(16): r[row] = [REST, 0, 0, 0]
    return r
s1 = pattern(speechbar())
s2 = pattern(speechbar())
s3 = pattern(speechbar())

order = [
    # voice 1      voice 2    voice 3
    (b_intro,      v_pad,     s1),
    (b_a,          v_pad,     d_hats),
    (b_a,          v_arpa,    d_beat),
    (b_bd,         v_arpbd,   s2),
    (b_a,          v_arpa,    d_beat),
    (b_b,          v_arpb,    d_fill),
    (b_a,          v_lead1,   d_beat),
    (b_bd,         v_lead2d,  s3),
    (b_a,          v_lead1,   d_beat),
    (b_b,          v_lead2,   d_fill),
    (b_ad,         v_arpad,   s1),
    (b_b,          v_arpb,    d_beat),
    (b_ad,         v_lead1d,  s2),
    (b_b,          v_lead2,   d_fill),
    (b_ad,         v_pad,     s3),
    (b_b,          v_pad,     d_fill),
]
LOOP = 2

# ---------------------------------------------------------------- write
def s32(t): return t.encode('ascii')[:32].ljust(32, b'\0')
out = bytearray(b'GTS5')
out += s32("Together We Hunt") + s32("IT-DUMMKOPFEN") + s32("2026 C64 GrooveGoat demo")
out.append(1)
for ch in range(3):
    seq = [o[ch] for o in order]
    out.append(len(seq) + 1)
    out += bytes(seq) + bytes([0xFF, LOOP])
out.append(len(INS))
for name, ad, sr, w, p, f, s, vd, gt, fw in INS:
    out += bytes([ad, sr, w, p, f, s, vd, gt, fw]) + name.encode()[:16].ljust(16, b'\0')
for t in range(4):
    l, r = tabs[t]
    out.append(len(l)); out += bytes(l) + bytes(r)
out.append(len(pats))
for p in pats:
    out.append(len(p) + 1)
    for row in p: out += bytes(row)
    out += bytes([ENDPATT, 0, 0, 0])
open(sys.argv[1], 'wb').write(out)
print("patterns: speech", s1, s2, s3, " instruments", len(INS))
