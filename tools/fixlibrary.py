#!/usr/bin/env python3
#
# C64 GrooveGoat - repairs instruments of the SIDGen library that are silent
# with the GoatTracker playroutine.
#
# About 380 instruments (mostly drums, percussion and arpeggios) have
# Attack/Decay $x0/$x1 and Sustain 0: the envelope has already decayed to zero
# during the first frame of the note, in which "1stFrame Wave 09" (gate + test
# bit) keeps the oscillator silent. Their release nibble holds the intended
# decay time (e.g. Harpsichord AD 00 SR 08, Hat Pulse AD 00 SR 04), so the
# decay is set to the release value.
#
# Very short percussion (decay $2/$3, sustain 0) loses most of its sound in
# that silent first frame and is barely audible; its decay is made two steps
# longer. Nothing else is changed.
#
#   tools/fixlibrary.py FOLDER      repairs all .ins files below FOLDER in place
#
import os, sys

def fix(path):
    d = bytearray(open(path, 'rb').read())
    if d[:3] != b'GTI' or len(d) < 13:
        return False
    ad, sr = d[4], d[5]
    decay, sustain, release = ad & 15, sr >> 4, sr & 15
    changed = False
    if decay <= 1 and sustain == 0 and release > decay:
        d[4] = (ad & 0xf0) | release
        changed = True
    elif 2 <= decay <= 3 and sustain == 0 and d[12] == 0x09:
        d[4] = (ad & 0xf0) | (decay + 2)
        changed = True
    if changed:
        open(path, 'wb').write(d)
    return changed

def main():
    n = total = 0
    for root, dirs, files in os.walk(sys.argv[1]):
        for f in files:
            if f.lower().endswith('.ins'):
                total += 1
                if fix(os.path.join(root, f)):
                    n += 1
    print('repaired %d of %d instruments' % (n, total))

if __name__ == '__main__':
    main()
