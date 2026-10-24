#!/usr/bin/env python3
# impair_corpus.py -- make an impaired copy of a SigID corpus
# Copyright (C) 2026
# This file is part of fldigi. GPL-3.0-or-later.
"""Run impair.py over every clip in a corpus folder.

Writes the impaired clips and a new manifest.jsonl into OUT. Each entry
keeps the clean entry's fields, with carrier_hz moved by --offset, plus the
impairment record from impair.py under "impair". --tilt then runs each
clip through a gentle low-pass, [t, 1-2t, t], as a receiver's sloping
passband would: with t = 0.15 that is about 3 dB down at fs/4 and 8 dB
near fs/2. It shapes the noise and the signal alike. The seed for each clip
is --seed plus its line number, so a set can be rebuilt exactly.

    impair_corpus.py ~/src/sigid-corpus/clean ~/src/sigid-corpus/awgn3 --snr 3
"""

import argparse
import array
import json
import os
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from impair import impair_file  # noqa: E402


def tilt(path, t):
    """Filter a 16-bit mono WAV in place with [t, 1-2t, t]."""
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
        x = array.array("h", w.readframes(w.getnframes()))
    if sys.byteorder != "little":
        x.byteswap()
    n = len(x)
    y = [t * x[max(i - 1, 0)] + (1 - 2 * t) * x[i] + t * x[min(i + 1, n - 1)] for i in range(n)]
    out = array.array("h", (int(round(v)) for v in y))
    if sys.byteorder != "little":
        out.byteswap()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(out.tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--snr", type=float)
    ap.add_argument("--noise-bw", type=float, default=2500.0)
    ap.add_argument("--channel", default="awgn",
                    choices=["awgn", "good", "moderate", "poor", "flutter"])
    ap.add_argument("--offset", type=float, default=0.0)
    ap.add_argument("--drift", type=float, default=0.0)
    ap.add_argument("--seed", type=int, default=1000)
    ap.add_argument("--tilt", type=float, default=0.0,
                    help="receiver slope, 0 to 0.25 (0.15 is typical)")
    args = ap.parse_args()

    src = os.path.expanduser(args.src)
    out = os.path.expanduser(args.out)
    os.makedirs(out, exist_ok=True)
    entries = [json.loads(line) for line in open(os.path.join(src, "manifest.jsonl"))]
    with open(os.path.join(out, "manifest.jsonl"), "w") as man:
        for n, e in enumerate(entries):
            rec = impair_file(os.path.join(src, e["file"]), os.path.join(out, e["file"]),
                              channel=args.channel, offset=args.offset, drift=args.drift,
                              snr=args.snr, noise_bw=args.noise_bw, seed=args.seed + n)
            if args.tilt:
                tilt(os.path.join(out, e["file"]), args.tilt)
                rec["tilt"] = args.tilt
            rec["input"] = e["file"]
            rec["output"] = e["file"]
            e = dict(e, carrier_hz=e["carrier_hz"] + args.offset, snr_db=args.snr,
                     channel=args.channel, impair=rec)
            man.write(json.dumps(e) + "\n")
            print("%-14s done" % e["mode"], flush=True)
    # A run once left an empty manifest beside complete clips; never let
    # that pass silently.
    with open(os.path.join(out, "manifest.jsonl")) as man:
        written = sum(1 for _ in man)
    if written != len(entries):
        sys.exit("%s: manifest has %d of %d entries" % (out, written, len(entries)))


if __name__ == "__main__":
    main()
