#!/bin/bash
# Compares the register writes of the multi-SID playroutine with the original
# single SID GoatTracker playroutine: every SID of a song whose orderlists are
# duplicated to all SIDs must produce exactly the same register values.
GG=${GG:-build/linux/groovegoat}
T=${T:-build/tools/sidtrace}
SONGS=${SONGS:-../gt2src/examples/*.sng}
FRAMES=${FRAMES:-2500}
W=$(mktemp -d)
fail=0; pass=0
# Known differences that are not playroutine errors:
#  wavecmdtest - the single SID reference player only runs the used voice
#  everlasting - reads beyond the trimmed frequency table (layout dependent)
SKIP=${SKIP:-"wavecmdtest everlasting"}
for song in $SONGS; do
  name=$(basename $song .sng)
  case " $SKIP " in *" $name "*) echo "SKIP $name (known reference difference)"; continue;; esac
  for ad in 0f00 f000; do
  for opt in 0 8 24 40 64 512 256; do
    $GG -A$ad --options=$opt --export=$W/ref.sid $song > /dev/null 2>&1 || { echo "SKIP $name (ref export failed)"; continue; }
    nsongs=$(od -An -tu1 -j15 -N1 $W/ref.sid | tr -d ' ')
    [ $nsongs -gt 3 ] && nsongs=3
    for sids in 2 4 8; do
      for layout in direct ghost; do
        addr=""
        shift=0
        if [ $layout = ghost ]; then
          case $sids in
            2) addr="d400,d500";;
            4) addr="d400,d420,d480,d4a0";;
            8) addr="d400,d420,d480,d4a0,d500,d520,d580,d5a0";;
          esac
          shift=1
          fmt=prgmusic
        else
          fmt=sid
        fi
        if [ -n "$addr" ]; then
          GG_SIDADDR=$addr GG_DUPSIDS=1 $GG -A$ad --options=$opt --format=$fmt --export=$W/m.bin --sids=$sids $song > $W/exp.txt 2>&1
        else
          GG_DUPSIDS=1 $GG -A$ad --options=$opt --format=$fmt --export=$W/m.bin --sids=$sids $song > $W/exp.txt 2>&1
        fi
        if [ $? -ne 0 ]; then
          if grep -q "TOO MANY SUBTUNES" $W/exp.txt; then continue; fi
          echo "FAIL $name ad=$ad opt=$opt sids=$sids $layout: export failed: $(cat $W/exp.txt)"; fail=$((fail+1)); continue
        fi
        for ((st=0; st<nsongs; st++)); do
          refshift=$(( (opt & 512) ? 1 : 0 ))
          mshift=$shift
          [ $((opt & 512)) -ne 0 ] && mshift=1
          $T $W/ref.sid $((FRAMES+1)) $st 2>/dev/null | awk -v sh=$refshift 'NR>sh {print $2}' | head -$FRAMES > $W/r.txt
          if [ $layout = ghost ]; then
            $T $W/m.bin $((FRAMES+1)) $st --prg 1000 ${addr//,/ } 2>/dev/null > $W/m.txt
          else
            $T $W/m.bin $((FRAMES+1)) $st 2>/dev/null > $W/m.txt
          fi
          ok=1
          for ((s=0; s<sids; s++)); do
            awk -v c=$((s+2)) -v sh=$mshift 'NR>sh {print $c}' $W/m.txt | head -$FRAMES > $W/s.txt
            if ! cmp -s $W/r.txt $W/s.txt; then
              ok=0
              echo "FAIL $name ad=$ad opt=$opt sids=$sids $layout subtune=$st sid=$((s+1)): $(diff $W/r.txt $W/s.txt | head -3 | tr '\n' ' ')"
              break
            fi
          done
          if [ $ok = 1 ]; then pass=$((pass+1)); else fail=$((fail+1)); fi
        done
      done
    done
  done
  done
done
rm -rf $W
echo "passed: $pass failed: $fail"
[ $fail = 0 ]
