#!/usr/bin/env python3
# gen_signatures.py -- write src/sigid/signatures.cxx
# Copyright (C) 2026
# This file is part of fldigi. GPL-3.0-or-later.
"""Generate the SigID signature table from the mode list below.

Run from the top of the source tree:

    scripts/sigid/gen_signatures.py

Each add() is one row: fldigi's short mode name, family, tones or
carriers, spacing, baud and bandwidth (see src/sigid/sigid_signatures.h).
The MODE_* enum for each name is looked up in src/globals/globals.cxx, so
a typo fails here, not at run time. Rows with the same physical signal
(same kind, tones, spacing, baud and PSK order) are put in one look-alike
group automatically. To add a mode, add a row and re-run.
"""

import re
import sys
src = open("src/globals/globals.cxx").read()
enum_of = {}
for m in re.finditer(r'\{\s*(MODE_[A-Za-z0-9_]+)\s*,\s*&?[A-Za-z0-9_]+\s*,\s*"([^"]*)"', src):
    enum_of[m.group(2)] = m.group(1)

rows = []   # (sname, family, tones, spacing, baud, bw, order, group, comment-block)
def add(name, fam, tones, spacing, baud, bw, order=0, group=0):
    if name not in enum_of:
        sys.exit("no enum for " + name)
    rows.append([name, fam, tones, spacing, baud, bw, order, group])

blocks = []
def block(title):
    blocks.append((len(rows), title))

block("One keyed carrier. CW's rate is dots per second at 20 WPM.")
add("CW", "FAM_OOK", 1, 0, 20 * 50 / 60.0, 150)
add("SCAMPOOK", "FAM_OOK", 1, 0, 8000 / 256.0, 62.5)
add("SCOOKSLW", "FAM_OOK", 1, 0, 8000 / 576.0, 27.78)

block("Hell as on-off keying. Rate: columns per second.")
add("FELDHELL", "FAM_HELL_OOK", 1, 0, 17.5, 245)
add("SLOWHELL", "FAM_HELL_OOK", 1, 0, 2.1875, 30.625)
add("HELLX5", "FAM_HELL_OOK", 1, 0, 87.5, 1225)
add("HELLX9", "FAM_HELL_OOK", 1, 0, 157.5, 2205)

block("Two-tone FSK. Spacing is the shift. FSK Hell keys at its pixel rate,\n// 14 pixels per column.")
add("RTTY", "FAM_FSK2", 2, 170, 45.45, 170)
add("NAVTEX", "FAM_FSK2", 2, 170, 100, 170, group="NAVTEX")
add("SITORB", "FAM_FSK2", 2, 170, 100, 170, group="NAVTEX")
add("SCAMPFSK", "FAM_FSK2", 2, 66.667, 8000 / 240.0, 133.33)
add("SCFSKFST", "FAM_FSK2", 2, 166.667, 8000 / 96.0, 333.33)
add("SCFSKSLW", "FAM_FSK2", 2, 41.667, 8000 / 576.0, 69.44)
add("SCFSKVSL", "FAM_FSK2", 2, 20.833, 8000 / 1152.0, 34.72)
add("FSKH245", "FAM_FSK2", 2, 122.5, 245, 122.5)
add("FSKH105", "FAM_FSK2", 2, 55, 245, 55)
add("HELL80", "FAM_FSK2", 2, 300, 490, 300)

block("MFSK: one tone at a time, spacing equal to the baud.")
for name, n, sp in (("MFSK4", 32, 3.90625), ("MFSK8", 32, 7.8125), ("MFSK11", 16, 11025 / 1024.0),
                    ("MFSK16", 16, 15.625), ("MFSK22", 16, 11025 / 512.0), ("MFSK31", 8, 31.25),
                    ("MFSK32", 16, 31.25), ("MFSK64", 16, 62.5), ("MFSK64L", 16, 62.5),
                    ("MFSK128", 16, 125), ("MFSK128L", 16, 125)):
    g = "MFSK64" if name.startswith("MFSK64") else "MFSK128" if name.startswith("MFSK128") else 0
    add(name, "FAM_MFSK", n, sp, sp, (n - 1) * sp, group=g)

block("Olivia and Contestia: N tones across a nominal bandwidth B,\n// spacing and baud B/N. The same N/B is the same signal.")
for fam_name, prefix in (("OLIVIA", "OLIVIA-"), ("CONT", "Cont-")):
    for n in (4, 8, 16, 32, 64):
        for b, tag in ((125, "125"), (250, "250"), (500, "500"), (1000, "1K"), (2000, "2K")):
            name = "%s%d/%s" % (prefix, n, tag)
            if name not in enum_of:
                continue
            sp = b / float(n)
            other = ("Cont-%d/%s" if fam_name == "OLIVIA" else "OLIVIA-%d/%s") % (n, tag)
            g = ("OC%d/%s" % (n, tag)) if other in enum_of else 0
            add(name, "FAM_OLIVIA", n, sp, sp, (n - 1) * sp, group=g)

block("Incremental frequency keying: DominoEX and THOR use 18 tones, FSQ\n// and IFKP 33. FSQ and IFKP use their default speeds.")
for (dname, tname), sp, baud in ((("DOMEXM", "THORM"), 2.0, 2.0), (("DOMEX4", "THOR4"), 7.8125, 3.90625),
                                 (("DOMEX5", "THOR5"), 11025 / 1024.0, 11025 / 2048.0),
                                 (("DOMEX8", "THOR8"), 15.625, 7.8125),
                                 (("DOMX11", "THOR11"), 11025 / 1024.0, 11025 / 1024.0),
                                 (("DOMX16", "THOR16"), 15.625, 15.625),
                                 (("DOMX22", "THOR22"), 11025 / 512.0, 11025 / 512.0)):
    add(dname, "FAM_IFK", 18, sp, baud, 18 * sp, group="DT" + dname)
    add(tname, "FAM_IFK", 18, sp, baud, 18 * sp, group="DT" + dname)
add("DOMX44", "FAM_IFK", 18, 11025 / 128.0, 11025 / 256.0, 18 * 11025 / 128.0)
add("DOMX88", "FAM_IFK", 18, 11025 / 128.0, 11025 / 128.0, 18 * 11025 / 128.0)
for name, sp, baud in (("THOR25", 25, 25), ("THOR32", 31.25, 31.25), ("THOR44", 11025 / 256.0, 11025 / 256.0),
                       ("THOR56", 16000 / 290.0, 16000 / 290.0), ("THOR25x4", 100, 25),
                       ("THOR50x1", 50, 50), ("THOR50x2", 100, 50), ("THOR100", 100, 100)):
    add(name, "FAM_IFK", 18, sp, baud, 18 * sp)
# FSQ's "4.5 baud" setting is a 3072-sample symbol at 12 kHz.
add("FSQ", "FAM_IFK", 33, 3 * 12000 / 4096.0, 12000 / 3072.0, 33 * 3 * 12000 / 4096.0)
add("IFKP", "FAM_IFK", 33, 3 * 16000 / 4096.0, 16000 / 4096.0, 33 * 3 * 16000 / 4096.0)

block("Throb: two tones at once from 9 (Throb) or 11 (ThrobX).")
for name, n, sp, baud in (("THROB1", 9, 8, 8000 / 8192.0), ("THROB2", 9, 8, 8000 / 4096.0),
                          ("THROB4", 9, 16, 8000 / 2048.0), ("THRBX1", 11, 7.8125, 8000 / 8192.0),
                          ("THRBX2", 11, 7.8125, 8000 / 4096.0), ("THRBX4", 11, 15.625, 8000 / 2048.0)):
    add(name, "FAM_THROB", n, sp, baud, (n - 1) * sp)

block("One PSK carrier. PSK-R and BPSK63F add FEC to BPSK at the same rate,\n// and the 8PSK F and FL modes add it to 8PSK.")
for baud, b, r in ((31.25, "BPSK31", None), (62.5, "BPSK63", "BPSK63F"), (125, "BPSK125", "PSK125R"),
                   (250, "BPSK250", "PSK250R"), (500, "BPSK500", "PSK500R"), (1000, "BPSK1000", "PSK1000R")):
    g = ("B%g" % baud) if r else 0
    add(b, "FAM_PSK", 1, 0, baud, baud, 2, g)
    if r:
        add(r, "FAM_PSK", 1, 0, baud, baud, 2, g)
for name, baud in (("QPSK31", 31.25), ("QPSK63", 62.5), ("QPSK125", 125), ("QPSK250", 250),
                   ("QPSK500", 500)):
    add(name, "FAM_PSK", 1, 0, baud, baud, 4)
for baud, names in ((125, ("8PSK125", "8PSK125FL", "8PSK125F")), (250, ("8PSK250", "8PSK250FL", "8PSK250F")),
                    (500, ("8PSK500", "8PSK500F")), (1000, ("8PSK1000", "8PSK1000F"))):
    for n in names:
        add(n, "FAM_PSK", 1, 0, baud, baud, 8, "8P%d" % baud)
add("8PSK1200F", "FAM_PSK", 1, 0, 16000 / 13.0, 16000 / 13.0, 8)

block("Multi-carrier PSK: carriers 1.4 baud apart. The C modes (no FEC) and\n// RC modes (FEC) with the same carriers and baud are the same signal.")
multi = [("PSK125C12", 12, 125), ("PSK250C6", 6, 250), ("PSK500C2", 2, 500), ("PSK500C4", 4, 500),
         ("PSK800C2", 2, 800), ("PSK1000C2", 2, 1000)]
rc = [("PSK63RC%d" % n, n, 62.5) for n in (4, 5, 10, 20, 32)] + \
     [("PSK125RC%d" % n, n, 125) for n in (4, 5, 10, 12, 16)] + \
     [("PSK250RC%d" % n, n, 250) for n in (2, 3, 5, 6, 7)] + \
     [("PSK500RC%d" % n, n, 500) for n in (2, 3, 4)] + [("PSK800RC2", 2, 800), ("PSK1000RC2", 2, 1000)]
pairs = {(n, b) for _, n, b in multi}
for name, n, b in multi + rc:
    g = ("M%dx%g" % (n, b)) if (n, b) in pairs else 0
    add(name, "FAM_PSK_MULTI", n, 1.4 * b, b, b * (1 + 1.4 * (n - 1)), 2, g)

block("MT63: 64 differential BPSK carriers. S and L differ only in\n// interleave length.")
for tag, bw, baud in (("500", 500, 5), ("1K", 1000, 10), ("2K", 2000, 20)):
    for sl in "SL":
        add("MT63-%s%s" % (tag, sl), "FAM_MT63", 64, bw / 64.0, baud, bw, 2, "MT" + tag)

# Rows that look the same on the air share a group: same family kind,
# tone or carrier count, spacing and baud (PSK: order and baud). The
# hand-set groups above are kept as names for those that need no rule.
def physical(r):
    name, fam, tones, sp, baud, bw, order, group = r
    kind = {"FAM_MFSK": "tone", "FAM_OLIVIA": "tone", "FAM_IFK": "ifk"}.get(fam, fam)
    return (kind, tones, round(sp, 2), round(baud, 2), order)
by_key = {}
for r in rows:
    by_key.setdefault(physical(r), []).append(r)
for members in by_key.values():
    if len(members) > 1:
        tag = "P:" + members[0][0]
        for r in members:
            r[7] = r[7] or tag
# merge groups that share a member (MFSK64L joins OLIVIA-16/1K via MFSK64)
parent = {}
def find(x):
    while parent.get(x, x) != x:
        x = parent[x]
    return x
for members in by_key.values():
    tags = [r[7] for r in members if r[7]]
    for t in tags[1:]:
        parent[find(t)] = find(tags[0])
for r in rows:
    if r[7]:
        r[7] = find(r[7])
gid = {}
for r in rows:
    if r[7]:
        r[7] = gid.setdefault(r[7], len(gid) + 1)

hdr = open("src/sigid/sigid_signatures.h").read().split("// ----------------------------------------------------------------------------\n\n")[0]
hdr = hdr.replace("sigid_signatures.h -- what each known signal looks like", "signatures.cxx -- SigID signature table")
out = [hdr + "// ----------------------------------------------------------------------------\n",
       "// Generated from SIGID-MODES.md and the modem sources. Bandwidth is",
       "// fldigi's get_bandwidth(); see sigid_signatures.h for the fields.",
       "",
       "#include <config.h>", "", '#include "sigid_signatures.h"', "", "namespace sigid {", "",
       "const signature signatures[] = {"]
starts = dict(blocks)
def num(v):
    s = ("%.6f" % v).rstrip("0").rstrip(".")
    return s if "." in s else s + ".0"
for i, r in enumerate(rows):
    if i in starts:
        if i:
            out.append("")
        out.append("// " + starts[i])
    name, fam, tones, sp, baud, bw, order, group = r
    out.append('\t{ %s, "%s", %s, %d, %s, %s, %s, %d, %d },' % (
        enum_of[name], name, fam, tones, num(sp), num(baud), num(bw), order, group))
out += ["};", "", "const int signature_count = sizeof(signatures) / sizeof(signatures[0]);", "",
        "} // namespace sigid", ""]
open("src/sigid/signatures.cxx", "w").write("\n".join(out))
print(len(rows), "rows,", len(gid), "groups")
