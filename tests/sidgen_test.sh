#!/bin/bash
# Compares the SIDGen port with SIDGen itself (https://github.com/IT-DUMMKOPFEN/sidgen):
# new songs (composer), the MIDI importer and the enhanced SID calculation must give the same
# song (instruments, tables, orderlists and patterns) as SIDGen.
#
#   tests/sidgen_test.sh SIDGEN_DIR [GROOVEGOAT_BINARY]
#
# SIDGEN_DIR is a checkout of SIDGen (Python 3.12 or newer: its sum() of floats is
# compensated, the port does the same). The MIDI files are written by SIDGen's MIDI exporter.
SG=${1:?usage: tests/sidgen_test.sh SIDGEN_DIR [BINARY]}
GG=${2:-build/linux/groovegoat}
T=$(dirname "$0")/../tools
W=$(mktemp -d)
pass=0
fail=0

check() {
  if cmp -s $W/p.txt $W/c.txt; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL $*: $(diff $W/p.txt $W/c.txt | head -2 | tail -1 | cut -c1-120)"; fi
}

# composer: styles x SIDs
for st in techno rock jazz klassik c64game reggae; do
  for sids in 1 2 4 8; do
    python3 $T/sgdumpsong.py $SG $st 7 60 $sids > $W/p.txt 2>/dev/null
    GG_SGDUMPSONG=$W/c.txt $GG --compose=$st,7,60,$sids --save=$W/o.sng > /dev/null 2>&1
    check compose $st $sids
  done
done
# composer: subtunes, voice 3 free, 8580
python3 $T/sgdumpsong.py $SG pop 9 60 2 1 2 8580 > $W/p.txt 2>/dev/null
GG_SGDUMPSONG=$W/c.txt $GG --compose=pop,9,60,2,8580,1,2 --save=$W/o.sng > /dev/null 2>&1
check compose pop subtunes
# enhanced
for st in house metal ambient; do
  for sids in 1 2; do
    python3 $T/sgdumpenh.py $SG $st 5 60 $sids > $W/p.txt 2>/dev/null
    GG_SGDUMPSONG=$W/c.txt $GG --compose=$st,5,60,$sids,6581,0,3,1 --save=$W/o.sng > /dev/null 2>&1
    check enhanced $st $sids
  done
done
# MIDI import: scores written by SIDGen's exporter
python3 - "$SG" "$W" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1])
from sidgen.composer import Composer
from sidgen.enhanced import Exporter
from sidgen.midi import write_midi
for st, seed in (('trance', 3), ('blues', 4), ('film', 6)):
    c = Composer(st, seconds=45, seed=seed, voices=3, sids=8)
    c.compose_tracks()
    tracks, hints = Exporter(c).score()
    write_midi('%s/%s.mid' % (sys.argv[2], st), tracks, ppq=480, bpm=c.bpm)
EOF
for st in trance blues film; do
  for opt in "1" "2" "4" "8" "1 3 6581 1" "2 3 6581 0 broken careful" "1 2 8580 0 pads loose"; do
    set -- $opt
    python3 $T/sgdumpmidi.py $SG $W/$st.mid $opt > $W/p.txt 2>/dev/null
    GG_SGDUMPSONG=$W/c.txt $GG --midi=$W/$st.mid,$1,${2:-3},${3:-6581},${4:-0},${5:-auto},${6:-normal} --save=$W/o.sng > /dev/null 2>&1
    check midi $st $opt
  done
done
rm -rf $W
echo "passed: $pass failed: $fail"
[ $fail = 0 ]
