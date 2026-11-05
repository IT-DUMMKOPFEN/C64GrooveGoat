#!/usr/bin/env python3
# C64 GrooveGoat - drum + bass combination instruments for the instrument
# library: Kick+Bass, Snare+Bass, HiHat+Bass, Clap+Bass, Tom+Bass for the 6581
# and the 8580.
#
# Every combination plays the first frames of a drum (absolute wavetable
# notes, independent of the pattern note) and then a bass that follows the
# note - the classic way to get "bass drum + bass" (or snare + bass ...) out
# of ONE SID voice. Drums come from the library (Drums/<Kick ...>) and from a
# few new kit drums (808 / 909 / rock / chip), basses from Bass/. Every
# instrument is rendered with the GrooveGoat playroutine and reSID on its chip
# and kept only if the drum attack is audible and the bass sustains.
#
# usage: tools/mkcombos.py LIBRARY GROOVEGOAT [per-type count, default 36]
#        (writes LIBRARY/<6581|8580>/<Type>+Bass/*.ins)

import os, sys, struct, subprocess, tempfile, random, wave
import numpy as np

LIB, GG = sys.argv[1], sys.argv[2]
COUNT = int(sys.argv[3]) if len(sys.argv) > 3 else 36

TYPES = {           # folder, drum frames to keep, ADSR decay minimum
    "Kick": ("Kick", 6, 8),
    "Snare": ("Snare", 5, 8),
    "HiHat": ("HiHat", 2, 7),
    "Clap": ("Clap", 5, 8),
    "Tom": ("Tom", 6, 8),
}

# ---------------------------------------------------------------- .ins files
def readins(path):
    d = open(path, 'rb').read()
    if d[:4] != b'GTI5': return None
    ins = dict(ad=d[4], sr=d[5], ptr=list(d[6:10]), vibdelay=d[10], gatetimer=d[11], firstwave=d[12],
               name=d[13:29].rstrip(b'\0').decode('latin-1'))
    p = 29
    tabs = []
    for t in range(4):
        n = d[p]
        tabs.append((list(d[p + 1:p + 1 + n]), list(d[p + 1 + n:p + 1 + 2 * n])))
        p += 1 + 2 * n
    ins['tabs'] = tabs
    return ins

def writeins(path, ins):
    out = bytearray(b'GTI5')
    out += bytes([ins['ad'], ins['sr']] + [1 if len(ins['tabs'][t][0]) else 0 for t in range(4)] +
                 [ins['vibdelay'], ins['gatetimer'], ins['firstwave']])
    out += ins['name'].encode('latin-1')[:15].ljust(16, b'\0')
    for l, r in ins['tabs']:
        out.append(len(l)); out += bytes(l) + bytes(r)
    open(path, 'wb').write(out)

def program(ins, t):
    """rows of table t from the instrument pointer (file-relative, ptr = 1)
    until the stop / jump; jumps as row indexes of the program"""
    l, r = ins['tabs'][t]
    if not l: return []
    ptr = ins['ptr'][t] or 1
    rows = []
    for k in range(ptr - 1, len(l)):
        if l[k] == 0xff:
            tgt = r[k] - ptr if r[k] else -1          # file jumps are relative to ptr
            rows.append((0xff, tgt))
            break
        rows.append((l[k], r[k]))
    return rows

def isabs(r): return 0x81 <= r <= 0xdf

def drumpart(ins, maxframes):
    """the drum frames: absolute notes (and silent / delay rows) at the start"""
    out, frames = [], 0
    for l, r in program(ins, 0):
        if l == 0xff: break
        if 0x01 <= l <= 0x0f:                         # delay
            if out: out.append((l, 0x80)); frames += l + 1
            continue
        if (0xe0 <= l <= 0xef) or ((0x10 <= l < 0xe0) and (isabs(r) or (l & 0x80))):
            if (0x10 <= l < 0xe0) and not isabs(r): r = 0xd0     # noise with a relative note: bright
            out.append((l, r)); frames += 1
        else:
            break
        if frames >= maxframes: break
    return out if any(0x10 <= l < 0xe0 for l, r in out) else []

def basspart(ins):
    """the bass program (it must follow the note: relative notes)"""
    rows = program(ins, 0)
    tone = [x for x in rows if 0x10 <= x[0] < 0xe0 and not (x[0] & 0x80) and x[1] < 0x80]
    return rows if tone else []

def combine(drum, bass, kind, n):
    folder, frames, mindecay = TYPES[kind]
    dp = drumpart(drum, frames)
    bp = basspart(bass)
    if not dp or not bp: return None
    w = list(dp)
    base = len(w)
    for l, r in bp:
        if l == 0xff: w.append((0xff, (base + r + 1) if r >= 0 else 0))
        else: w.append((l, r))
    if w[-1][0] != 0xff: w.append((0xff, 0))
    # pulse: the bass program, or the drum's if the bass has none (a pulse
    # drum frame with pulse width 0 would be silent)
    tabs = [([x[0] for x in w], [x[1] for x in w]), ([], []), ([], []), ([], [])]
    for t in (1, 2):
        src = bass if program(bass, t) else (drum if t == 1 else None)
        if src is None: continue
        rows = program(src, t)
        if not rows: continue
        l = [x[0] for x in rows]
        r = [(x[1] + 1 if x[1] >= 0 else 0) if x[0] == 0xff else x[1] for x in rows]
        if l[-1] != 0xff: l.append(0xff); r.append(0)
        tabs[t] = (l, r)
    if program(bass, 3):
        x = program(bass, 3)[0]
        tabs[3] = ([x[0]], [x[1]])
    decay = max(bass['ad'] & 0x0f, mindecay)
    ins = dict(ad=decay, sr=bass['sr'], vibdelay=bass['vibdelay'] if tabs[3][0] else 0,
               gatetimer=(bass['gatetimer'] & 0x3f) | 0x00 if (bass['gatetimer'] & 0x3f) else 2,
               firstwave=0x09, name="%s+Bass %02d" % (kind if kind != "HiHat" else "Hat", n), tabs=tabs)
    return ins

# ---------------------------------------------------------------- new kit drums
def kitdrums(kind):
    """a few new drum attacks (like the beat editor kits)"""
    N = lambda n: 0x80 + n
    kits = {
        "Kick": {"808": [(0x11, N(52)), (0x11, N(46)), (0x11, N(41)), (0x11, N(37)), (0x11, N(34))],
                 "909": [(0x81, N(84)), (0x11, N(50)), (0x11, N(42)), (0x11, N(36)), (0x11, N(32))],
                 "rock": [(0x81, N(76)), (0x41, N(46)), (0x41, N(38)), (0x41, N(32))],
                 "chip": [(0x81, N(80)), (0x41, N(48)), (0x41, N(40)), (0x41, N(34))]},
        "Snare": {"808": [(0x81, N(90)), (0x11, N(44)), (0x81, N(82)), (0x81, N(79))],
                  "909": [(0x81, N(92)), (0x41, N(50)), (0x81, N(86)), (0x81, N(80))],
                  "rock": [(0x81, N(90)), (0x41, N(48)), (0x41, N(45)), (0x81, N(82)), (0x81, N(78))]},
        "HiHat": {"tick": [(0x81, N(95))], "hiss": [(0x81, N(95)), (0x81, N(92))]},
        "Clap": {"808": [(0x81, N(88)), (0xe1, 0x80), (0x81, N(88)), (0xe1, 0x80), (0x81, N(86))]},
        "Tom": {"hi": [(0x81, N(80)), (0x11, N(54)), (0x11, N(52)), (0x11, N(50)), (0x11, N(48))],
                "lo": [(0x81, N(80)), (0x11, N(42)), (0x11, N(40)), (0x11, N(38)), (0x11, N(36))]},
    }
    out = []
    for name, rows in kits.get(kind, {}).items():
        l = [x[0] for x in rows] + [0xff]; r = [x[1] for x in rows] + [0]
        out.append(("kit_" + name, dict(ad=0x09, sr=0, ptr=[1, 1, 0, 0], vibdelay=0, gatetimer=2, firstwave=0x09, name="kit",
                                         tabs=[(l, r), ([0x88, 0xff], [0x00, 0]), ([], []), ([], [])])))
    return out

# ---------------------------------------------------------------- render check
FIRST, REST, OFF = 0x60, 0xBD, 0xBE
def testsong(ins, path):
    notes = [24, 24, 19, 22]       # C-2 C-2 G-1 A#1
    rows = []
    for n in notes:
        rows.append([FIRST + n, 1, 0, 0])
        rows += [[REST, 0, 0, 0]] * 7
    out = bytearray(b'GTS5') + b'test'.ljust(32, b'\0') * 3
    out.append(1)
    for ch in range(3):
        out.append(2); out += bytes([0 if ch == 0 else 1, 0xff, 0])
    out.append(1)
    out += bytes([ins['ad'], ins['sr']] + [1 if ins['tabs'][t][0] else 0 for t in range(4)] +
                 [ins['vibdelay'], ins['gatetimer'], ins['firstwave']]) + b'x'.ljust(16, b'\0')
    for l, r in ins['tabs']:
        out.append(len(l)); out += bytes(l) + bytes(r)
    out.append(2)
    for p in (rows, [[REST, 0, 0, 0]] * 32):
        out.append(len(p) + 1)
        for row in p: out += bytes(row)
        out += bytes([0xff, 0, 0, 0])
    open(path, 'wb').write(out)

def check(ins, model, tmp):
    sng = os.path.join(tmp, 't.sng'); wav = os.path.join(tmp, 't.wav')
    testsong(ins, sng)
    subprocess.run([GG, '-E1' if model == '8580' else '-E0', '--render=' + wav, '--seconds=4', '--fade=0', sng],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        w = wave.open(wav); sr = w.getframerate(); ch = w.getnchannels()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
        if ch > 1: x = x.reshape(-1, ch).mean(1)
    except Exception:
        return None
    rowlen = 6 * 0.02 * 8                            # 8 rows at tempo 6
    hits, sus = [], []
    for k in range(1, 4):           # (the first note starts with the song)
        s = int((k * rowlen + 0.02) * sr)
        a = x[s:s + int(0.06 * sr)]
        b = x[s + int(0.15 * sr):s + int(0.7 * sr)]
        if len(a) == 0 or len(b) == 0: return None
        hits.append(np.sqrt((a ** 2).mean())); sus.append(np.sqrt((b ** 2).mean()))
    peak = np.abs(x).max()
    if peak > 32000 or min(hits) < 600 or min(sus) < 300: return None
    return float(np.mean(hits) + np.mean(sus))

# ---------------------------------------------------------------- main
def main():
    random.seed(64)
    tmp = tempfile.mkdtemp()
    for model in ('6581', '8580'):
        basses = []
        bdir = os.path.join(LIB, model, 'Bass')
        for f in sorted(os.listdir(bdir)):
            i = readins(os.path.join(bdir, f))
            if i and basspart(i): basses.append((f[:-4], i))
        print(model, len(basses), 'basses')
        for kind, (folder, frames, mind) in TYPES.items():
            drums = []
            ddir = os.path.join(LIB, model, 'Drums', folder)
            for f in sorted(os.listdir(ddir)):
                i = readins(os.path.join(ddir, f))
                if i and drumpart(i, frames): drums.append((f[:-4], i))
            random.shuffle(drums)
            drums = kitdrums(kind) + drums[:16]
            outdir = os.path.join(LIB, model, kind + '+Bass')
            os.makedirs(outdir, exist_ok=True)
            # pairs: every drum with different basses, spread
            cand = []
            bsel = random.sample(basses, min(24, len(basses)))
            for di, (dn, d) in enumerate(drums):
                for bi in range(4):
                    bn, b = bsel[(di * 5 + bi * 7) % len(bsel)]
                    cand.append((dn, d, bn, b))
            random.shuffle(cand)
            made, used = 0, set()
            for dn, d, bn, b in cand:
                if made >= COUNT: break
                if (dn, bn) in used: continue
                ins = combine(d, b, kind, made + 1)
                if not ins: continue
                if check(ins, model, tmp) is None: continue
                used.add((dn, bn))
                made += 1
                fn = "%s+bass_%s__%s.ins" % (kind.lower(), dn.replace('kick_', '').replace('snare_', '').replace('hat_', ''), bn.replace('bass_', ''))
                writeins(os.path.join(outdir, fn[:80]), ins)
            print("  %-6s+Bass: %d instruments (%d tried)" % (kind, made, len(cand)))

main()
