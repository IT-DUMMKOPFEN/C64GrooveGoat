#!/usr/bin/env python3
# Dump a SIDGen composition (for comparing the C port): sgdump.py SIDGEN STYLE SEED SECONDS SIDS
import sys
sys.path.insert(0, sys.argv[1])
from sidgen.composer import Composer, base
st, seed, secs, sids = sys.argv[2], int(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5])
c = Composer(st, seconds=secs, seed=seed, sids=sids, expressive=(len(sys.argv) < 7 or sys.argv[6] != "0"), model=int(sys.argv[7]) if len(sys.argv) > 7 else 6581)
c.compose_tracks()
print('title', c.title)
print('sub', c.st.get('sub_name') or '')
print('key', c.key, c.mode)
print('tempo', c.tempo[0], c.tempo[1])
print('layout', c.layout)
print("sections", ' '.join('%s:%d' % s for s in c.sections))
print('ins', c.i_bass, '|', c.i_kickbass, '|', c.i_lead, '|', c.i_lead_b, '|', c.t_arp, '|', c.t_stab, '|', c.t_pad, '|',
      c.i_counter, '|', c.i_broken)
print('kit', ' | '.join('%s=%s' % (k, c.kit[k]) for k in sorted(c.kit)))
print('filter', c.filter_role, c.filter_tpl)
print('bass', ' '.join('%s=%s' % (k, c.bass_pat[k]) for k in sorted(c.bass_pat)))
for k in ('intro', 'A', 'B', 'C', 'outro', 'A2', 'B2', 'C2'):
    if k in c.progs:
        print('prog', k, c.chord_bars.get(k), ' '.join('%d%s' % ch for ch in c.progs[k]))
for i, t in enumerate(c.tracks):
    for r in sorted(t.ev):
        e = t.ev[r]
        if e[0] == 'n':
            print('t%d %d n %d %s %s' % (i, r, e[1], e[2], e[3] if e[3] is not None else '-'))
        else:
            print('t%d %d off' % (i, r))
