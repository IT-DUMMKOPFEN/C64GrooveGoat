#!/usr/bin/env python3
# Dump a SIDGen song after the build (patterns, orderlists, instruments, tables) for comparing the C port
import sys
sys.path.insert(0, sys.argv[1])
from sidgen.composer import Composer
from sidgen.i18n import en
from sidgen.song import ascii_text
st, seed, secs, sids = sys.argv[2], int(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5])
subtunes = len(sys.argv) > 6 and sys.argv[6] == '1'
voices = int(sys.argv[7]) if len(sys.argv) > 7 else 3
model = int(sys.argv[8]) if len(sys.argv) > 8 else 6581
from sidgen.app import ggt_fits
from sidgen.song import SongError


def build(variants=True, compact=False):
    common = dict(sids=sids, voices=voices, model=model)
    c = Composer(st, seconds=secs, seed=seed, variants=variants, compact=compact, **common)
    c.compose_tracks()
    extra = []
    if subtunes:
        fixed = dict(c.instrument_choice(), key=c.key, mode=c.mode, tempo=c.tempo, sub=c.sub_index)
        game = Composer(st, seconds=secs or c.target, seed=c.seed + 1, title=c.title, fixed=fixed, variants=variants,
                        compact=compact, **common)
        game.compose_tracks()
        over = Composer(st, seed=c.seed + 2, title=c.title, fixed=fixed, jingle=True, compact=compact, **common)
        over.compose_tracks()
        extra = [game, over]
    return c, c.build_song(extra)


c, song = build()
fits = sids < 4 or ggt_fits(song)
for variants, compact in ((False, False), (False, True)):
    if fits:
        break
    c2, s2 = build(variants, compact)
    if ggt_fits(s2):
        c, song, fits = c2, s2, True
if not fits:
    print('nofit')
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
