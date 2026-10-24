#!/usr/bin/env python3
# score_segments.py -- score SigID segmentation against a corpus manifest
# Copyright (C) 2026
# This file is part of fldigi. GPL-3.0-or-later.
"""Run sigid-scan --candidates over a corpus folder and score the result.

For each clip, the truth is the manifest's carrier_hz and the 99% power
span that check_corpus.py measured on the clean clip (clean/check.jsonl).
A candidate belongs to the signal when it overlaps the nominal band
(carrier +/- bandwidth/2, plus 20 Hz). Per clip:

  found      one or more candidates belong to the signal
  whole      exactly one does (not split into pieces)
  center_ok  |candidate center - carrier| <= 5 Hz, or 10% of a wide
             signal's bandwidth
  width_ok   candidate width within 15% (at least 16 Hz, two bins) of the
             measured span (not checked
             for on-off keyed modes, whose span is mostly keying splatter)
  extra      candidates that belong to nothing

    score_segments.py SCAN FOLDER [--clean CLEAN] [--show N]
"""

import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_corpus import BANDWIDTH  # noqa: E402
from check_corpus import OOK  # noqa: E402


def scan(binary, folder, files):
    out = subprocess.run([binary, "--candidates"] + [os.path.join(folder, f) for f in files],
                         capture_output=True, text=True, check=False).stdout
    final = {}
    for line in out.splitlines():
        r = json.loads(line)
        if "cand" in r:
            final.setdefault(os.path.basename(r["file"]), {})[r["cand"]] = r
    # Keep only each file's last report (the end of the clip).
    last = {}
    for f, cands in final.items():
        t = max(c["t"] for c in cands.values())
        last[f] = [c for c in cands.values() if c["t"] == t]
    return last


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("scan", help="path to sigid-scan")
    ap.add_argument("folder")
    ap.add_argument("--clean", help="clean corpus folder with check.jsonl "
                    "(default: FOLDER/../clean)")
    ap.add_argument("--show", type=int, default=12, help="worst clips to list")
    ap.add_argument("--json", help="write per-clip results here")
    args = ap.parse_args()
    folder = os.path.expanduser(args.folder)
    clean = os.path.expanduser(args.clean or os.path.join(folder, "..", "clean"))
    spans = {}
    for line in open(os.path.join(clean, "check.jsonl")):
        e = json.loads(line)
        spans[e["file"]] = (e["measured_lo_hz"], e["measured_hi_hz"])
    entries = [json.loads(line) for line in open(os.path.join(folder, "manifest.jsonl"))]
    if not entries:
        sys.exit("%s: manifest.jsonl is empty" % folder)
    found = scan(args.scan, folder, [e["file"] for e in entries])

    rows = []
    for e in entries:
        f, mode, carrier = e["file"], e["mode"], e["carrier_hz"]
        bw = BANDWIDTH.get(mode, e.get("bandwidth_hz") or 0)
        offset = carrier - json.loads(json.dumps(e)).get("carrier_hz", carrier)
        lo99, hi99 = spans.get(f, (carrier - bw / 2, carrier + bw / 2))
        lo99 += offset
        hi99 += offset
        band_lo, band_hi = carrier - bw / 2 - 20, carrier + bw / 2 + 20
        cands = found.get(f, [])
        mine = [c for c in cands if c["hi"] > band_lo and c["lo"] < band_hi]
        extra = len(cands) - len(mine)
        row = {"file": f, "mode": mode, "carrier": carrier, "bw": bw,
               "found": bool(mine), "whole": len(mine) == 1, "extra": extra,
               "pieces": len(mine)}
        if mine:
            best = max(mine, key=lambda c: c["hi"] - c["lo"])
            if len(mine) > 1:
                lo = min(c["lo"] for c in mine)
                hi = max(c["hi"] for c in mine)
                best = dict(best, lo=lo, hi=hi, center=(lo + hi) / 2)
            err = best["center"] - carrier
            width = best["hi"] - best["lo"]
            ref = hi99 - lo99
            row.update(center_err=round(err, 1), width=round(width, 1),
                       ref_width=ref, snr=best["snr"],
                       center_ok=abs(err) <= max(5.0, 0.1 * bw),
                       width_ok=(mode in OOK) or abs(width - ref) <= max(0.15 * ref, 16.0))
        else:
            row.update(center_ok=False, width_ok=False)
        rows.append(row)

    n = len(rows)
    def pct(key):
        return 100.0 * sum(1 for r in rows if r[key]) / max(n, 1)
    print("%s: %d clips  found %.0f%%  whole %.0f%%  center %.0f%%  width %.0f%%  "
          "extra candidates %d" % (os.path.basename(folder.rstrip("/")), n, pct("found"),
          pct("whole"), pct("center_ok"), pct("width_ok"), sum(r["extra"] for r in rows)))
    bad = [r for r in rows if not (r["found"] and r["whole"] and r["center_ok"] and r["width_ok"])
           or r["extra"]]
    for r in bad[:args.show]:
        if r["found"]:
            print("  %-12s pieces %d extra %d  center %+6.1f  width %6.1f vs %6.1f" % (
                r["mode"], r["pieces"], r["extra"], r["center_err"], r["width"], r["ref_width"]))
        else:
            print("  %-12s not found (extra %d)" % (r["mode"], r["extra"]))
    if len(bad) > args.show:
        print("  ... %d more" % (len(bad) - args.show))
    if args.json:
        with open(args.json, "w") as f:
            for r in rows:
                f.write(json.dumps(r) + "\n")


if __name__ == "__main__":
    main()
