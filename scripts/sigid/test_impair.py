#!/usr/bin/env python3
# test_impair.py -- checks for the HF channel impairer
# Copyright (C) 2026
# This file is part of fldigi. GPL-3.0-or-later.
#
# Fldigi is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fldigi is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with fldigi.  If not, see <http://www.gnu.org/licenses/>.

"""Stdlib checks for impair.py.

The source is one second-ish of a 1000 Hz sine at 8000 Hz, with a length
that puts 1000 Hz on a DFT bin. SNR is a rectangular DFT-bin estimate:
tone-bin power against the average noise-bin power, scaled to 2500 Hz.
"""

import array
import json
import math
import os
import sys
import tempfile
import unittest
import wave
from contextlib import redirect_stdout
from io import StringIO

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import impair  # noqa: E402

FS = 8000
N = 8192  # 1000 Hz is bin 1024; bin width is 8000/8192 Hz
TONE_HZ = 1000.0


def write_sine(path, fs, freq, n, amplitude=0.5):
    buf = array.array("h")
    for i in range(n):
        sample = amplitude * math.sin(2.0 * math.pi * freq * i / fs)
        buf.append(int(round(sample * 32767.0)))
    if sys.byteorder != "little":
        buf.byteswap()
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(fs)
        handle.writeframes(buf.tobytes())


def read_mono16(path):
    with wave.open(path, "rb") as handle:
        fs = handle.getframerate()
        n = handle.getnframes()
        raw = handle.readframes(n)
        channels = handle.getnchannels()
        width = handle.getsampwidth()
    if channels != 1 or width != 2 or len(raw) != n * 2:
        raise AssertionError(f"{path}: expected mono 16-bit PCM")
    buf = array.array("h")
    buf.frombytes(raw)
    if sys.byteorder != "little":
        buf.byteswap()
    return fs, [sample / 32768.0 for sample in buf]


def fft_mag2(samples):
    """|X[k]|^2 of a power-of-two real sequence. Forward DFT, e^{-j}."""
    n = len(samples)
    a = [complex(v, 0.0) for v in samples]
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    step = 2
    while step <= n:
        half = step >> 1
        ang = -2.0 * math.pi / step
        wrb = math.cos(ang)
        wib = math.sin(ang)
        for start in range(0, n, step):
            wr = 1.0
            wi = 0.0
            for k in range(half):
                i0 = start + k
                i1 = i0 + half
                v = a[i1]
                vr = v.real * wr - v.imag * wi
                vi = v.real * wi + v.imag * wr
                u = a[i0]
                ur = u.real
                ui = u.imag
                a[i0] = complex(ur + vr, ui + vi)
                a[i1] = complex(ur - vr, ui - vi)
                nwr = wr * wrb - wi * wib
                wi = wr * wib + wi * wrb
                wr = nwr
        step <<= 1
    return [z.real * z.real + z.imag * z.imag for z in a]


def direct_bin_power(samples, k):
    """|sum x[n] exp(-j*2*pi*k*n/N)|^2, for one bin."""
    n = len(samples)
    ang = 2.0 * math.pi * k / n
    step_r = math.cos(ang)
    step_i = -math.sin(ang)
    cr = 1.0
    ci = 0.0
    sr = 0.0
    si = 0.0
    for v in samples:
        sr += v * cr
        si += v * ci
        cr, ci = cr * step_r - ci * step_i, cr * step_i + ci * step_r
    return sr * sr + si * si


def peak_hz(samples, fs):
    spec = fft_mag2(samples)
    half = len(samples) // 2
    best = 1
    best_p = spec[1]
    for k in range(2, half):
        if spec[k] > best_p:
            best_p = spec[k]
            best = k
    return best * fs / len(samples)


def snr_in_bandwidth(samples, fs, tone_hz, noise_bw):
    """SNR in dB from tone bins vs mean noise-bin power scaled to noise_bw.

    The tone is on one bin. Its power is the positive-frequency bin plus
    the matching negative-frequency bin. Each positive-frequency bin is
    ``fs/N`` Hz wide on a one-sided spectrum and holds both DFT bins'
    power (the factor of two below). That average density times noise_bw
    is the noise power the SNR is referred to.
    """
    n = len(samples)
    spec = fft_mag2(samples)
    k = int(round(tone_hz * n / fs))
    # Catch a broken FFT before it silently warps the SNR.
    direct = direct_bin_power(samples, k)
    if abs(spec[k] - direct) > 1e-6 * max(direct, 1.0):
        raise AssertionError("DFT bin does not match a direct sum")
    signal = (spec[k] + spec[n - k]) / (n * n)
    bin_hz = fs / n
    acc = 0.0
    count = 0
    for m in range(1, n // 2):
        if m == k:
            continue
        acc += 2.0 * spec[m] / (n * n)
        count += 1
    noise = (acc / count) / bin_hz * noise_bw
    return 10.0 * math.log10(signal / noise)


class ImpairTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sine = os.path.join(self.tmp.name, "sine.wav")
        write_sine(self.sine, FS, TONE_HZ, N)

    def tearDown(self):
        self.tmp.cleanup()

    def test_awgn_snr_within_1_db(self):
        out = os.path.join(self.tmp.name, "snr.wav")
        rc = impair.main([
            self.sine, out,
            "--channel", "awgn",
            "--snr", "10",
            "--noise-bw", "2500",
            "--seed", "1",
        ])
        self.assertEqual(rc, 0)
        fs, samples = read_mono16(out)
        self.assertEqual(fs, FS)
        self.assertEqual(len(samples), N)
        measured = snr_in_bandwidth(samples, fs, TONE_HZ, 2500.0)
        self.assertLessEqual(
            abs(measured - 10.0), 1.0,
            f"measured SNR {measured:.2f} dB",
        )

    def test_offset_moves_peak_by_50_hz(self):
        clean_fs, clean = read_mono16(self.sine)
        self.assertLessEqual(abs(peak_hz(clean, clean_fs) - TONE_HZ), 2.0)

        out = os.path.join(self.tmp.name, "offset.wav")
        rc = impair.main([
            self.sine, out,
            "--channel", "awgn",
            "--offset", "50",
            "--seed", "1",
        ])
        self.assertEqual(rc, 0)
        fs, samples = read_mono16(out)
        freq = peak_hz(samples, fs)
        self.assertLessEqual(
            abs(freq - 1050.0), 2.0,
            f"peak at {freq:.2f} Hz",
        )

    def test_seed_is_deterministic(self):
        out_a = os.path.join(self.tmp.name, "a.wav")
        out_b = os.path.join(self.tmp.name, "b.wav")
        out_c = os.path.join(self.tmp.name, "c.wav")
        args = ["--channel", "awgn", "--snr", "10", "--noise-bw", "2500"]
        self.assertEqual(impair.main([self.sine, out_a, *args, "--seed", "1"]), 0)
        self.assertEqual(impair.main([self.sine, out_b, *args, "--seed", "1"]), 0)
        self.assertEqual(impair.main([self.sine, out_c, *args, "--seed", "2"]), 0)
        with open(out_a, "rb") as handle:
            same = handle.read()
        with open(out_b, "rb") as handle:
            self.assertEqual(handle.read(), same)
        with open(out_c, "rb") as handle:
            self.assertNotEqual(handle.read(), same)

    def test_json_reports_applied_parameters(self):
        out = os.path.join(self.tmp.name, "json.wav")
        buf = StringIO()
        with redirect_stdout(buf):
            rc = impair.main([
                self.sine, out,
                "--channel", "moderate",
                "--snr", "10",
                "--noise-bw", "2500",
                "--offset", "12.5",
                "--drift", "-3",
                "--seed", "99",
                "--json",
            ])
        self.assertEqual(rc, 0)
        text = buf.getvalue()
        self.assertTrue(text.endswith("\n"))
        self.assertEqual(len(text.strip().splitlines()), 1)
        record = json.loads(text)
        self.assertEqual(record["seed"], 99)
        self.assertEqual(record["channel"], "moderate")
        self.assertEqual(record["delay_s"], 0.001)
        self.assertEqual(record["doppler_spread_hz"], 0.5)
        self.assertEqual(record["offset_hz"], 12.5)
        self.assertEqual(record["drift_hz_per_min"], -3.0)
        self.assertEqual(record["snr_db"], 10.0)
        self.assertEqual(record["noise_bw_hz"], 2500.0)
        self.assertEqual(record["sample_rate"], FS)
        self.assertEqual(record["n_samples"], N)
        self.assertGreater(record["signal_power"], 0.0)
        self.assertGreater(record["noise_variance"], 0.0)

        presets = {
            "good": (0.0005, 0.1),
            "poor": (0.002, 1.0),
            "flutter": (0.0005, 10.0),
            "awgn": (0.0, 0.0),
        }
        for channel, (delay, spread) in presets.items():
            dest = os.path.join(self.tmp.name, channel + ".wav")
            captured = StringIO()
            with redirect_stdout(captured):
                rc = impair.main([
                    self.sine, dest,
                    "--channel", channel,
                    "--seed", "3",
                    "--json",
                ])
            self.assertEqual(rc, 0, channel)
            got = json.loads(captured.getvalue())
            self.assertEqual(got["delay_s"], delay, channel)
            self.assertEqual(got["doppler_spread_hz"], spread, channel)
            self.assertIsNone(got["snr_db"], channel)
            self.assertIsNone(got["noise_bw_hz"], channel)
            fs, samples = read_mono16(dest)
            self.assertEqual(fs, FS)
            self.assertEqual(len(samples), N)


if __name__ == "__main__":
    unittest.main()
