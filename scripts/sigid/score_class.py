#!/usr/bin/env python3
# score_class.py -- score SigID classification against a corpus manifest
# Copyright (C) 2026
# This file is part of fldigi. GPL-3.0-or-later.
"""Run sigid-scan over a corpus folder and score the modes it names.

For each clip, the candidate overlapping the true band the most is taken
as the signal, and its mode at the end of the clip is compared with the
manifest:

  exact   the same mode
  group   the same mode or a look-alike in its group (Phase 3 territory)
  family  the same family (PSK, MFSK, IFK, ...), or a look-alike from
          another family (MFSK64 read as Olivia 16/1K is the same signal)
  none    no candidate, or no mode named

Then the misses are listed, most common first.

    score_class.py SCAN FOLDER [FOLDER...] [--show N] [--json OUT]
"""

import argparse
import collections
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_corpus import BANDWIDTH  # noqa: E402


def table(scan):
    out = subprocess.run([scan, "--signatures"], capture_output=True, text=True,
                         check=True).stdout
    rows = [json.loads(line) for line in out.splitlines()]
    return {r["name"]: r for r in rows}


def scan_folder(scan, folder, files):
    out = subprocess.run([scan, "--candidates"] + [os.path.join(folder, f) for f in files],
                         capture_output=True, text=True, check=False).stdout
    last = {}
    for line in out.splitlines():
        if line.startswith("#"):
            continue
        r = json.loads(line)
        if "cand" not in r:
            continue
        f = os.path.basename(r["file"])
        if f not in last or r["t"] > last[f][0]["t"]:
            last[f] = [r]
        elif r["t"] == last[f][0]["t"]:
            last[f].append(r)
    return last


def score(scan, folder, sig, show, rows_out):
    entries = [json.loads(line) for line in open(os.path.join(folder, "manifest.jsonl"))]
    if not entries:
        sys.exit("%s: manifest.jsonl is empty" % folder)
    found = scan_folder(scan, folder, [e["file"] for e in entries])
    counts = collections.Counter()
    misses = collections.Counter()
    for e in entries:
        truth = e["mode"]
        bw = BANDWIDTH.get(truth, 500)
        lo, hi = e["carrier_hz"] - bw / 2 - 20, e["carrier_hz"] + bw / 2 + 20
        cands = [c for c in found.get(e["file"], []) if c["hi"] > lo and c["lo"] < hi]
        pred = ""
        if cands:
            best = max(cands, key=lambda c: min(c["hi"], hi) - max(c["lo"], lo))
            pred = best["mode"]
        t = sig.get(truth)
        p = sig.get(pred)
        exact = pred == truth
        group = exact or (t and p and t["group"] and t["group"] == p["group"])
        family = bool(group or (t and p and t["family"] == p["family"]))
        counts["clips"] += 1
        counts["exact"] += exact
        counts["group"] += bool(group)
        counts["family"] += family
        counts["none"] += not pred
        if not group:
            misses[(truth, pred or "-")] += 1
        rows_out.append({"set": os.path.basename(folder.rstrip("/")), "file": e["file"],
                         "truth": truth, "pred": pred, "exact": exact,
                         "group": bool(group), "family": family})
    n = counts["clips"]
    print("%-10s %3d clips  exact %3.0f%%  group %3.0f%%  family %3.0f%%  none %d" % (
        os.path.basename(folder.rstrip("/")), n, 100.0 * counts["exact"] / n,
        100.0 * counts["group"] / n, 100.0 * counts["family"] / n, counts["none"]))
    for (truth, pred), k in misses.most_common(show):
        print("    %-12s -> %-12s %s" % (truth, pred, "x%d" % k if k > 1 else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("scan")
    ap.add_argument("folders", nargs="+")
    ap.add_argument("--show", type=int, default=15)
    ap.add_argument("--json")
    args = ap.parse_args()
    sig = table(args.scan)
    rows = []
    for folder in args.folders:
        score(args.scan, os.path.expanduser(folder), sig, args.show, rows)
    if args.json:
        with open(args.json, "w") as f:
            for r in rows:
                f.write(json.dumps(r) + "\n")


if __name__ == "__main__":
    main()
