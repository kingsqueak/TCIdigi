#!/usr/bin/env python3
# impair.py -- add HF channel effects to a test WAV
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

"""Add HF channel effects to a mono 16-bit PCM WAV.

The chain is the one used to build a signal-identification corpus:
frequency offset and linear drift, a two-path Watterson channel
(ITU-R F.1487 / CCIR 520), then white noise at a chosen SNR. It is a
baseband audio model of those effects, for detector tests.

Stages, in order:

1. Samples are converted with int16 / 32768.
2. A 127-tap Blackman-windowed Hilbert FIR, applied by direct
   convolution, builds the analytic signal. The kernel is centered, so
   the (N-1)/2 group delay is removed and the transform lines up with
   the input. The analytic signal is then multiplied by exp(j*phi(t)),
   where the instantaneous offset is ``offset + drift * t / 60``
   (drift is Hz per minute) and phi is the integral of that.
3. ``awgn`` leaves the signal unfaded. The other presets sum two paths.
   Path 1 has no extra delay. Path 2 is delayed by the preset
   differential delay. Each path is independent complex Gaussian noise
   (Rayleigh amplitude) whose Doppler power spectrum is Gaussian with
   two-sigma width 2*sigma equal to the preset spread, which is the
   ITU-R F.1487 definition of Doppler spread. White noise at 100 Hz is
   shaped by the square root of that spectrum, interpolated linearly to
   the audio rate, and normalized to unit mean power. The real part is
   taken after the paths are summed.
4. If ``--snr`` is set, real white Gaussian noise is added. Signal power
   is the mean square after stages 2 and 3, before noise. With N0 the
   one-sided density, N0 = signal_power / 10**(snr/10) / noise_bw and
   the noise variance is N0 * (fs/2). The default noise_bw is 2500 Hz.
   The noise is white out to Nyquist; noise_bw sets the density so the
   SNR inside that reference band equals --snr. It is the average over
   the file, so a fade changes the instantaneous SNR.
5. The peak is scaled to about -1 dBFS (full scale is 32767) and written
   as 16-bit mono PCM at the same sample rate.

``--json`` prints one JSON object of the parameters that were applied,
including the seed and that pre-noise signal power.

Limits:

- The Hilbert FIR only approaches a flat 90 degree shift away from 0 Hz
  and Nyquist. The transition is on the order of fs/127. A tone near
  the middle of the band is shifted cleanly; energy near either edge is
  not.
- The first and last 63 samples include the FIR's zero-padded edges.
- With no offset, no drift, and ``awgn``, the Hilbert step is skipped.
  The samples stay as they were, apart from noise and the output scale.
- Doppler shaping is a DFT multiply, so each gain is periodic on the
  DFT length: the next power of two above the file, at the 100 Hz fade
  rate. A spread much smaller than 1/duration is only a rough draw from
  that spectrum.
- 100 Hz is enough for these presets (flutter's spread is 10 Hz). It is
  not enough for a much wider Doppler spectrum.
- A delay that is not an integer number of samples is linearly
  interpolated. At 8000 Hz every preset delay is an integer.
- ``--noise-bw`` wider than fs/2 still follows the variance formula, so
  the Nyquist band then holds less noise than the requested SNR implies.
- The same ``--seed`` repeats the fading and the noise. Python's
  ``random.Random`` is the generator.
"""

import argparse
import array
import json
import math
import random
import sys
import wave

# Differential delay (seconds) and Doppler spread 2*sigma (Hz).
# CCIR 520 good / moderate / poor, plus the flutter preset.
CHANNELS = {
    "good": (0.0005, 0.1),
    "moderate": (0.001, 0.5),
    "poor": (0.002, 1.0),
    "flutter": (0.0005, 10.0),
}

HILBERT_TAPS = 127
FADE_RATE_HZ = 100.0
# 0 dBFS is the largest positive int16. -1 dBFS leaves a little headroom.
TARGET_PEAK = 10.0 ** (-1.0 / 20.0)
INT16_SCALE = 32767.0


def _hilbert_taps(num_taps):
    """Odd-lag coefficients of a centered Blackman Hilbert FIR.

    Ideal taps are 2/(pi*k) for odd k and 0 otherwise. The window is
    symmetric, so the windowed kernel stays odd and the negative lags
    are implied by h[-k] = -h[k].
    """
    if num_taps % 2 != 1 or num_taps < 3:
        raise ValueError("Hilbert FIR length must be an odd integer >= 3")
    half = (num_taps - 1) // 2
    lags = []
    coeffs = []
    denom = num_taps - 1
    for k in range(1, half + 1, 2):
        # Tap index of lag +k. Blackman is symmetric about the center.
        n = half + k
        window = (
            0.42
            - 0.5 * math.cos(2.0 * math.pi * n / denom)
            + 0.08 * math.cos(4.0 * math.pi * n / denom)
        )
        lags.append(k)
        coeffs.append((2.0 / (math.pi * k)) * window)
    return lags, coeffs


_HILBERT_LAGS, _HILBERT_COEFFS = _hilbert_taps(HILBERT_TAPS)


def _hilbert(x):
    """Analytic-signal imaginary part, time-aligned with x.

    Direct convolution. Because h is odd,
    y[i] = sum_{k>0} h[k] * (x[i-k] - x[i+k]). Samples off the ends of
    x are treated as 0.
    """
    lags = _HILBERT_LAGS
    coeffs = _HILBERT_COEFFS
    n = len(x)
    y = [0.0] * n
    m = len(lags)
    half = lags[-1]

    def edge(i):
        acc = 0.0
        for t in range(m):
            k = lags[t]
            j1 = i - k
            j2 = i + k
            v1 = x[j1] if 0 <= j1 < n else 0.0
            v2 = x[j2] if 0 <= j2 < n else 0.0
            acc += coeffs[t] * (v1 - v2)
        return acc

    if n <= 2 * half:
        for i in range(n):
            y[i] = edge(i)
        return y

    for i in range(half):
        y[i] = edge(i)
    for i in range(half, n - half):
        acc = 0.0
        for t in range(m):
            k = lags[t]
            acc += coeffs[t] * (x[i - k] - x[i + k])
        y[i] = acc
    for i in range(n - half, n):
        y[i] = edge(i)
    return y


def _mix(x, hilb, fs, offset_hz, drift_hz_per_min, want_imag):
    """Multiply the analytic signal by exp(j*phi) and return real [, imag]."""
    n = len(x)
    real = [0.0] * n
    imag = [0.0] * n if want_imag else None
    two_pi = 2.0 * math.pi
    inv_fs = 1.0 / fs
    # f(t) = offset + drift * t/60, so phi = 2*pi*(offset*t + drift*t^2/120).
    drift_coef = drift_hz_per_min / 120.0
    for i in range(n):
        t = i * inv_fs
        phase = two_pi * (offset_hz * t + drift_coef * t * t)
        c = math.cos(phase)
        s = math.sin(phase)
        xr = x[i]
        xi = hilb[i]
        real[i] = xr * c - xi * s
        if want_imag:
            imag[i] = xr * s + xi * c
    return real, imag


def _next_pow2(n):
    p = 1
    while p < n:
        p <<= 1
    return p


def _fft(a, inverse=False):
    """In-place radix-2 DFT. ``len(a)`` must be a power of two.

    The forward transform uses e^{-j}. The inverse uses e^{+j} and
    divides by n, so the two are inverses of each other.
    """
    n = len(a)
    if n < 1 or n & (n - 1):
        raise ValueError("FFT length must be a power of two")
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
    sign = 1.0 if inverse else -1.0
    while step <= n:
        half = step >> 1
        ang = sign * 2.0 * math.pi / step
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
    if inverse:
        inv = 1.0 / n
        for i in range(n):
            a[i] *= inv
    return a


def _synth_gain(n, fs, spread_hz, rng):
    """One path's complex gain at the audio rate, unit mean power.

    Complex white noise is generated at FADE_RATE_HZ, shaped in the DFT
    domain, interpolated linearly, then scaled so the mean of |g|^2 over
    this file is 1. The shaping magnitude is exp(-f^2 / spread^2), the
    square root of a Gaussian spectrum whose 2*sigma equals spread_hz.
    """
    if spread_hz <= 0.0:
        raise ValueError("Doppler spread must be positive")
    n_low = int(math.floor((n - 1) * FADE_RATE_HZ / fs)) + 2
    n_fft = _next_pow2(max(n_low, 8))
    spec = [complex(rng.gauss(0.0, 1.0), rng.gauss(0.0, 1.0))
            for _ in range(n_fft)]
    _fft(spec, inverse=False)
    inv_spread2 = 1.0 / (spread_hz * spread_hz)
    bin_hz = FADE_RATE_HZ / n_fft
    half = n_fft // 2
    for k in range(n_fft):
        freq = (k if k <= half else k - n_fft) * bin_hz
        spec[k] *= math.exp(-(freq * freq) * inv_spread2)
    _fft(spec, inverse=True)

    low_r = [spec[i].real for i in range(n_low)]
    low_i = [spec[i].imag for i in range(n_low)]
    gr = [0.0] * n
    gi = [0.0] * n
    step = FADE_RATE_HZ / fs
    for i in range(n):
        pos = i * step
        i0 = int(pos)
        frac = pos - i0
        keep = 1.0 - frac
        gr[i] = low_r[i0] * keep + low_r[i0 + 1] * frac
        gi[i] = low_i[i0] * keep + low_i[i0 + 1] * frac

    power = 0.0
    for i in range(n):
        power += gr[i] * gr[i] + gi[i] * gi[i]
    power /= n
    if power > 0.0:
        scale = 1.0 / math.sqrt(power)
        for i in range(n):
            gr[i] *= scale
            gi[i] *= scale
    return gr, gi


def _sample_at(zr, zi, pos):
    """Linear interpolation of a complex sequence. Outside, the value is 0."""
    n = len(zr)
    if pos <= -1.0 or pos >= n:
        return 0.0, 0.0
    if pos < 0.0:
        frac = pos + 1.0
        return zr[0] * frac, zi[0] * frac
    if pos >= n - 1:
        keep = n - pos
        return zr[n - 1] * keep, zi[n - 1] * keep
    i0 = int(pos)
    frac = pos - i0
    keep = 1.0 - frac
    return (
        zr[i0] * keep + zr[i0 + 1] * frac,
        zi[i0] * keep + zi[i0 + 1] * frac,
    )


def _apply_paths(zr, zi, g1r, g1i, g2r, g2i, delay_samples):
    """Real part of g1(t)*z(t) + g2(t)*z(t - delay).

    The gain is the gain at time t, not the gain at the delayed instant.
    """
    n = len(zr)
    y = [0.0] * n
    if delay_samples < 0.0:
        raise ValueError("path delay must be non-negative")
    nearest = round(delay_samples)
    integer = abs(delay_samples - nearest) <= 1e-6
    if integer:
        d = int(nearest)
        if d >= n:
            for i in range(n):
                y[i] = g1r[i] * zr[i] - g1i[i] * zi[i]
            return y
        for i in range(d):
            y[i] = g1r[i] * zr[i] - g1i[i] * zi[i]
        for i in range(d, n):
            j = i - d
            y[i] = (
                g1r[i] * zr[i] - g1i[i] * zi[i]
                + g2r[i] * zr[j] - g2i[i] * zi[j]
            )
        return y

    for i in range(n):
        acc = g1r[i] * zr[i] - g1i[i] * zi[i]
        zdr, zdi = _sample_at(zr, zi, i - delay_samples)
        y[i] = acc + g2r[i] * zdr - g2i[i] * zdi
    return y


def _fade(zr, zi, fs, spread_hz, delay_s, rng):
    n = len(zr)
    g1r, g1i = _synth_gain(n, fs, spread_hz, rng)
    g2r, g2i = _synth_gain(n, fs, spread_hz, rng)
    return _apply_paths(zr, zi, g1r, g1i, g2r, g2i, delay_s * fs)


def _mean_square(y):
    if not y:
        return 0.0
    acc = 0.0
    for s in y:
        acc += s * s
    return acc / len(y)


def _add_noise(y, fs, snr_db, noise_bw, rng):
    """Add real AWGN in place. Return (signal_power, noise_variance).

    signal_power is measured before the noise is added. noise_variance
    is None when snr_db is None.
    """
    power = _mean_square(y)
    if snr_db is None:
        return power, None
    if not math.isfinite(snr_db):
        raise ValueError("snr must be finite")
    if not math.isfinite(noise_bw) or noise_bw <= 0.0:
        raise ValueError("noise bandwidth must be a positive finite number")
    if power == 0.0:
        variance = 0.0
    else:
        # SNR in the reference band: power / (N0 * noise_bw).
        density = power / (10.0 ** (snr_db / 10.0)) / noise_bw
        variance = density * (fs / 2.0)
    if variance > 0.0:
        sigma = math.sqrt(variance)
        for i in range(len(y)):
            y[i] += rng.gauss(0.0, sigma)
    return power, variance


def _read_wav(path):
    """Return (sample_rate, float_samples) for a mono 16-bit PCM WAV."""
    try:
        handle = wave.open(path, "rb")
    except wave.Error as exc:
        raise ValueError(f"{path}: {exc}") from exc
    with handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        comptype = handle.getcomptype()
        fs = handle.getframerate()
        n = handle.getnframes()
        raw = handle.readframes(n)
    if channels != 1:
        raise ValueError(f"{path}: need a mono file, got {channels} channels")
    if width != 2 or comptype != "NONE":
        raise ValueError(f"{path}: need uncompressed 16-bit PCM")
    if fs <= 0:
        raise ValueError(f"{path}: sample rate must be positive")
    if n <= 0 or len(raw) != n * 2:
        raise ValueError(f"{path}: no audio samples")
    buf = array.array("h")
    buf.frombytes(raw)
    if sys.byteorder != "little":
        buf.byteswap()
    scale = 1.0 / 32768.0
    return fs, [sample * scale for sample in buf]


def _write_wav(path, fs, samples):
    """Scale the peak to about -1 dBFS and write mono 16-bit PCM."""
    peak = 0.0
    for s in samples:
        a = s if s >= 0.0 else -s
        if a > peak:
            peak = a
    gain = 0.0 if peak == 0.0 else (TARGET_PEAK / peak) * INT16_SCALE
    out = array.array("h")
    for s in samples:
        v = int(round(s * gain))
        if v > 32767:
            v = 32767
        elif v < -32768:
            v = -32768
        out.append(v)
    if sys.byteorder != "little":
        out.byteswap()
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(int(fs))
        handle.writeframes(out.tobytes())


def _check_finite(name, value):
    if not math.isfinite(value):
        raise ValueError(f"{name} must be finite")


def impair_file(in_path, out_path, channel="awgn", offset=0.0, drift=0.0,
                snr=None, noise_bw=2500.0, seed=None):
    """Impair one WAV. Return the parameter record written by --json."""
    if channel != "awgn" and channel not in CHANNELS:
        known = ", ".join(["awgn"] + list(CHANNELS))
        raise ValueError(f"unknown channel {channel!r} (expected {known})")
    _check_finite("offset", offset)
    _check_finite("drift", drift)
    if snr is not None:
        _check_finite("snr", snr)
        _check_finite("noise_bw", noise_bw)

    if seed is None:
        seed = random.SystemRandom().randrange(0, 2 ** 32)
    rng = random.Random(seed)

    fs, samples = _read_wav(in_path)
    n = len(samples)
    fading = channel != "awgn"
    shifting = offset != 0.0 or drift != 0.0
    delay_s = 0.0
    spread_hz = 0.0

    if fading:
        delay_s, spread_hz = CHANNELS[channel]
        if shifting:
            hilb = _hilbert(samples)
            zr, zi = _mix(samples, hilb, fs, offset, drift, True)
        else:
            zr = samples
            zi = _hilbert(samples)
        output = _fade(zr, zi, fs, spread_hz, delay_s, rng)
    elif shifting:
        hilb = _hilbert(samples)
        output, _imag = _mix(samples, hilb, fs, offset, drift, False)
    else:
        output = list(samples)

    signal_power, noise_variance = _add_noise(
        output, fs, snr, noise_bw, rng)
    _write_wav(out_path, fs, output)

    return {
        "input": in_path,
        "output": out_path,
        "sample_rate": fs,
        "n_samples": n,
        "channel": channel,
        "delay_s": delay_s,
        "doppler_spread_hz": spread_hz,
        "offset_hz": offset,
        "drift_hz_per_min": drift,
        "snr_db": snr,
        "noise_bw_hz": noise_bw if snr is not None else None,
        "seed": seed,
        "signal_power": signal_power,
        "noise_variance": noise_variance,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="impair.py",
        description=(
            "Add HF noise, Watterson fading, frequency offset and drift "
            "to a mono 16-bit WAV."
        ),
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        epilog=(
            "Channel presets are differential delay and Doppler spread: "
            "good 0.5 ms / 0.1 Hz, moderate 1 ms / 0.5 Hz, "
            "poor 2 ms / 1 Hz, flutter 0.5 ms / 10 Hz, awgn none. "
            "SNR is set inside --noise-bw (default 2500 Hz), not inside "
            "the whole Nyquist band."
        ),
    )
    parser.add_argument("input", metavar="IN.wav", help="clean mono 16-bit WAV")
    parser.add_argument("output", metavar="OUT.wav", help="impaired WAV to write")
    parser.add_argument(
        "--snr", type=float, default=None,
        help="SNR in dB inside --noise-bw; omit to add no noise",
    )
    parser.add_argument(
        "--noise-bw", type=float, default=2500.0, dest="noise_bw",
        help="reference bandwidth for --snr, in Hz",
    )
    parser.add_argument(
        "--channel",
        choices=["awgn", "good", "moderate", "poor", "flutter"],
        default="awgn",
        help="Watterson preset",
    )
    parser.add_argument(
        "--offset", type=float, default=0.0,
        help="frequency offset in Hz (positive raises the frequency)",
    )
    parser.add_argument(
        "--drift", type=float, default=0.0,
        help="linear frequency drift in Hz per minute",
    )
    parser.add_argument(
        "--seed", type=int, default=None,
        help="RNG seed; a seed is chosen and reported if omitted",
    )
    parser.add_argument(
        "--json", action="store_true",
        help="print one JSON line of the parameters actually applied",
    )
    args = parser.parse_args(argv)
    try:
        record = impair_file(
            args.input,
            args.output,
            channel=args.channel,
            offset=args.offset,
            drift=args.drift,
            snr=args.snr,
            noise_bw=args.noise_bw,
            seed=args.seed,
        )
    except (OSError, ValueError) as exc:
        print(f"impair.py: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(record, allow_nan=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
