#!/usr/bin/env python3
# Dump a song made by the SIDGen MIDI importer (as sgdumpsong.py) for comparing the C port
# args: sidgen-dir midi-file sids [voices model variant accomp harmony]
import sys
sys.path.insert(0, sys.argv[1])
from sidgen.midiimport import import_midi
from sidgen.app import ggt_fits
from sidgen.i18n import en
from sidgen.song import ascii_text
path, sids = sys.argv[2], int(sys.argv[3])
voices = int(sys.argv[4]) if len(sys.argv) > 4 else 3
model = int(sys.argv[5]) if len(sys.argv) > 5 else 6581
variant = int(sys.argv[6]) if len(sys.argv) > 6 else 0
accomp = sys.argv[7] if len(sys.argv) > 7 else 'auto'
harmony = sys.argv[8] if len(sys.argv) > 8 else 'normal'
data = open(path, 'rb').read()
opts = dict(variant=variant, accomp=accomp, harmony=harmony)
song = None
for s2 in [sids] + [n for n in (4, 2, 1) if n < sids and sids > 2]:
    song, imp = import_midi(data, voices=voices, filename=path, sids=s2, model=model, opts=dict(opts))
    if s2 < 4 or ggt_fits(song):
        break
import os
if os.environ.get('TRACKS'):
    for k, t in enumerate(imp.tracks):
        for r in sorted(t.ev):
            e = t.ev[r]
            if e[0] == 'n':
                lv = t.lv.get(r)
                print('t%d %d n %d %s %s%s%s' % (k, r, e[1], e[2], '-' if e[3] is None else e[3],
                                                 '' if lv is None else ' lv%.4f' % lv,
                                                 (' c%x:%02x' % e[4]) if len(e) > 4 and e[4] else ''))
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
