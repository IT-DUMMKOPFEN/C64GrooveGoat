#!/usr/bin/env python3
# Dump a song of SIDGen's enhanced SID calculation (as sgdumpsong.py) for comparing the C port
# args: sidgen-dir style seed seconds sids [voices model]; ENHMIDI=file keeps the MIDI score,
# TRACKS=1 dumps the voices of the importer instead of the song
import os
import shutil
import sys
sys.path.insert(0, sys.argv[1])
import sidgen.enhanced as E
from sidgen.i18n import en
from sidgen.song import ascii_text
st, seed, secs, sids = sys.argv[2], int(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5])
voices = int(sys.argv[6]) if len(sys.argv) > 6 else 3
model = int(sys.argv[7]) if len(sys.argv) > 7 else 6581
orig = E.write_midi


def keep(path, *a, **k):
    orig(path, *a, **k)
    if os.environ.get('ENHMIDI'):
        shutil.copy(path, os.environ['ENHMIDI'])


E.write_midi = keep
song, big, imp = E.compose_enhanced(st, seed, sids, voices, model, length=secs)
if os.environ.get('TRACKS'):
    for k, t in enumerate(imp.tracks):
        for r in sorted(t.ev):
            e = t.ev[r]
            if e[0] == 'n':
                lv = t.lv.get(r)
                print('t%d %d n %d %s %s%s%s' % (k, r, e[1], e[2], '-' if e[3] is None else e[3],
                                                 '' if lv is None else ' lv%.4f' % lv,
                                                 (' c%x:%02x' % e[4]) if len(e) > 4 and e[4] and not isinstance(e[4][1], tuple) else ''))
            else:
                print('t%d %d off' % (k, r))
    sys.exit(0)
song.build_tables()
for i, (ins, (w, p, f, s)) in enumerate(zip(song.instruments, song.ptrs)):
    print('ins %d %02x %02x %d %d %d %d %d %02x %02x %s' % (i + 1, ins.ad, ins.sr, w, p, f, s, ins.vibdelay & 0xff, ins.gt, ins.fw,
                                                          ascii_text(en(ins.name), 15)))
for k in ('wave', 'pulse', 'filt', 'speed'):
    print('tab %s %s' % (k, ' '.join('%02x:%02x' % (l, r) for l, r in song.tables[k])))
for si, (orders, restart) in enumerate(song.subtunes()):
    for ch, o in enumerate(orders):
        r = restart[ch] if isinstance(restart, (list, tuple)) else restart
        print('order %s%d %s%s' % ('' if not si else '%d.' % si, ch, ' '.join(str(p) for p in o), (' / %d' % r) if r else ''))
for pi, pat in enumerate(song.rpatterns):
    print('patt %d %s' % (pi, ' '.join('%02x%02x%x%02x' % (song._gtnote(n), i, cm, d) for n, i, cm, d in pat)))
