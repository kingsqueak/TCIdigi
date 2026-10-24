// ----------------------------------------------------------------------------
//
//	verify.cxx -- SigID: tell look-alike modes apart
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

// Some modes are the same signal on the air and differ only in coding.
// Their spectra cannot tell them apart, but a few seconds of decoding can:
//
//   Olivia / Contestia / MFSK  fldigi's own Olivia receiver, run once as
//                              Olivia and once as Contestia; the one that
//                              syncs is the mode. Neither syncing on a
//                              group with an MFSK row means MFSK.
//   DominoEX / THOR            symbols read as DominoEX varicode: clean
//                              text is DominoEX, THOR's FEC output is not.
//   BPSK / PSK-R, BPSK63F      the bit stream: plain PSK varicode keeps
//                              runs of zeros at exactly two between
//                              characters; FEC output is random.
//
// Groups without a test (MT63 S/L, MFSK64/64L, 8PSK F/FL, C/RC, NAVTEX /
// SITOR-B) are left as look-alikes.

#include <config.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <vector>

#include "dominovar.h"
#include "interleave.h"
#include "thorvaricode.h"
#include "viterbi.h"
#include "jalocha/pj_mfsk.h"
#include "sigid_internal.h"
#include "sigid_signatures.h"

namespace sigid {

typedef std::complex<double> cplx;

//----------------------------------------------------------------------
// Olivia and Contestia

// Sync quality of fldigi's Olivia receiver on x, as Olivia or Contestia:
// the best FEC-sync S/N it reports while working through the audio.
static double olivia_sync(const float *x, size_t n, double center, const signature &sig,
                          bool contestia)
{
	MFSK_Receiver<double> rx;
	rx.bContestia = contestia;
	rx.Tones = sig.tones;
	rx.Bandwidth = (size_t)floor(sig.tones * sig.spacing + 0.5);
	rx.SyncMargin = 8;
	rx.SyncIntegLen = 4;
	rx.SyncThreshold = 0;
	rx.SampleRate = RATE;
	rx.InputSampleRate = RATE;
	double fc_offset = rx.Bandwidth * (1.0 - 0.5 / rx.Tones) / 2.0;
	rx.FirstCarrierMultiplier = (center - fc_offset) / 500.0;
	rx.Reverse = 0;
	if (rx.Preset() < 0)
		return 0;
	std::vector<double> buf(1024);
	double best = 0;
	uint8_t ch;
	for (size_t at = 0; at < n; at += buf.size()) {
		size_t len = std::min(buf.size(), n - at);
		for (size_t i = 0; i < len; i++)
			buf[i] = x[at + i];
		rx.Process(&buf[0], len);
		while (rx.GetChar(ch) > 0)
			;
		best = std::max(best, (double)rx.SignalToNoiseRatio());
	}
	return best;
}

//----------------------------------------------------------------------
// DominoEX

// Text-likeness of decoded characters: letters, digits, spaces and common
// punctuation against everything else.
static double text_share(const std::vector<int> &chars)
{
	if (chars.empty())
		return 0;
	size_t good = 0;
	for (size_t i = 0; i < chars.size(); i++) {
		int c = chars[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		    || c == ' ' || c == '.' || c == ',' || c == '\n' || c == '\r' || c == '?'
		    || c == '/' || c == '-' || c == '\'')
			good++;
	}
	return (double)good / chars.size();
}

// Reads the candidate as 18-tone IFK at the row's spacing and baud and
// turns each step between tones into a symbol the way DominoEX and THOR do
// (0..17; 16 and 17 are never sent). Returns how cleanly the frames held
// one tone (0..1), or -1 when there is too little audio.
static double read_ifk(const float *x, size_t n, double center, const signature &sig,
                       std::vector<int> &steps)
{
	steps.clear();
	const int tones = 18;
	std::vector<cplx> z;
	double fz = zoom(x, n, center, sig.bandwidth * 1.3 + 30, z);
	double len_f = fz / sig.baud;
	int len = (int)floor(len_f + 0.5);
	if (len < 4 || z.size() < (size_t)(len * 20))
		return -1;
	double s = sig.spacing;

	// Tone energies at the lattice for a frame starting at sample at, with
	// the lattice moved by off Hz.
	std::vector<double> e(tones);
	auto energies = [&](size_t at, double off) {
		for (int k = 0; k < tones; k++) {
			double f = (k - (tones - 1) / 2.0) * s + off;
			double w = -2 * M_PI * f / fz;
			cplx acc(0, 0), rot(cos(w), sin(w)), ph(1, 0);
			for (int i = 0; i < len; i++) {
				acc += z[at + i] * ph;
				ph *= rot;
			}
			e[k] = std::norm(acc);
		}
	};

	// Lattice offset and symbol timing: the pair whose frames put the most
	// of their tone energy on one tone.
	double best_off = 0, best_conc = -1;
	int best_phase = 0;
	const int offsets = 6, phases = 8;
	for (int o = 0; o < offsets; o++) {
		double off = s * ((double)o / offsets - 0.5);
		for (int p = 0; p < phases; p++) {
			size_t start = (size_t)(p * len_f / phases);
			double conc = 0;
			int frames = 0;
			for (double t = start; (size_t)t + len <= z.size() && frames < 60; t += len_f) {
				energies((size_t)t, off);
				double sum = 0, top = 0;
				for (int k = 0; k < tones; k++) {
					sum += e[k];
					top = std::max(top, e[k]);
				}
				if (sum > 0)
					conc += top / sum;
				frames++;
			}
			if (frames)
				conc /= frames;
			if (conc > best_conc) {
				best_conc = conc;
				best_off = off;
				best_phase = p;
			}
		}
	}
	int prev = -1;
	for (double t = best_phase * len_f / phases; (size_t)t + len <= z.size(); t += len_f) {
		energies((size_t)t, best_off);
		int k = (int)(std::max_element(e.begin(), e.end()) - e.begin());
		if (prev >= 0)
			steps.push_back(((k - prev - 2) % tones + tones) % tones);
		prev = k;
	}
	return best_conc;
}

// DominoEX without FEC: the steps are varicode nibbles.
static void dominoex_decode(const std::vector<int> &steps, std::vector<int> &chars)
{
	chars.clear();
	int nibbles[3] = { 0, 0, 0 }, count = 0;
	for (size_t i = 0; i < steps.size(); i++) {
		int c = steps[i];
		{
			if (c < 16) {
				// A nibble with its top bit clear starts a new character.
				if (!(c & 8)) {
					if (count > 0 && count <= 3) {
						unsigned sym = 0;
						for (int i = 0; i < count; i++)
							sym |= nibbles[i] << (4 * i);
						int ch = dominoex_varidec(sym);
						chars.push_back(ch);
					}
					count = 0;
				}
				// Position 0 is the newest nibble, as in dominoex.cxx.
				nibbles[2] = nibbles[1];
				nibbles[1] = nibbles[0];
				nibbles[0] = c;
				count = std::min(count + 1, 4);
			}
		}
	}
}

// THOR: the steps are four bits of a rate-1/2, K=7 convolutional code
// after an interleaver. Feeds them through THOR's own de-interleaver and
// Viterbi decoder and returns the share of decoded bits whose path metric
// is good (at least half scale), which fldigi shows as THOR's FEC
// confidence. Real THOR scores high; anything else, low.
static double thor_confidence(const std::vector<int> &steps, int depth, std::vector<int> &chars)
{
	chars.clear();
	interleave inlv(4, depth, INTERLEAVE_REV);
	viterbi dec(7, 0x6d, 0x4f);
	dec.settraceback(45);
	dec.setchunksize(1);
	unsigned char pair[2] = { 0, 0 };
	int half = 0, good = 0, total = 0;
	unsigned int shreg = 1;
	for (size_t i = 0; i < steps.size(); i++) {
		unsigned char sym[4];
		int c = steps[i];
		if (c >= 16) {
			sym[0] = sym[1] = sym[2] = sym[3] = 128;	// puncture
		} else {
			for (int b = 3; b >= 0; b--) {
				sym[b] = (c & 1) ? 255 : 0;
				c >>= 1;
			}
		}
		inlv.symbols(sym);
		for (int b = 0; b < 4; b++) {
			pair[0] = pair[1];
			pair[1] = sym[b];
			half ^= 1;
			if (half)
				continue;
			int met = 0;
			int bit = dec.decode(pair, &met);
			if (bit < 0)
				continue;
			total++;
			if (met >= 255 / 2)
				good++;
			shreg = (shreg << 1) | (bit ? 1 : 0);
			if ((shreg & 7) == 1) {
				chars.push_back(thorvaridec(shreg >> 1));
				shreg = 1;
			}
		}
	}
	return total >= 50 ? (double)good / total : -1;
}

//----------------------------------------------------------------------
// BPSK and its FEC variants

// Differentially demodulates BPSK at the row's baud and measures how the
// zero runs are spread. Plain PSK varicode puts exactly two zeros between
// characters and never two inside one, so among runs of two or more,
// nearly all are exactly two (idle aside). FEC output is random: about
// half. Returns that share, or -1 when there is too little to judge.
// quality gets how cleanly the phase steps read as 0 or 180 degrees
// (mean |cos|): bit errors also make runs look random, so a "random"
// verdict needs clean bits.
static double varicode_share(const float *x, size_t n, double center, const signature &sig,
                             double &quality, std::string *detail)
{
	quality = 0;
	std::vector<cplx> z;
	double fz = zoom(x, n, center, sig.baud * 1.6 + 20, z);
	double len_f = fz / sig.baud;
	if (len_f < 2 || z.size() < (size_t)(len_f * 200))
		return -1;
	// Symbol timing: PSK's envelope peaks mid-symbol.
	const int phases = 8;
	std::vector<double> energy(phases, 0.0);
	for (int p = 0; p < phases; p++)
		for (double t = p * len_f / phases; t < z.size(); t += len_f)
			energy[p] += std::norm(z[(size_t)t]);
	int phase = (int)(std::max_element(energy.begin(), energy.end()) - energy.begin());

	std::vector<int> bits;
	cplx last(0, 0);
	double qsum = 0;
	for (double t = phase * len_f / phases; t < z.size(); t += len_f) {
		cplx now = z[(size_t)t];
		cplx d = now * std::conj(last);
		if (std::abs(d) > 0) {
			bits.push_back(std::real(d) > 0 ? 1 : 0);
			qsum += fabs(std::real(d)) / std::abs(d);
		}
		last = now;
	}
	quality = bits.empty() ? 0 : qsum / bits.size();
	// fldigi's PSK sends a 0 as a phase reversal.
	int run = 0, two = 0, longer = 0;
	for (size_t i = 0; i < bits.size(); i++) {
		if (bits[i] == 0) {
			run++;
			continue;
		}
		if (run == 2)
			two++;
		else if (run >= 3 && run <= 6)	// longer runs are idle, not text
			longer++;
		run = 0;
	}
	int runs_used = two + longer;
	double best = runs_used >= 80 ? (double)two / runs_used : 0;
	if (detail) {
		char buf[96];
		snprintf(buf, sizeof(buf), "zero runs of exactly two: %.2f of %d, bit quality %.2f",
			best, runs_used, quality);
		*detail = buf;
	}
	return runs_used >= 80 ? best : -1;
}

//----------------------------------------------------------------------

static int group_row(int group, bool (*want)(const signature &))
{
	for (int i = 0; i < signature_count; i++)
		if (signatures[i].group == group && want(signatures[i]))
			return i;
	return -1;
}

static int group_count(int group, bool (*want)(const signature &))
{
	int n = 0;
	for (int i = 0; i < signature_count; i++)
		if (signatures[i].group == group && want(signatures[i]))
			n++;
	return n;
}

static bool is_olivia(const signature &s) { return s.fam == FAM_OLIVIA && s.name[0] == 'O'; }
static bool is_contestia(const signature &s) { return s.fam == FAM_OLIVIA && s.name[0] == 'C'; }
static bool is_mfsk(const signature &s) { return s.fam == FAM_MFSK; }
static bool is_dominoex(const signature &s) { return s.fam == FAM_IFK && s.name[0] == 'D'; }
static bool is_thor(const signature &s) { return s.fam == FAM_IFK && s.name[0] == 'T'; }
static bool is_plain_bpsk(const signature &s)
{
	return s.fam == FAM_PSK && s.order == 2 && s.name[0] == 'B' && s.name[strlen(s.name) - 1] != 'F';
}
static bool is_bpsk_fec(const signature &s)
{
	return s.fam == FAM_PSK && s.order == 2 && !is_plain_bpsk(s);
}

int verify_group(const float *x, size_t n, const candidate_view &c, int row, bool &settled,
                 std::string *detail)
{
	settled = true;
	if (row < 0 || row >= signature_count || !signatures[row].group)
		return -1;
	int g = signatures[row].group;
	char buf[160];

	int olivia = group_row(g, is_olivia), contestia = group_row(g, is_contestia);
	if (olivia >= 0 || contestia >= 0) {
		const signature &sig = signatures[olivia >= 0 ? olivia : contestia];
		// The receiver syncs on whole FEC blocks (64 symbols for Olivia, 32
		// for Contestia) and integrates four of them. With less audio than
		// about five blocks, a missing sync means nothing yet.
		double block = olivia >= 0 ? 64 : 32;
		double need_s = std::min(5 * block / sig.baud, 12.0);
		if (n < (size_t)(need_s * RATE)) {
			if (detail) {
				snprintf(buf, sizeof(buf), "too little audio yet (%.1f of %.1f s)",
					(double)n / RATE, need_s);
				*detail = buf;
			}
			return -1;
		}
		// Each receiver pass is costly (an FFT and dozens of soft decoders
		// per symbol), so give it only the most recent stretch it needs.
		double mfsk_s = std::min(8 * block / sig.baud, 15.0);
		size_t use = std::min(n, (size_t)(std::max(need_s, mfsk_s) * RATE));
		const float *recent = x + (n - use);
		double so = olivia >= 0 ? olivia_sync(recent, use, c.center, sig, false) : 0;
		double sc = contestia >= 0 ? olivia_sync(recent, use, c.center, sig, true) : 0;
		// "No lock" picks the MFSK row, but only settles a group with one:
		// MFSK64 and MFSK64L are the same on the air.
		int mfsk = group_row(g, is_mfsk);
		if (detail) {
			snprintf(buf, sizeof(buf), "Olivia sync %.1f, Contestia sync %.1f", so, sc);
			*detail = buf;
		}
		// The S/N the receiver reports for its own FEC sync. A real
		// signal of the other kind, or MFSK, still gives 3.5 to 4.7, so a
		// clear lead decides. 4-tone Contestia locks at about 4.7, but the
		// groups shared with MFSK are 8 and 16 tones, whose real locks are
		// 6 and up: there a lock needs 5, and neither reaching 4.6 is MFSK.
		double sync = group_count(g, is_mfsk) > 0 ? 5.0 : 4.0;
		if (so >= sync && so > 1.5 * sc && olivia >= 0)
			return olivia;
		if (sc >= sync && sc > 1.5 * so && contestia >= 0)
			return contestia;
		// No lock only means MFSK once a lock has had time to build:
		// about eight blocks, or nearly all the audio kept. Below about 20
		// baud (Contestia 16/250 against MFSK16) the receiver can take
		// longer than the audio kept to lock, so no lock proves nothing.
		if (mfsk >= 0 && sig.baud >= 20 && so < 4.6 && sc < 4.6
		    && n >= (size_t)(mfsk_s * RATE)) {
			settled = group_count(g, is_mfsk) == 1;
			return mfsk;
		}
		return -1;
	}

	int dominoex = group_row(g, is_dominoex), thor = group_row(g, is_thor);
	if (dominoex >= 0 && thor >= 0) {
		// Decode the symbols both ways: THOR through its own interleaver,
		// Viterbi decoder and varicode, DominoEX straight through its
		// varicode. The right decoder gives clean text; the wrong one gives
		// junk with control characters in it.
		std::vector<int> steps, dchars, tchars;
		double clean = read_ifk(x, n, c.center, signatures[thor], steps);
		// THOR Micro interleaves 4 deep, the other speeds here 10.
		int depth = signatures[thor].baud < 3 ? 4 : 10;
		// 2-baud Micro gives only about 40 steps in the audio kept.
		bool slow = signatures[thor].baud < 3;
		if (clean < 0.45 || steps.size() < (slow ? 30u : 60u)) {
			if (detail) {
				snprintf(buf, sizeof(buf), clean < 0.45 ? "tones too weak (%.2f)"
					: "too few symbols yet (%.0f)", clean < 0.45 ? clean : (double)steps.size());
				*detail = buf;
			}
			return -1;
		}
		thor_confidence(steps, depth, tchars);
		dominoex_decode(steps, dchars);
		double ts = tchars.size() >= 15 ? text_share(tchars) : 0;
		double ds = dchars.size() >= 15 ? text_share(dchars) : 0;
		if (detail) {
			snprintf(buf, sizeof(buf), "tones %.2f, text as THOR %.2f (%d), as DominoEX %.2f (%d)",
				clean, ts, (int)tchars.size(), ds, (int)dchars.size());
			*detail = buf;
		}
		// THOR's decoder needs its interleaver full before it says much:
		// fewer than 15 characters is no evidence either way.
		if (tchars.size() >= 15 && ts >= 0.8 && ts >= ds)
			return thor;
		if (tchars.size() >= 15 && ds >= 0.97 && ds - ts >= 0.15)
			return dominoex;
		// Slow speeds may not fill THOR's interleaver in the audio kept.
		// DominoEX's own decode then decides alone: real DominoEX reads
		// almost perfectly, THOR read as DominoEX never above about 0.9.
		// (Micro never gets enough characters for this in the audio kept;
		// it stays a look-alike pair.)
		if (tchars.size() < 15 && dchars.size() >= 30 && clean >= 0.85) {
			if (ds >= 0.97)
				return dominoex;
			if (ds <= 0.88)
				return thor;
		}
		return -1;
	}

	int plain = group_row(g, is_plain_bpsk), fec = group_row(g, is_bpsk_fec);
	if (plain >= 0 && fec >= 0) {
		double quality = 0;
		double share = varicode_share(x, n, c.center, signatures[plain], quality, detail);
		if (share < 0)
			return -1;
		if (share >= 0.8)
			return plain;
		if (share <= 0.65 && quality >= 0.85)
			return fec;
		return -1;
	}
	return -1;
}

} // namespace sigid
