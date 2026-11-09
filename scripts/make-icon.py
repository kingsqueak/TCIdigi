#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 KC2RGW Chris
#
# Makes TCIdigi's icon: src/qtui/icons/TCIdigi.svg and TCIdigi.png (1024 px; the window
# icon, the Linux package's, and the source of the macOS .icns that make-icns.sh builds),
# and TCIdigi.ico (Windows). The style is the family's (SimpleHPSDR, SimpleDream,
# PatchPrimeMac, 590Ctrl): a dark slate tile with a faint grid, blue, cyan and yellow.
# The picture is digital modes: a waterfall with a few narrow signals, the tuning
# cursor on one of them, and below it the decoded text.
#
#   scripts/make-icon.py
#
# Needs rsvg-convert (brew install librsvg) and ImageMagick.

import os
import random
import shutil
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "src", "qtui", "icons")

TILE = (100, 100, 824, 185)   # x, y, size, corner radius
TOP, BOTTOM = 150, 610        # the waterfall
# Signals: (centre x, width, strength 0..1). The middle one is tuned.
SIGNALS = [(232, 36, 0.5), (366, 46, 0.7), (512, 56, 1.0), (664, 42, 0.6), (796, 34, 0.45)]
TUNED = 512
COLOURS = ["#123a8c", "#106BD8", "#2ED3C8", "#F5D03B", "#F9DF83"]  # weak to strong


def svg():
    x0, y0, size, r = TILE
    rng = random.Random(7)  # the same picture every time
    p = [f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
<defs>
<linearGradient id="tile" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#1F2633"/><stop offset="1" stop-color="#0B0D11"/></linearGradient>
<linearGradient id="glow" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3B82F6" stop-opacity="0.0"/><stop offset="1" stop-color="#3B82F6" stop-opacity="0.18"/></linearGradient>
<clipPath id="c"><rect x="{x0}" y="{y0}" width="{size}" height="{size}" rx="{r}"/></clipPath>
</defs>
<rect x="{x0}" y="{y0}" width="{size}" height="{size}" rx="{r}" fill="url(#tile)"/>
<g clip-path="url(#c)">
<rect x="{x0}" y="{y0 + size // 2}" width="{size}" height="{size // 2}" fill="url(#glow)"/>
<g stroke="#FFFFFF" stroke-opacity="0.06" stroke-width="3">''']
    for x in (375, 650):
        p.append(f'<line x1="{x}" y1="{y0}" x2="{x}" y2="{BOTTOM}"/>')
    for y in (300, 450):
        p.append(f'<line x1="{x0}" y1="{y}" x2="{x0 + size}" y2="{y}"/>')
    p.append("</g>")
    # The waterfall: each signal a column of short dashes that drift a little, coloured
    # by strength, fading towards the top (older).
    row, gap = 26, 6
    y = BOTTOM - row
    n = 0
    while y > TOP:
        age = n / ((BOTTOM - TOP) / (row + gap))
        for cx, width, strength in SIGNALS:
            level = strength * (0.75 + 0.25 * rng.random())
            colour = COLOURS[min(len(COLOURS) - 1, int(level * len(COLOURS)))]
            w = width * (0.7 + 0.5 * rng.random())
            x = cx + rng.uniform(-3, 3) - w / 2
            p.append(f'<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{row}" rx="6" fill="{colour}" '
                     f'opacity="{0.95 - 0.35 * age:.2f}"/>')
        y -= row + gap
        n += 1
    # The tuning cursor: two lines either side of the tuned signal, with a cap.
    for dx in (-52, 52):
        p.append(f'<rect x="{TUNED + dx - 4}" y="{TOP - 6}" width="8" height="{BOTTOM - TOP + 6}" rx="4" fill="#EF4444" opacity="0.9"/>')
    p.append(f'<rect x="{TUNED - 56}" y="{TOP - 16}" width="112" height="12" rx="6" fill="#EF4444"/>')
    # The baseline under the waterfall.
    p.append(f'<rect x="{x0}" y="{BOTTOM + 8}" width="{size}" height="5" fill="#7FB2FF" opacity="0.35"/>')
    # The decoded text: three lines of words.
    lines = [[96, 64, 120, 52, 88], [70, 140, 58, 104, 76], [110, 48, 92, 66]]
    for i, words in enumerate(lines):
        ty = 670 + i * 62
        tx = 175
        for w in words:
            if tx + w > 860:
                break
            p.append(f'<rect x="{tx}" y="{ty}" width="{w}" height="26" rx="13" fill="#C9D6EE" opacity="{0.85 - 0.18 * i:.2f}"/>')
            tx += w + 24
    # The cursor where the next character goes.
    p.append(f'<rect x="{tx}" y="{670 + 2 * 62 - 4}" width="12" height="34" rx="3" fill="#2ED3C8"/>')
    p.append("</g>")
    p.append(f'<rect x="{x0 + 0.5}" y="{y0 + 0.5}" width="{size - 1}" height="{size - 1}" rx="{r}" fill="none" stroke="#FFFFFF" stroke-opacity="0.09" stroke-width="2"/>')
    p.append("</svg>\n")
    return "\n".join(p)


def run(args):
    subprocess.run(args, check=True, capture_output=True)


def main():
    svg_path = os.path.join(OUT, "TCIdigi.svg")
    with open(svg_path, "w") as f:
        f.write(svg())
    work = tempfile.mkdtemp()
    big = os.path.join(work, "1024.png")
    run(["rsvg-convert", "-w", "1024", "-h", "1024", "-o", big, svg_path])
    run(["magick", big, "-strip", os.path.join(OUT, "TCIdigi.png")])
    run(["magick", big, "-strip", "-define", "icon:auto-resize=256,128,64,48,32,24,16", os.path.join(OUT, "TCIdigi.ico")])
    shutil.rmtree(work)
    print("made", svg_path, "TCIdigi.png, TCIdigi.ico")


if __name__ == "__main__":
    main()
