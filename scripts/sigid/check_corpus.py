#!/usr/bin/env python3
# check_corpus.py -- confirm each corpus clip holds the signal its label says
# Copyright (C) 2026
# This file is part of fldigi. GPL-3.0-or-later.
"""Measure where each clip's energy sits and compare it with the manifest.

For every manifest entry: average a 0.25 s Hann-windowed spectrum (4 Hz
resolution, 8 Hz steps) over the clip, then report the span that holds 99%
of the power and its center. A clip is flagged when the center is off from
carrier_hz by more than max(40 Hz, 20% of the bandwidth), or the span is
more than 1.5 x the bandwidth + 120 Hz, or less than half the bandwidth
less 40 Hz. On-off keyed modes splatter far past their nominal bandwidth,
so for those only the spectral peak is checked: it must sit within
max(10 Hz, 10% of the bandwidth) of carrier_hz. That catches a clip
recorded in the wrong mode or at the wrong frequency. Standard library only, so it is slow:
about a second per clip.

    check_corpus.py ~/src/sigid-corpus/clean [FILE.wav...]

Writes measured_lo_hz, measured_hi_hz and ok into check.jsonl beside the
manifest, and exits 1 if any clip is flagged.
"""

import array
import json
import math
import os
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_corpus import BANDWIDTH  # noqa: E402


# One keyed carrier: the peak is the carrier, the span is keying splatter.
OOK = {"CW", "FELDHELL", "SLOWHELL", "HELLX5", "HELLX9", "SCAMPOOK", "SCOOKSLW"}


def spectrum_span(path, step=8, frac=0.99):
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
        x = array.array("h", w.readframes(w.getnframes()))
    if sys.byteorder != "little":
        x.byteswap()
    n = rate // 4
    win = [(0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1))) / 32768.0 for i in range(n)]
    freqs = list(range(100, rate // 2 - 100, step))
    coefs = [2 * math.cos(2 * math.pi * f / rate) for f in freqs]
    acc = [0.0] * len(freqs)
    first = min(rate, len(x) // 4)
    hop = max(n, (len(x) - n - first) // 10)
    for s in range(first, len(x) - n, hop):
        seg = [a * b for a, b in zip(x[s:s + n], win)]
        for k, c in enumerate(coefs):
            s1 = s2 = 0.0
            for v in seg:
                s1, s2 = v + c * s1 - s2, s1
            acc[k] += s1 * s1 + s2 * s2 - c * s1 * s2
    total = sum(acc) or 1.0
    cum, lo, hi = 0.0, None, None
    edge = (1 - frac) / 2
    for f, p in zip(freqs, acc):
        cum += p
        if lo is None and cum >= edge * total:
            lo = f
        if hi is None and cum >= (1 - edge) * total:
            hi = f
    peak = freqs[max(range(len(acc)), key=acc.__getitem__)]
    return lo, hi, peak


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    folder = os.path.expanduser(sys.argv[1])
    only = set(sys.argv[2:])
    entries = [json.loads(line) for line in open(os.path.join(folder, "manifest.jsonl"))]
    bad = 0
    with open(os.path.join(folder, "check.jsonl"), "w") as out:
        for e in entries:
            if only and e["file"] not in only:
                continue
            lo, hi, peak = spectrum_span(os.path.join(folder, e["file"]))
            bw = float(BANDWIDTH.get(e["mode"], e.get("bandwidth_hz") or 0))
            center = (lo + hi) / 2
            if e["mode"] in OOK:
                ok = abs(peak - e["carrier_hz"]) <= max(10.0, 0.1 * bw)
            else:
                ok = (abs(center - e["carrier_hz"]) <= max(40.0, 0.2 * bw)
                    and hi - lo <= 1.5 * bw + 120
                    and hi - lo >= 0.5 * bw - 40)
            bad += not ok
            e.update(measured_lo_hz=lo, measured_hi_hz=hi, measured_peak_hz=peak, ok=ok)
            out.write(json.dumps(e) + "\n")
            out.flush()
            print("%-4s %-12s carrier %4d  measured %4d..%4d (center %4d, peak %4d)  bw %4d" % (
                "ok" if ok else "BAD", e["mode"], e["carrier_hz"], lo, hi, center, peak, bw),
                flush=True)
    print("%d clips flagged" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
