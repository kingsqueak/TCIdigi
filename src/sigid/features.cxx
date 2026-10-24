// ----------------------------------------------------------------------------
//
//	features.cxx -- SigID: test a candidate against each signature
//
// Copyright (C) 2026
//		Chris, KC2RGW
//
// This file is part of fldigi.
//
// Fldigi is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fldigi is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with fldigi.  If not, see <http://www.gnu.org/licenses/>.
// ----------------------------------------------------------------------------

// Each signature is a hypothesis: "this candidate is that mode". The
// candidate is mixed down to a narrow complex baseband once, and every row
// whose bandwidth is close enough is scored by a test for its family. The
// tests share work through a small cache of spectrograms.

#include <config.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <vector>

#include "gfft.h"
#include "sigid_internal.h"
#include "sigid_signatures.h"

#include <cstdio>
#include <ctime>

namespace sigid {

bool trace = false;
double family_seconds[FAM_COUNT];

typedef std::complex<double> cplx;

//----------------------------------------------------------------------
// baseband

// Mixes x down so fc sits at 0 Hz, low-passes to +-bw/2 and decimates so
// the output rate is at least twice bw. Returns the output rate.
static double zoom(const float *x, size_t n, double fc, double bw, std::vector<cplx> &z)
{
	bw = std::max(bw, 20.0);
	int d = std::max(1, (int)floor(RATE / (2.0 * bw)));
	double fz = (double)RATE / d;
	double cutoff = std::min(0.6 * bw, 0.45 * fz);
	int taps = 8 * d + 33;
	if (!(taps & 1))
		taps++;
	std::vector<double> h(taps);
	double fcn = cutoff / RATE, sum = 0;
	for (int i = 0; i < taps; i++) {
		double m = i - (taps - 1) / 2.0;
		double s = m == 0 ? 2 * fcn : sin(2 * M_PI * fcn * m) / (M_PI * m);
		double w = 0.42 - 0.5 * cos(2 * M_PI * i / (taps - 1)) + 0.08 * cos(4 * M_PI * i / (taps - 1));
		h[i] = s * w;
		sum += h[i];
	}
	for (int i = 0; i < taps; i++)
		h[i] /= sum;

	// Mixed input, computed once.
	std::vector<cplx> mixed(n);
	double w = -2 * M_PI * fc / RATE;
	cplx rot(cos(w), sin(w)), ph(1, 0);
	for (size_t i = 0; i < n; i++) {
		mixed[i] = ph * (double)x[i];
		ph *= rot;
		if ((i & 1023) == 0)
			ph /= std::abs(ph);
	}
	z.clear();
	for (size_t at = 0; at + taps <= n; at += d) {
		cplx acc(0, 0);
		for (int i = 0; i < taps; i++)
			acc += h[i] * mixed[at + i];
		z.push_back(acc);
	}
	return fz;
}

static int pow2_at_least(int n)
{
	int m = 1;
	while (m < n)
		m <<= 1;
	return m;
}

//----------------------------------------------------------------------
// spectrograms, shared by rows with the same frame length

struct spectrogram {
	int len;	// frame length, samples
	int hop;
	int size;	// FFT size, a power of two >= len
	std::vector<std::vector<double> > frames;	// power per bin
};

class spectra {
public:
	spectra(const std::vector<cplx> &z, double fz) : z(z), fz(fz) {}

	// Frames of len samples (rectangular window, zero-padded to at least
	// pad * len), hop len / 2.
	const spectrogram &get(int len, int pad)
	{
		len = std::max(len, 4);
		int size = pow2_at_least(std::max(len * pad, 16));
		std::pair<int, int> key(len, size);
		std::map<std::pair<int, int>, spectrogram>::iterator it = cache.find(key);
		if (it != cache.end())
			return it->second;
		spectrogram &s = cache[key];
		s.len = len;
		s.hop = std::max(1, len / 2);
		s.size = size;
		g_fft<double> fft(size);
		std::vector<cplx> buf(size);
		for (size_t at = 0; at + len <= z.size(); at += s.hop) {
			std::fill(buf.begin(), buf.end(), cplx(0, 0));
			std::copy(z.begin() + at, z.begin() + at + len, buf.begin());
			fft.ComplexFFT(&buf[0]);
			std::vector<double> p(size);
			for (int k = 0; k < size; k++)
				p[k] = std::norm(buf[k]);
			s.frames.push_back(p);
		}
		return s;
	}

	double rate() const { return fz; }

private:
	const std::vector<cplx> &z;
	double fz;
	std::map<std::pair<int, int>, spectrogram> cache;
};

struct stft;

// One candidate's baseband and everything measured from it that several
// rows share. Owned by one score_signatures call, so nothing is kept
// between calls or shared between threads.
struct baseband {
	const std::vector<cplx> &z;
	double fz;
	// Spectrum of |z|^2, for envelope lines.
	std::vector<double> env;
	bool env_done;
	// Transition-line height per baud.
	std::map<double, double> transition;
	// Symbol-length STFTs per frame length.
	std::map<int, stft> stfts;
	// Keying-rate evidence per keyed row.
	std::map<const struct signature *, double> rates;
	baseband(const std::vector<cplx> &z, double fz) : z(z), fz(fz), env_done(false) {}
};

// Bin of baseband frequency f in an FFT of the given size.
static int bin_of(double f, double fz, int size)
{
	int b = (int)floor(f / fz * size + 0.5);
	return ((b % size) + size) % size;
}

//----------------------------------------------------------------------
// tone tests: MFSK, IFK, two-tone FSK, Throb

// How well "N tones, spacing s, one (or two) at a time, baud r" explains
// the candidate. For each lattice offset, every frame's strongest tone
// (or two) is compared with all the energy in the band: 1 means pure tones
// on the lattice. The tone must also change as often as that baud and
// family imply.
static double tone_fit(spectra &sp, const signature &sig, int at_once, double expect_change,
                       double *overlap = 0)
{
	if (overlap)
		*overlap = 0;
	double fz = sp.rate();
	int n = sig.tones;
	double s = sig.spacing;
	if (n < 2 || s <= 0 || sig.baud <= 0)
		return 0;
	int len = (int)floor(fz / sig.baud + 0.5);
	if (len < 4)
		return 0;
	// Bins fine enough to put six lattice offsets between two tones.
	const int offsets = 6;
	int pad = std::max(2, (int)ceil((double)offsets * fz / (s * len)));
	const spectrogram &g = sp.get(len, pad);
	if (g.frames.size() < 8)
		return 0;
	double span = (n - 1) * s;
	double lo = -span / 2 - s / 2, hi = span / 2 + s / 2;
	if (hi - lo >= fz)
		return 0;
	int blo = (int)floor(lo / fz * g.size), bhi = (int)ceil(hi / fz * g.size);
	double zpad = (double)g.size / g.len;

	double best = 0, best_change = 0, best_used = 0, best_second = 0;
	std::vector<int> hist(n);
	std::vector<int> tone_bin(n);
	for (int o = 0; o < offsets; o++) {
		double off = s * ((double)o / offsets - 0.5);
		for (int k = 0; k < n; k++)
			tone_bin[k] = bin_of((k - (n - 1) / 2.0) * s + off, fz, g.size);
		double conc = 0, second = 0;
		int changes = 0, steps = 0, last = -1;
		std::fill(hist.begin(), hist.end(), 0);
		for (size_t f = 0; f < g.frames.size(); f++) {
			const std::vector<double> &p = g.frames[f];
			double band = 0;
			for (int b = blo; b <= bhi; b++)
				band += p[((b % g.size) + g.size) % g.size];
			if (band <= 0)
				continue;
			int k1 = 0, k2 = -1;
			for (int k = 1; k < n; k++)
				if (p[tone_bin[k]] > p[tone_bin[k1]])
					k1 = k;
			double top = p[tone_bin[k1]];
			for (int k = 0; k < n; k++)
				if (k != k1 && (k2 < 0 || p[tone_bin[k]] > p[tone_bin[k2]]))
					k2 = k;
			if (top > 0)
				second += p[tone_bin[k2]] / top;
			if (at_once == 2)
				top += p[tone_bin[k2]];
			conc += std::min(1.0, zpad * top / band);
			hist[k1]++;
			// Compare frames a whole symbol (two hops) apart.
			if (f % 2 == 0) {
				if (last >= 0) {
					steps++;
					if (k1 != last)
						changes++;
				}
				last = k1;
			}
		}
		conc /= g.frames.size();
		// Energy spread evenly over the band already puts at_once/cells of
		// it on the strongest tone(s), where cells is how many resolution
		// cells the lattice spans: 4 for 4-tone Olivia, whose tones are a
		// baud apart. Count only the concentration above that.
		double cells = std::max(1.0, n * s / sig.baud);
		double base = std::min(0.9, at_once / cells);
		conc = std::max(0.0, (conc - base) / (1 - base));
		if (conc > best) {
			best = conc;
			best_second = second / g.frames.size();
			best_change = steps ? (double)changes / steps : 0;
			// Tones that carry a fair share of the frames.
			int used = 0;
			for (int k = 0; k < n; k++)
				if (hist[k] * 4.0 * n >= g.frames.size())
					used++;
			best_used = (double)used / n;
		}
	}
	// Frames shorter than the real symbols (a faster baud than the signal's)
	// see the tone change about half as often as expected. Only too few
	// changes count against the row; too many come with low concentration.
	double ratio = expect_change > 0 ? best_change / expect_change : 1;
	double miss = std::max(0.0, 0.8 - ratio);
	// A lattice with more tones than the signal uses (8 tones laid over a
	// 4-tone signal at half the spacing) leaves many of them idle. Olivia's
	// overlapping symbols put two tones in each frame; the stronger one
	// still marks the usage.
	// Only meaningful with a few symbols per tone in the window.
	bool enough = g.frames.size() / 2 >= (size_t)(3 * n);
	double usage = enough ? pow(std::min(1.0, best_used / 0.85), 2) : 1.0;
	if (overlap)
		*overlap = best_second;
	if (trace)
		fprintf(stderr, "    tones %-12s conc %.3f change %.2f/%.2f used %.2f%s second %.2f\n",
			sig.name, best, best_change, expect_change, best_used,
			enough ? "" : " (few)", best_second);
	return best * usage * exp(-miss * miss / (2 * 0.15 * 0.15));
}

//----------------------------------------------------------------------
// PSK tests

// The line that z^m makes when z is m-phase PSK. Raising the phase to
// the m-th power wipes out m-phase modulation and leaves a carrier.
// coherence: the line's share, 1 for a pure carrier (the largest FFT bin
// of (z/|z|)^m over the window, divided by its length). significance:
// how far it stands above the bins around it (out to a sixteenth of the
// band), 0 at 10 dB and 1 at 25 dB.
struct power_line_t {
	double coherence;
	double significance;
};

static power_line_t power_line(const std::vector<cplx> &z, int m)
{
	power_line_t out = { 0, 0 };
	int n = (int)z.size();
	if (n < 64)
		return out;
	int size = pow2_at_least(n);
	std::vector<cplx> buf(size, cplx(0, 0));
	for (int i = 0; i < n; i++) {
		double a = std::abs(z[i]);
		buf[i] = a > 0 ? std::pow(z[i] / a, m) : cplx(0, 0);
	}
	g_fft<double> fft(size);
	fft.ComplexFFT(&buf[0]);
	int kp = 0;
	for (int k = 1; k < size; k++)
		if (std::norm(buf[k]) > std::norm(buf[kp]))
			kp = k;
	double peak = std::norm(buf[kp]);
	std::vector<double> around;
	int reach = std::max(8, size / 16);
	for (int d = 3; d <= reach; d++) {
		around.push_back(std::norm(buf[(kp + d) % size]));
		around.push_back(std::norm(buf[(kp - d + size) % size]));
	}
	std::nth_element(around.begin(), around.begin() + around.size() / 2, around.end());
	double med = std::max(around[around.size() / 2], 1e-30);
	out.coherence = sqrt(peak) / n;
	if (peak > 0)
		out.significance = std::max(0.0, std::min(1.0, (10 * log10(peak / med) - 10) / 15));
	return out;
}

static double clamp01(double x)
{
	return std::max(0.0, std::min(1.0, x));
}

// Height of a spectral line at r in the real series v sampled at fz:
// the power there against the median of the spectrum around it (from 0.75 r
// to 1.25 r, leaving out the line), as 0..1 (0 at 6 dB above, 1 at 20 dB).
// A local floor matters: keyed envelopes put most of their power at low
// frequencies, which would make any low r look like a line.
// Power spectrum (first half) of a real series, mean removed.
static void series_spectrum(const std::vector<double> &v, std::vector<double> &mag)
{
	mag.clear();
	int n = (int)v.size();
	if (n < 64)
		return;
	int size = pow2_at_least(n);
	std::vector<cplx> buf(size, cplx(0, 0));
	double mean = 0;
	for (int i = 0; i < n; i++)
		mean += v[i];
	mean /= n;
	for (int i = 0; i < n; i++)
		buf[i] = cplx(v[i] - mean, 0);
	g_fft<double> fft(size);
	fft.ComplexFFT(&buf[0]);
	int half = size / 2;
	mag.resize(half);
	for (int k = 0; k < half; k++)
		mag[k] = std::norm(buf[k]);
}

// Height of the line at r in a spectrum from series_spectrum.
static double line_in(const std::vector<double> &mag, double fz, double r)
{
	int half = (int)mag.size();
	if (half < 32 || r >= fz / 2)
		return 0;
	int size = 2 * half;
	int b = (int)floor(r / fz * size + 0.5);
	int w = std::max(1, b / 100);
	// Neighbours out to a quarter of r, and at least 10 bins, so a line at
	// a low rate still has a floor to stand on.
	int reach = std::max(10 + 2 * w, b / 4);
	std::vector<double> around;
	for (int k = std::max(1, b - reach); k <= std::min(half - 1, b + reach); k++)
		if (abs(k - b) > 2 * w)
			around.push_back(mag[k]);
	if (around.size() < 6)
		return 0;
	std::nth_element(around.begin(), around.begin() + around.size() / 2, around.end());
	double floor_p = std::max(around[around.size() / 2], 1e-30);
	double peak = 0;
	for (int k = std::max(1, b - w); k <= std::min(half - 1, b + w); k++)
		peak = std::max(peak, mag[k]);
	if (peak <= 0)
		return 0;
	double snr_db = 10 * log10(peak / floor_p);
	return std::max(0.0, std::min(1.0, (snr_db - 6) / 14));
}

static double series_line(const std::vector<double> &v, double fz, double r)
{
	std::vector<double> mag;
	series_spectrum(v, mag);
	return line_in(mag, fz, r);
}

// The envelope line: PSK's amplitude dips once a symbol.
static double envelope_line_of(const std::vector<cplx> &z, double fz, double r)
{
	std::vector<double> v(z.size()), mag;
	for (size_t i = 0; i < z.size(); i++)
		v[i] = std::norm(z[i]);
	series_spectrum(v, mag);
	return line_in(mag, fz, r);
}

// The same for the candidate, its spectrum computed once and shared.
static double envelope_line(baseband &bb, double r)
{
	if (!bb.env_done) {
		std::vector<double> v(bb.z.size());
		for (size_t i = 0; i < bb.z.size(); i++)
			v[i] = std::norm(bb.z[i]);
		series_spectrum(v, bb.env);
		bb.env_done = true;
	}
	return line_in(bb.env, bb.fz, r);
}

// Keying on a fixed grid: random on-off data has no envelope line at its
// baud, but the size of the envelope's steps repeats there.
static double keying_line(const std::vector<cplx> &z, double fz, double r)
{
	int w = std::max(1, (int)(fz / (8 * r)));
	int n = (int)z.size();
	std::vector<double> env(n, 0.0);
	double acc = 0;
	for (int i = 0; i < n; i++) {
		acc += std::abs(z[i]);
		if (i >= w)
			acc -= std::abs(z[i - w]);
		env[i] = acc / std::min(i + 1, w);
	}
	int d = std::max(1, (int)(fz / (4 * r)));
	std::vector<double> step(n, 0.0);
	for (int i = d; i < n; i++)
		step[i] = fabs(env[i] - env[i - d]);
	return series_line(step, fz, r);
}

// Instantaneous frequency, Hz, smoothed over w samples.
static void inst_freq(const std::vector<cplx> &z, double fz, int w, std::vector<double> &f)
{
	int n = (int)z.size();
	f.assign(n, 0.0);
	std::vector<double> raw(n, 0.0);
	for (int i = 1; i < n; i++)
		raw[i] = std::arg(z[i] * std::conj(z[i - 1])) * fz / (2 * M_PI);
	double acc = 0;
	for (int i = 0; i < n; i++) {
		acc += raw[i];
		if (i >= w)
			acc -= raw[i - w];
		f[i] = acc / std::min(i + 1, w);
	}
}

// The transition line: a frequency-keyed signal jumps once a symbol, so
// the size of its frequency steps repeats at the baud. Cached per baud.
static double transition_line_of(const std::vector<cplx> &z, double fz, double r);

static double transition_line(baseband &bb, double r)
{
	std::map<double, double>::iterator it = bb.transition.find(r);
	if (it != bb.transition.end())
		return it->second;
	return bb.transition[r] = transition_line_of(bb.z, bb.fz, r);
}

static double transition_line_of(const std::vector<cplx> &z, double fz, double r)
{
	int w = std::max(1, (int)(fz / (8 * r)));
	std::vector<double> f;
	inst_freq(z, fz, w, f);
	int d = std::max(1, (int)(fz / (4 * r)));
	std::vector<double> step(f.size(), 0.0);
	for (size_t i = d; i < f.size(); i++)
		step[i] = fabs(f[i] - f[i - d]);
	return series_line(step, fz, r);
}

// FSK whose shift is narrower than its baud (FSK Hell) has no tones to
// resolve. Its instantaneous frequency instead sits near +-shift/2: the
// share of the signal's power found within shift/4 of either.
static double shift_fit(const std::vector<cplx> &z, double fz, double shift, double r)
{
	int w = std::max(1, (int)(fz / (4 * r)));
	std::vector<double> f;
	inst_freq(z, fz, w, f);
	// Power-weighted histogram of the instantaneous frequency, in bins of
	// shift/8, over +-shift.
	const int per = 8;
	int nb = 4 * per + 1;
	std::vector<double> hist(nb, 0.0);
	double total = 0;
	for (size_t i = 0; i < z.size(); i++) {
		double p = std::norm(z[i]);
		total += p;
		int b = (int)floor(f[i] / shift * per + 0.5) + 2 * per;
		if (b >= 0 && b < nb)
			hist[b] += p;
	}
	if (total <= 0)
		return 0;
	// The best pair of windows shift apart, each shift/4 wide: the tones
	// need not sit either side of the measured center, which leans toward
	// the tone used more. Continuous-phase FSK sits right on them, so most
	// of the power must be there; several tones, or noise, spread out.
	double best = 0;
	for (int c = per; c <= 3 * per; c++) {
		double near = 0;
		for (int side = -1; side <= 1; side += 2) {
			int mid = c + side * per / 2;
			for (int b = mid - per / 8; b <= mid + per / 8; b++)
				if (b >= 0 && b < nb)
					near += hist[b];
		}
		best = std::max(best, near / total);
	}
	return std::max(0.0, std::min(1.0, (best - 0.55) / 0.35));
}

struct psk_measures {
	power_line_t line[9];	// for m = 2, 4, 8
	bool have;
	psk_measures() : have(false) {}
};

static double psk_fit(baseband &bb, const signature &sig, psk_measures &pm)
{
	const std::vector<cplx> &z = bb.z;
	if (!pm.have) {
		pm.line[2] = power_line(z, 2);
		pm.line[4] = power_line(z, 4);
		pm.line[8] = power_line(z, 8);
		pm.have = true;
	}
	int m = sig.order;
	if (m != 2 && m != 4 && m != 8)
		return 0;
	if (trace)
		fprintf(stderr, "    psk %-12s coherence z2 %.3f z4 %.3f z8 %.3f"
			" significance %.2f %.2f %.2f baud-line %.2f\n", sig.name,
			pm.line[2].coherence, pm.line[4].coherence, pm.line[8].coherence,
			pm.line[2].significance, pm.line[4].significance, pm.line[8].significance,
			envelope_line(bb, sig.baud));
	// Every order shows some line (fldigi's symbols are not used evenly),
	// so the orders are told apart by how the lines compare: BPSK has a
	// strong z^2 line, QPSK a z^4 line well above its z^2, 8PSK a z^8 line
	// above its z^4.
	const power_line_t *L = pm.line;
	double bpsk = clamp01((L[2].coherence - 0.15) / 0.25);
	double order_fit;
	if (m == 2)
		order_fit = L[2].significance * bpsk;
	else if (m == 4)
		order_fit = L[4].significance * (1 - bpsk)
			* clamp01((L[4].coherence / std::max(L[2].coherence, 1e-6) - 1.5) / 1.5);
	else
		order_fit = L[8].significance * (1 - bpsk)
			* clamp01((L[8].coherence / std::max(L[4].coherence, 1e-6) - 1.2) / 1.0);
	double baud_fit = envelope_line(bb, sig.baud);
	// PSK dips in amplitude once a symbol; narrow FSK, which also shows a
	// line in z^2, keeps a constant envelope.
	return order_fit * (0.2 + 0.8 * baud_fit);
}

// Complex short-time spectra with frames one symbol long, half a symbol
// apart: each bin is that frequency averaged over a symbol, which is how
// a carrier of a multi-carrier mode is taken out. One per baud, shared by
// every carrier and every row at that baud.
struct stft {
	int len;
	int hop;
	int size;
	std::vector<std::vector<cplx> > frames;
};

static const stft &symbol_stft(baseband &bb, double baud)
{
	const std::vector<cplx> &z = bb.z;
	int len = std::max(4, (int)(bb.fz / (1.2 * baud)));
	std::map<int, stft>::iterator it = bb.stfts.find(len);
	if (it != bb.stfts.end())
		return it->second;
	stft &t = bb.stfts[len];
	t.len = len;
	t.hop = std::max(1, len / 2);
	// Bins a quarter of a symbol rate apart, so every carrier sits near one.
	t.size = pow2_at_least(4 * len);
	g_fft<double> fft(t.size);
	std::vector<cplx> buf(t.size);
	for (size_t at = 0; at + len <= z.size(); at += t.hop) {
		std::fill(buf.begin(), buf.end(), cplx(0, 0));
		std::copy(z.begin() + at, z.begin() + at + len, buf.begin());
		fft.ComplexFFT(&buf[0]);
		t.frames.push_back(buf);
	}
	return t;
}

// Multi-carrier PSK: every carrier of the row must be on steadily, dip
// once a symbol and show a BPSK line.
static double psk_multi_fit(baseband &bb, const signature &sig)
{
	double fz = bb.fz;
	int c = sig.tones;
	if (c < 2 || sig.spacing <= 0 || (c - 1) * sig.spacing / 2 + sig.baud >= fz / 2)
		return 0;
	const stft &t = symbol_stft(bb, sig.baud);
	if (t.frames.size() < 64)
		return 0;
	double rate = fz / t.hop;
	double total = 0;
	std::vector<cplx> y(t.frames.size());
	std::vector<double> env(t.frames.size());
	for (int k = 0; k < c; k++) {
		int b = bin_of((k - (c - 1) / 2.0) * sig.spacing, fz, t.size);
		for (size_t f = 0; f < t.frames.size(); f++)
			y[f] = t.frames[f][b];
		for (size_t i = 0; i < y.size(); i++)
			env[i] = std::abs(y[i]);
		std::sort(env.begin(), env.end());
		double steady = env[env.size() / 2] > 0 ?
			std::min(1.0, env[env.size() / 10] / env[env.size() / 2] * 2) : 0;
		double dip = envelope_line_of(y, rate, sig.baud);
		power_line_t L2 = power_line(y, 2);
		total += L2.significance * clamp01((L2.coherence - 0.15) / 0.25)
			* steady * (0.2 + 0.8 * dip);
	}
	return total / c;
}

//----------------------------------------------------------------------
// MT63 and on-off keying

// MT63: a comb of carriers at the row's spacing, all on at once. The
// averaged fine spectrum must repeat at the spacing, and a frame must not
// hold one tone.
static double mt63_fit(spectra &sp, const signature &sig, double single_tone)
{
	double fz = sp.rate();
	int len = pow2_at_least((int)ceil(8 * fz / sig.spacing));
	const spectrogram &g = sp.get(len, 1);
	if (g.frames.size() < 2)
		return 0;
	std::vector<double> avg(g.size, 0.0);
	for (size_t f = 0; f < g.frames.size(); f++)
		for (int k = 0; k < g.size; k++)
			avg[k] += g.frames[f][k];
	double half_bw = sig.bandwidth / 2;
	int lag = (int)floor(sig.spacing / fz * g.size + 0.5);
	int b0 = (int)ceil(-half_bw / fz * g.size), b1 = (int)floor(half_bw / fz * g.size) - lag;
	if (b1 <= b0 || lag < 2)
		return 0;
	// Correlation of the spectrum with itself one spacing along, against
	// half a spacing along.
	double mean = 0;
	int cnt = 0;
	for (int b = b0; b <= b1 + lag; b++) {
		mean += avg[((b % g.size) + g.size) % g.size];
		cnt++;
	}
	mean /= cnt;
	double at = 0, mid = 0, var = 0;
	for (int b = b0; b <= b1; b++) {
		double a = avg[((b % g.size) + g.size) % g.size] - mean;
		double p = avg[(((b + lag) % g.size) + g.size) % g.size] - mean;
		double q = avg[(((b + lag / 2) % g.size) + g.size) % g.size] - mean;
		at += a * p;
		mid += a * q;
		var += a * a;
	}
	if (var <= 0)
		return 0;
	double comb = std::max(0.0, (at - mid) / (2 * var));
	return std::min(1.0, comb * 2) * (1 - single_tone);
}

struct envelope_shape {
	// Share of the time below a quarter of the high level.
	double off;
	// Of the time above half the high level, the share above 85% of it:
	// near 1 for keyed carriers with flat tops, lower for pulse shapes.
	double flat;
};

// Envelope smoothed over a quarter symbol, measured against its high
// level (the 95th percentile).
static envelope_shape envelope_stats(const std::vector<cplx> &z, double fz, double baud)
{
	envelope_shape e = { 0, 0 };
	int n = (int)z.size();
	int w = std::max(1, (int)(fz / std::max(baud, 1.0) / 4));
	if (n < 4 * w)
		return e;
	std::vector<double> env;
	double acc = 0;
	for (int i = 0; i < n; i++) {
		acc += std::abs(z[i]);
		if (i >= w)
			acc -= std::abs(z[i - w]);
		if (i >= w)
			env.push_back(acc / w);
	}
	std::vector<double> sorted(env);
	std::sort(sorted.begin(), sorted.end());
	double high = sorted[(size_t)(sorted.size() * 0.95)];
	if (high <= 0)
		return e;
	size_t off = 0, on = 0, top = 0;
	for (size_t i = 0; i < env.size(); i++) {
		if (env[i] < 0.25 * high)
			off++;
		if (env[i] > 0.5 * high) {
			on++;
			if (env[i] > 0.85 * high)
				top++;
		}
	}
	e.off = (double)off / env.size();
	e.flat = on ? (double)top / on : 0;
	return e;
}

static double off_share(const std::vector<cplx> &z, double fz, double baud)
{
	return envelope_stats(z, fz, baud).off;
}

// Evidence that a keyed signal keeps the row's rate, cached per row. CW,
// whose speed varies, gets a fixed middling value.
static double keyed_rate(baseband &bb, const signature &sig)
{
	const std::vector<cplx> &z = bb.z;
	double fz = bb.fz;
	std::map<const signature *, double>::iterator it = bb.rates.find(&sig);
	if (it != bb.rates.end())
		return it->second;
	double r;
	if (sig.mode == MODE_CW)
		r = 0.6;
	else if (sig.fam == FAM_OOK)
		r = keying_line(z, fz, sig.baud);
	else	// Hell: columns repeat at the column rate, pixels key on a grid
		// 14 times as fast.
		r = std::max(envelope_line(bb, sig.baud), keying_line(z, fz, 14 * sig.baud));
	bb.rates[&sig] = r;
	return r;
}

//----------------------------------------------------------------------
// width each row should measure

// The 0.5%..99.5% span Phase 1 reports, predicted for a row.
static double expected_width(const signature &sig)
{
	switch (sig.fam) {
	case FAM_FSK2:
		// Narrow FSK (FSK Hell) has no separate tones.
		if (sig.spacing < sig.baud)
			return sig.spacing + 0.5 * sig.baud;
		return sig.spacing + 2.0 * sig.baud;
	case FAM_OLIVIA:
		// Shaped symbols keep the edges in.
		return (sig.tones - 1) * sig.spacing + 1.1 * sig.baud;
	case FAM_MFSK:
	case FAM_IFK:
	case FAM_THROB:
		return (sig.tones - 1) * sig.spacing + 2.0 * sig.baud;
	case FAM_PSK:
		return 1.5 * sig.baud;
	case FAM_PSK_MULTI:
		return (sig.tones - 1) * sig.spacing + 1.5 * sig.baud;
	case FAM_MT63:
		return sig.bandwidth;
	default:
		return sig.bandwidth;
	}
}

static double width_fit(double measured, const signature &sig)
{
	double want = std::max(expected_width(sig), 20.0);
	// A keyed carrier's width says little: hard keying splatters far past
	// the nominal width at high SNR, only the middle clears the noise at
	// low SNR, and CW's depends on the sending speed. Keyed rows rely on
	// their keying rate instead.
	if (sig.fam == FAM_OOK || sig.fam == FAM_HELL_OOK)
		return 1.0;
	double r = log(std::max(measured, 5.0) / want);
	return exp(-r * r / (2 * 0.3 * 0.3));
}

//----------------------------------------------------------------------

void score_signatures(const float *x, size_t n, const candidate_view &c,
                      std::vector<double> &score)
{
	score.assign(signature_count, 0.0);
	double width = c.hi - c.lo;
	if (n < (size_t)RATE * 2 || width <= 0)
		return;

	// One baseband for every row, wide enough for the widest plausible
	// reading of the candidate.
	std::vector<cplx> z;
	double fz = zoom(x, n, c.center, width * 1.3 + 30, z);
	baseband bb(z, fz);
	spectra sp(z, fz);
	psk_measures pm;
	// Long silences mean on-off keying; the constant-envelope families
	// cannot have them.
	double off = off_share(z, fz, 25.0);
	double steady = std::max(0.0, 1.0 - 2.5 * off);

	// MT63's "not one tone at a time" check, per carrier spacing.
	std::map<double, double> single_tone;
	for (int i = 0; i < signature_count; i++) {
		const signature &sig = signatures[i];
		double wf = width_fit(width, sig);
		if (wf < 0.15)
			continue;
		double fit = 0;
		clock_t t0 = clock();
		switch (sig.fam) {
		case FAM_MFSK:
		case FAM_OLIVIA:
			fit = tone_fit(sp, sig, 1, 1.0 - 1.0 / sig.tones);
			fit *= steady * (0.4 + 0.6 * transition_line(bb, sig.baud));
			break;
		case FAM_IFK:
			fit = tone_fit(sp, sig, 1, 1.0);
			fit *= steady * (0.4 + 0.6 * transition_line(bb, sig.baud));
			break;
		case FAM_FSK2:
			if (sig.spacing < 0.9 * sig.baud)
				fit = shift_fit(z, fz, sig.spacing, sig.baud);
			else
				fit = tone_fit(sp, sig, 1, 0.5)
					* (0.4 + 0.6 * transition_line(bb, sig.baud));
			fit *= steady;
			break;
		case FAM_THROB:
			// Throb shapes each symbol with a raised cosine, so its
			// envelope repeats at the baud.
			// Its pulses also put a harmonic at twice the baud, so a line
			// at half the row's baud means a slower Throb.
			fit = tone_fit(sp, sig, 2, 0.8)
				* (0.4 + 0.6 * envelope_line(bb, sig.baud))
				* (1 - 0.8 * envelope_line(bb, sig.baud / 2));
			break;
		case FAM_PSK:
			fit = psk_fit(bb, sig, pm) * steady;
			break;
		case FAM_PSK_MULTI:
			fit = psk_multi_fit(bb, sig) * steady;
			break;
		case FAM_MT63:
			if (!single_tone.count(sig.spacing)) {
				signature probe = sig;
				probe.tones = 64;
				single_tone[sig.spacing] = tone_fit(sp, probe, 1, 0.98);
			}
			fit = mt63_fit(sp, sig, single_tone[sig.spacing]) * steady;
			break;
		case FAM_OOK:
		case FAM_HELL_OOK: {
			// Keyed: long silences and flat tops, judged over a quarter
			// of the shortest element (a Hell pixel is a 14th of a
			// column).
			double element = sig.fam == FAM_HELL_OOK ? 14 * sig.baud : sig.baud;
			envelope_shape e = envelope_stats(z, fz, element);
			double keyed = std::min(1.0, e.off * 2.5)
				* std::max(0.0, std::min(1.0, (e.flat - 0.35) / 0.35));
			// A fixed keying rate (SCAMP, Hell) must show as a line;
			// CW, whose speed varies, is what is left without one. A
			// slower keyed row whose rate divides this one's and shows
			// its own line explains the line here as a harmonic.
			double rate = keyed_rate(bb, sig);
			for (int j = 0; j < signature_count; j++) {
				const signature &o = signatures[j];
				if ((o.fam != FAM_OOK && o.fam != FAM_HELL_OOK) || o.mode == MODE_CW
				    || o.baud >= 0.9 * sig.baud)
					continue;
				double h = sig.baud / o.baud;
				if (fabs(h - floor(h + 0.5)) <= 0.03 * h)
					rate *= 1 - 0.8 * keyed_rate(bb, o);
			}
			fit = keyed * (0.3 + 0.7 * rate);
			if (trace)
				fprintf(stderr, "    keyed %-12s off %.2f flat %.2f rate %.2f\n",
					sig.name, e.off, e.flat, rate);
			break;
		}
		default:
			break;
		}
		if (trace)
			family_seconds[sig.fam] += (double)(clock() - t0) / CLOCKS_PER_SEC;
		score[i] = fit * wf;
		if (trace && score[i] > 0.02)
			fprintf(stderr, "  %-12s fit %.3f width %.3f (measured %.0f, expected %.0f)"
				" off %.2f -> %.3f\n", sig.name, fit, wf, width,
				expected_width(sig), off, score[i]);
	}
}

void decide(const std::vector<double> &score, classification &out)
{
	out = classification();
	int best = -1, alt = -1;
	for (int i = 0; i < (int)score.size(); i++)
		if (best < 0 || score[i] > score[best])
			best = i;
	if (best < 0 || score[best] <= 0)
		return;
	// The runner-up from another group decides the confidence; one from
	// the same group is reported as the alternate.
	double rival = 0;
	for (int i = 0; i < (int)score.size(); i++) {
		if (i == best)
			continue;
		bool same = signatures[best].group && signatures[i].group == signatures[best].group;
		if (same) {
			if (alt < 0 || score[i] > score[alt])
				alt = i;
		} else
			rival = std::max(rival, score[i]);
	}
	out.index = best;
	out.score = score[best];
	out.alternate = alt;
	out.confidence = score[best] <= 0 ? 0 : (score[best] - rival) / score[best];
}

} // namespace sigid
