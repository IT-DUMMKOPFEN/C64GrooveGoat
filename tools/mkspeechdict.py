#!/usr/bin/env python3
#
# Builds res/speech/speechdict.bin from the CMU Pronouncing Dictionary
# (https://github.com/cmusphinx/cmudict, BSD license, see
# res/speech/cmudict-LICENSE.txt):
#
#   tools/mkspeechdict.py path/to/cmudict.dict
#
# Format: words sorted, front coded. Per word: 1 byte (shared prefix length
# << 4 | suffix length), the suffix (lower case), 1 byte phoneme count, then
# one byte per phoneme = index * 3 + stress (index into PHONES below).
#
import os, re, sys

PHONES = "AA AE AH AO AW AY B CH D DH EH ER EY F G HH IH IY JH K L M N NG OW OY P R S SH T TH UH UW V W Y Z ZH".split()

def main():
    src = sys.argv[1]
    idx = {p: i for i, p in enumerate(PHONES)}
    entries = {}
    for line in open(src, encoding='latin-1'):
        line = line.split('#')[0].strip()
        if not line:
            continue
        w, *p = line.split()
        if '(' in w or not re.fullmatch(r"[a-z']+", w):
            continue
        codes = []
        for x in p:
            s = 0
            if x[-1].isdigit():
                s = int(x[-1]); x = x[:-1]
            codes.append(idx[x] * 3 + s)
        entries[w] = codes
    out = bytearray()
    prev = ''
    count = 0
    for w in sorted(entries):
        k = 0
        while k < len(prev) and k < len(w) and prev[k] == w[k] and k < 15:
            k += 1
        suf = w[k:]
        if len(suf) > 15 or len(entries[w]) > 255:
            continue
        out.append(k << 4 | len(suf))
        out += suf.encode()
        out.append(len(entries[w]))
        out += bytes(entries[w])
        prev = w
        count += 1
    dst = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'res', 'speech', 'speechdict.bin')
    open(dst, 'wb').write(out)
    print('%d words, %d bytes -> %s' % (count, len(out), dst))

main()
