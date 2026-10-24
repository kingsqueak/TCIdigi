// ----------------------------------------------------------------------------
//
//	segment.cxx -- SigID: noise floor and occupied parts of the passband
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

#include <config.h>

#include <algorithm>
#include <cmath>

#include "sigid_internal.h"

namespace sigid {

// A bin is noise while the 5-bin average around it stays this close to
// the floor. Averaging first keeps a weak wide signal out of the floor
// estimate even where single bins of it dip low.
static const double NOISE_MARGIN_DB = 1.0;
// The floor is never put lower than this far under the strongest bin, so
// a noiseless input does not turn sidelobes into signals.
static const double MAX_RANGE_DB = 50.0;
// A run starts where the smoothed excess reaches SEED_DB and spreads to
// neighbours down to EXTEND_DB.
static const double SEED_DB = 3.0;
static const double EXTEND_DB = 1.5;
// Share of a run's power left outside each of its edges.
static const double EDGE_FRACTION = 0.005;
// Runs closer than this, or than this share of the narrower one's width,
// are one signal. The share joins the lobes of fast PSK across the nulls
// between them.
static const double JOIN_GAP_HZ = 32.0;
static const double JOIN_GAP_SHARE = 0.2;
// Tones that take turns are joined up to this far apart (RTTY 850).
static const double TURNS_GAP_HZ = 900.0;
static const double TURNS_CORR = -0.25;
// Carriers of a comb are joined up to this far apart (2xPSK1000).
static const double COMB_GAP_HZ = 600.0;

//----------------------------------------------------------------------
// short_history

short_history::short_history()
	: data(HISTORY * SHORT_BINS, 0.0), head(0), count(0)
{
}

void short_history::push(const double *power)
{
	std::copy(power, power + SHORT_BINS, data.begin() + head * SHORT_BINS);
	head = (head + 1) % HISTORY;
	if (count < HISTORY)
		count++;
}

void short_history::clear()
{
	head = count = 0;
}

void short_history::band_series(double lo_hz, double hi_hz, std::vector<double> &out) const
{
	int b0 = (int)ceil(lo_hz / SHORT_BIN_HZ);
	int b1 = (int)floor(hi_hz / SHORT_BIN_HZ);
	if (b1 < b0)
		b0 = b1 = (int)floor((lo_hz + hi_hz) / 2 / SHORT_BIN_HZ + 0.5);
	b0 = std::max(b0, 0);
	b1 = std::min(b1, SHORT_BINS - 1);
	out.assign(count, 0.0);
	int start = (head - count + HISTORY) % HISTORY;
	for (int f = 0; f < count; f++) {
		const double *row = &data[((start + f) % HISTORY) * SHORT_BINS];
		double sum = 0.0;
		for (int b = b0; b <= b1; b++)
			sum += row[b];
		out[f] = sum;
	}
}

double correlation(const std::vector<double> &a, const std::vector<double> &b)
{
	size_t n = std::min(a.size(), b.size());
	if (n < 8)
		return 0.0;
	double ma = 0, mb = 0;
	for (size_t i = 0; i < n; i++) {
		ma += a[i];
		mb += b[i];
	}
	ma /= n;
	mb /= n;
	double sab = 0, saa = 0, sbb = 0;
	for (size_t i = 0; i < n; i++) {
		double da = a[i] - ma, db = b[i] - mb;
		sab += da * db;
		saa += da * da;
		sbb += db * db;
	}
	if (saa <= 0 || sbb <= 0)
		return 0.0;
	return sab / sqrt(saa * sbb);
}

//----------------------------------------------------------------------
// noise floor

// Least-squares line through the points where use[i] is set, with x
// scaled to -1..1. Returns false when there are too few points.
static bool fit_line(const double *y, const std::vector<char> &use, int n,
                     double &c0, double &c1)
{
	double s = 0, sx = 0, sxx = 0, sy = 0, sxy = 0;
	for (int i = 0; i < n; i++) {
		if (!use[i])
			continue;
		double x = n > 1 ? 2.0 * i / (n - 1) - 1.0 : 0.0;
		s += 1;
		sx += x;
		sxx += x * x;
		sy += y[i];
		sxy += x * y[i];
	}
	double det = s * sxx - sx * sx;
	if (s < 8 || fabs(det) < 1e-12)
		return false;
	c1 = (s * sxy - sx * sy) / det;
	c0 = (sy - c1 * sx) / s;
	return true;
}

// Marks the bins whose 5-bin average stays within NOISE_MARGIN_DB of the
// floor, keeping at least a fifth of the band (the quietest) as noise.
static void mark_noise(const double *y, const double *fl, int n, std::vector<char> &noise)
{
	std::vector<double> ratio(n);
	for (int i = 0; i < n; i++)
		ratio[i] = pow(10.0, (y[i] - fl[i]) / 10.0);
	int count = 0;
	for (int i = 0; i < n; i++) {
		int a = std::max(0, i - 2), b = std::min(n - 1, i + 2);
		double r = 0;
		for (int j = a; j <= b; j++)
			r += ratio[j];
		noise[i] = 10.0 * log10(r / (b - a + 1)) < NOISE_MARGIN_DB;
		count += noise[i];
	}
	if (count < n / 5) {
		std::vector<double> d(n);
		for (int i = 0; i < n; i++)
			d[i] = y[i] - fl[i];
		std::vector<double> sorted(d);
		std::nth_element(sorted.begin(), sorted.begin() + n / 5, sorted.end());
		for (int i = 0; i < n; i++)
			noise[i] = d[i] <= sorted[n / 5];
	}
}

// Two stages. First a straight line under the noise: fit, drop the bins
// that sit above it, fit again, until only noise is left. That gets the
// level and tilt of the passband without wide signals lifting it. Then
// each bin's floor becomes the mean of the nearby noise bins, the window
// widening until it holds enough of them, which follows curvature and a
// filter's roll-off and still bridges a wide signal from both sides.
void noise_floor(const double *pdb, int kmin, int kmax, double *floor_db)
{
	int n = kmax - kmin + 1;
	if (n < 1)
		return;
	const double *y = pdb + kmin;
	double *fl = floor_db + kmin;
	double peak = *std::max_element(y, y + n);

	std::vector<char> noise(n, 1);
	double c0 = 0, c1 = 0;
	for (int pass = 0; pass < 8; pass++) {
		if (!fit_line(y, noise, n, c0, c1))
			break;
		for (int i = 0; i < n; i++)
			fl[i] = c0 + c1 * (n > 1 ? 2.0 * i / (n - 1) - 1.0 : 0.0);
		mark_noise(y, fl, n, noise);
	}

	std::vector<double> sum(n + 1), cnt(n + 1), next(n);
	static const int widths[] = { 40, 80, 160, 320, 640 };
	for (int pass = 0; pass < 4; pass++) {
		sum[0] = cnt[0] = 0;
		for (int i = 0; i < n; i++) {
			sum[i + 1] = sum[i] + (noise[i] ? y[i] : 0.0);
			cnt[i + 1] = cnt[i] + (noise[i] ? 1 : 0);
		}
		for (int i = 0; i < n; i++) {
			next[i] = fl[i];
			for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
				int a = std::max(0, i - widths[w]);
				int b = std::min(n, i + widths[w] + 1);
				double c = cnt[b] - cnt[a];
				if (c >= 12 && c >= (b - a) / 4) {
					next[i] = (sum[b] - sum[a]) / c;
					break;
				}
			}
		}
		for (int i = 0; i < n; i++)
			fl[i] = next[i];
		mark_noise(y, fl, n, noise);
	}
	for (int i = 0; i < n; i++)
		fl[i] = std::max(fl[i], peak - MAX_RANGE_DB);
}

//----------------------------------------------------------------------
// runs above the floor

static double to_lin(double db) { return pow(10.0, db / 10.0); }
static double to_db(double lin) { return 10.0 * log10(std::max(lin, 1e-12)); }

void find_regions(const double *excess_db, int kmin, int kmax, std::vector<region> &out)
{
	int n = kmax - kmin + 1;
	if (n < 5)
		return;
	// Five-bin average of the power ratio steadies the noise bins.
	std::vector<double> lin(n), smooth(n);
	for (int i = 0; i < n; i++)
		lin[i] = to_lin(excess_db[kmin + i]);
	for (int i = 0; i < n; i++) {
		int a = std::max(0, i - 2), b = std::min(n - 1, i + 2);
		double s = 0;
		for (int j = a; j <= b; j++)
			s += lin[j];
		smooth[i] = to_db(s / (b - a + 1));
	}

	std::vector<char> used(n, 0);
	for (int i = 0; i < n; i++) {
		if (used[i] || smooth[i] < SEED_DB)
			continue;
		int a = i, b = i;
		while (a > 0 && !used[a - 1] && smooth[a - 1] >= EXTEND_DB)
			a--;
		while (b < n - 1 && smooth[b + 1] >= EXTEND_DB)
			b++;
		for (int j = a; j <= b; j++)
			used[j] = 1;
		if (b - a < 1)
			continue;
		region r;
		r.lo = kmin + a;
		r.hi = kmin + b;
		// The threshold finds the signal; its edges are where the power
		// above the floor reaches 0.5% and 99.5% of the total, which does
		// not grow with SNR the way a threshold crossing does. Look a few
		// bins past the run for skirts the smoothing hid.
		int wa = std::max(0, a - 3), wb = std::min(n - 1, b + 3);
		double total = 0;
		for (int j = wa; j <= wb; j++)
			total += std::max(lin[j] - 1.0, 0.0);
		double fa = a, fb = b + 1;
		if (total > 0) {
			double cum = 0;
			bool have_lo = false;
			for (int j = wa; j <= wb; j++) {
				double p = std::max(lin[j] - 1.0, 0.0);
				if (!have_lo && cum + p >= EDGE_FRACTION * total) {
					fa = j + (EDGE_FRACTION * total - cum) / p;
					have_lo = true;
				}
				if (cum + p >= (1.0 - EDGE_FRACTION) * total) {
					fb = j + ((1.0 - EDGE_FRACTION) * total - cum) / p;
					break;
				}
				cum += p;
			}
		}
		// Bin j covers j - 0.5 .. j + 0.5.
		r.lo_hz = (kmin + fa - 0.5) * BIN_HZ;
		r.hi_hz = (kmin + fb - 0.5) * BIN_HZ;
		double s = 0;
		for (int j = a; j <= b; j++)
			s += lin[j];
		r.excess_db = to_db(s / (b - a + 1));
		r.parts = 1;
		r.unit_hz = r.hi_hz - r.lo_hz;
		r.pitch_hz = 0;
		r.last_center_hz = (r.lo_hz + r.hi_hz) / 2;
		out.push_back(r);
	}
}

//----------------------------------------------------------------------
// joining runs into signals

static void join(region &a, const region &b)
{
	double ua = a.unit_hz * a.parts, ub = b.unit_hz * b.parts;
	double wa = a.hi_hz - a.lo_hz, wb = b.hi_hz - b.lo_hz;
	double la = to_lin(a.excess_db) * wa, lb = to_lin(b.excess_db) * wb;
	a.parts += b.parts;
	a.unit_hz = (ua + ub) / a.parts;
	a.hi = b.hi;
	a.hi_hz = b.hi_hz;
	a.excess_db = to_db((la + lb) / std::max(wa + wb, 1.0));
	a.last_center_hz = b.last_center_hz;
}

static bool take_turns(const region &a, const region &b, const short_history &h)
{
	if (h.frames() < HISTORY / 2)
		return false;
	std::vector<double> sa, sb;
	h.band_series(a.lo_hz, a.hi_hz, sa);
	h.band_series(b.lo_hz, b.hi_hz, sb);
	return correlation(sa, sb) < TURNS_CORR;
}

// Does b continue a's comb of equal carriers? A pair sets the pitch.
static bool continues_comb(const region &a, const region &b)
{
	double wb = b.hi_hz - b.lo_hz;
	if (b.parts != 1 || wb < 40.0)
		return false;
	if (wb < 0.75 * a.unit_hz || wb > 1.33 * a.unit_hz)
		return false;
	if (fabs(a.excess_db - b.excess_db) > 3.0)
		return false;
	double step = (b.lo_hz + b.hi_hz) / 2 - a.last_center_hz;
	if (a.parts == 1)
		return step >= 1.0 * a.unit_hz && step <= 1.9 * a.unit_hz;
	return fabs(step - a.pitch_hz) <= 0.15 * a.pitch_hz;
}

// fldigi's multi-carrier PSK modes have at least four carriers below
// 250 baud, and two or three at 250 baud and up. A shorter run of similar
// signals is more likely several stations side by side.
static bool plausible_comb(const region &r)
{
	return r.parts >= 4 || (r.parts >= 2 && r.unit_hz >= 200.0);
}

void merge_regions(std::vector<region> &rs, const short_history &h)
{
	if (rs.empty())
		return;
	std::sort(rs.begin(), rs.end(),
		[](const region &x, const region &y) { return x.lo_hz < y.lo_hz; });

	// Close neighbours, and tones that take turns.
	std::vector<region> out;
	out.push_back(rs[0]);
	for (size_t i = 1; i < rs.size(); i++) {
		region &a = out.back();
		const region &b = rs[i];
		double gap = b.lo_hz - a.hi_hz;
		double narrow = std::min(a.hi_hz - a.lo_hz, b.hi_hz - b.lo_hz);
		if (gap <= std::max(JOIN_GAP_HZ, JOIN_GAP_SHARE * narrow)
		    || (gap <= TURNS_GAP_HZ && take_turns(a, b, h)))
			join(a, b);
		else
			out.push_back(b);
	}

	// Combs of equal carriers. Build each chain, keep it only if fldigi
	// has a mode that shape.
	std::vector<region> combed;
	for (size_t i = 0; i < out.size(); i++) {
		region chain = out[i];
		size_t j = i + 1;
		if (chain.parts == 1) {
			while (j < out.size() && out[j].lo_hz - chain.hi_hz <= COMB_GAP_HZ
			       && continues_comb(chain, out[j])) {
				double step = (out[j].lo_hz + out[j].hi_hz) / 2 - chain.last_center_hz;
				if (chain.parts == 1)
					chain.pitch_hz = step;
				join(chain, out[j]);
				j++;
			}
		}
		if (j > i + 1 && plausible_comb(chain)) {
			combed.push_back(chain);
			i = j - 1;
		} else
			combed.push_back(out[i]);
	}
	rs.swap(combed);
}

} // namespace sigid
