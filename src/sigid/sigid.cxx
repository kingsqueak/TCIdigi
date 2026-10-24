// ----------------------------------------------------------------------------
//
//	sigid.cxx -- automatic signal identification
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

// Audio comes in at the modem's rate and is converted to 8 kHz. Two
// spectra are kept: a fine one, averaged over about 1.5 s, to find where
// signals sit, and a coarse, fast one to see how their parts move in time.
// Every quarter second the fine spectrum is split into signals and those
// are matched to the candidates already being tracked. Each candidate is
// scored against the signature table once it is a few seconds old and
// every couple of seconds after that, from the last stretch of audio; the
// scores are averaged over time and the best row is its mode.

#include <config.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

#include <samplerate.h>

#include "gfft.h"
#include "sigid.h"
#include "sigid_internal.h"
#include "sigid_signatures.h"

using namespace sigid;

// Averaging time of the fine spectrum.
static const double AVERAGE_S = 1.5;
// Long frames between segmentations: 256 ms.
static const int SEGMENT_EVERY = 8;
// A candidate is reported once it has lasted this long, and dropped once
// it has been missing this long.
static const double REPORT_AFTER_S = 1.0;
static const double DROP_AFTER_S = 3.0;
// Audio kept for classification, and how much of it one pass uses.
static const int KEEP_S = 20;
static const double CLASSIFY_MAX_S = 10.0;
// Narrow signals are often slow (Throb, MFSK4, THOR Micro): give them all
// the audio kept, for enough symbols. Wide ones are fast and costly to
// test: a shorter stretch holds plenty of symbols.
static const double NARROW_HZ = 150.0;
static const double WIDE_HZ = 1000.0;
static const double WIDE_MAX_S = 6.0;
// First classification at this age, then this often.
static const double CLASSIFY_AFTER_S = 3.0;
static const double CLASSIFY_EVERY_S = 3.0;
// Once the same answer has come back this many times with at least this
// confidence, check it only this often.
static const int SETTLED_AFTER = 3;
static const double SETTLED_CONFIDENCE = 0.3;
static const double SETTLED_EVERY_S = 8.0;
// Weight of a new pass in the running score.
static const double VOTE_WEIGHT = 0.4;
// Look-alike checks decode a little of the signal, so they run less often:
// again this soon while undecided, this rarely once decided.
static const double VERIFY_RETRY_S = 4.0;
static const double VERIFY_EVERY_S = 12.0;
static const double VERIFY_SKIP_S = 2.0;

struct track {
	int id;
	double lo_hz;
	double hi_hz;
	double snr;
	double first_s;
	double last_s;
	int parts;
	double next_classify_s;
	std::vector<double> votes;
	classification cls;
	int same_answers;
	// Row the look-alike check picked (-1: none yet) and when to check
	// again.
	int verified;
	bool verified_settled;
	int verified_group;
	double next_verify_s;
};

struct sigid_impl {
	SRC_STATE *src;
	int src_rate;
	std::vector<float> src_in;
	std::vector<float> src_out;

	// Samples at 8 kHz not yet used by the two FFTs.
	std::vector<double> pending;
	size_t long_at;
	size_t short_at;
	unsigned long long seen;

	g_fft<double> long_fft;
	g_fft<double> short_fft;
	std::vector<double> long_win;
	std::vector<double> short_win;
	std::vector<std::complex<double> > buf;

	std::vector<double> avg;
	bool avg_started;
	int frames_since_segment;
	short_history history;

	std::vector<track> tracks;
	int next_id;

	// Last KEEP_S seconds of 8 kHz audio.
	std::vector<float> ring;
	size_t ring_at;
	size_t ring_have;

	sigid_impl()
		: src(0), src_rate(0), long_at(0), short_at(0), seen(0),
		  long_fft(LONG_N), short_fft(SHORT_N),
		  long_win(LONG_N), short_win(SHORT_N), buf(LONG_N),
		  avg(LONG_BINS, 0.0), avg_started(false), frames_since_segment(0),
		  next_id(1), ring(KEEP_S * RATE, 0.0f), ring_at(0), ring_have(0)
	{
		for (int i = 0; i < LONG_N; i++)
			long_win[i] = 0.5 - 0.5 * cos(2 * M_PI * i / (LONG_N - 1));
		for (int i = 0; i < SHORT_N; i++)
			short_win[i] = 0.5 - 0.5 * cos(2 * M_PI * i / (SHORT_N - 1));
	}

	~sigid_impl()
	{
		if (src)
			src_delete(src);
	}

	double now_s() const { return (double)seen / RATE; }

	void reset()
	{
		pending.clear();
		long_at = short_at = 0;
		seen = 0;
		std::fill(avg.begin(), avg.end(), 0.0);
		avg_started = false;
		frames_since_segment = 0;
		history.clear();
		tracks.clear();
		ring_at = ring_have = 0;
		if (src)
			src_reset(src);
	}

	void power_spectrum(g_fft<double> &fft, const std::vector<double> &win,
	                    size_t at, int n, double *out)
	{
		for (int i = 0; i < n; i++)
			buf[i] = std::complex<double>(pending[at + i] * win[i], 0.0);
		fft.ComplexFFT(&buf[0]);
		for (int k = 0; k < n / 2; k++)
			out[k] = std::norm(buf[k]);
	}

	// Takes audio a hop at a time, so the clock, the kept audio and every
	// segmentation inside one large block see only what the FFTs have
	// reached, whatever the caller's block size.
	void add(const double *x, size_t n)
	{
		while (n > 0) {
			size_t part = std::min(n, (size_t)LONG_HOP);
			add_part(x, part);
			x += part;
			n -= part;
		}
	}

	void add_part(const double *x, size_t n)
	{
		for (size_t i = 0; i < n; i++) {
			ring[ring_at] = (float)x[i];
			ring_at = (ring_at + 1) % ring.size();
		}
		ring_have = std::min(ring.size(), ring_have + n);
		pending.insert(pending.end(), x, x + n);
		seen += n;
		double p[LONG_BINS];
		while (short_at + SHORT_N <= pending.size()) {
			power_spectrum(short_fft, short_win, short_at, SHORT_N, p);
			history.push(p);
			short_at += SHORT_HOP;
		}
		while (long_at + LONG_N <= pending.size()) {
			power_spectrum(long_fft, long_win, long_at, LONG_N, p);
			long_frame(p);
			long_at += LONG_HOP;
		}
		size_t used = std::min(long_at, short_at);
		if (used > 0) {
			pending.erase(pending.begin(), pending.begin() + used);
			long_at -= used;
			short_at -= used;
		}
	}

	void long_frame(const double *p)
	{
		const double alpha = (double)LONG_HOP / (AVERAGE_S * RATE);
		if (!avg_started) {
			std::copy(p, p + LONG_BINS, avg.begin());
			avg_started = true;
		} else {
			for (int k = 0; k < LONG_BINS; k++)
				avg[k] += alpha * (p[k] - avg[k]);
		}
		if (++frames_since_segment >= SEGMENT_EVERY) {
			frames_since_segment = 0;
			segment();
		}
	}

	void segment()
	{
		int kmin = std::max(1, (int)ceil(LO_HZ / BIN_HZ));
		int kmax = std::min(LONG_BINS - 1, (int)floor(HI_HZ / BIN_HZ));
		double pdb[LONG_BINS], floor_db[LONG_BINS], excess[LONG_BINS];
		for (int k = 0; k < LONG_BINS; k++)
			pdb[k] = 10.0 * log10(std::max(avg[k], 1e-20));
		noise_floor(pdb, kmin, kmax, floor_db);
		for (int k = kmin; k <= kmax; k++)
			excess[k] = pdb[k] - floor_db[k];
		std::vector<region> regions;
		find_regions(excess, kmin, kmax, regions);
		merge_regions(regions, history);
		update_tracks(regions);
		classify_tracks();
	}

	// The last n samples, oldest first.
	void recent(size_t n, std::vector<float> &out) const
	{
		n = std::min(n, ring_have);
		out.resize(n);
		size_t start = (ring_at + ring.size() - n) % ring.size();
		for (size_t i = 0; i < n; i++)
			out[i] = ring[(start + i) % ring.size()];
	}

	void classify_tracks()
	{
		double t = now_s();
		std::vector<float> audio;
		std::vector<double> scores;
		for (size_t i = 0; i < tracks.size(); i++) {
			track &tr = tracks[i];
			double age = tr.last_s - tr.first_s;
			if (tr.last_s < t || age < CLASSIFY_AFTER_S)
				continue;
			candidate_view c;
			c.lo = tr.lo_hz;
			c.hi = tr.hi_hz;
			c.center = (tr.lo_hz + tr.hi_hz) / 2;
			c.parts = tr.parts;
			if (t >= tr.next_classify_s)
				classify(tr, c, age, t, audio, scores);
			if (tr.cls.index >= 0)
				verify(tr, c, age, t);
		}
	}

	void classify(track &tr, const candidate_view &c, double age, double t,
	              std::vector<float> &audio, std::vector<double> &scores)
	{
		double width = tr.hi_hz - tr.lo_hz;
		double span = width < NARROW_HZ ? KEEP_S
			: width > WIDE_HZ ? WIDE_MAX_S : CLASSIFY_MAX_S;
		recent((size_t)(std::min(age, span) * RATE), audio);
		score_signatures(audio.empty() ? 0 : &audio[0], audio.size(), c, scores);
		if (tr.votes.size() != scores.size())
			tr.votes = scores;
		else
			for (size_t k = 0; k < scores.size(); k++)
				tr.votes[k] += VOTE_WEIGHT * (scores[k] - tr.votes[k]);
		int before = tr.cls.index;
		decide(tr.votes, tr.cls);
		// Settled means the same answer, clearly ahead and clearly a
		// match: a lone weak score has full confidence but proves little.
		if (tr.cls.index == before && tr.cls.confidence >= SETTLED_CONFIDENCE
		    && tr.cls.score >= SETTLED_CONFIDENCE)
			tr.same_answers++;
		else
			tr.same_answers = 0;
		// A pass that breaks the streak is followed up soon.
		bool settled = tr.same_answers >= SETTLED_AFTER;
		tr.next_classify_s = t + (settled ? SETTLED_EVERY_S : CLASSIFY_EVERY_S);
	}

	// A winner with look-alikes: decode a little to pick the member. Runs
	// on its own schedule: a new group at once, the same group again after
	// VERIFY_RETRY_S while undecided and VERIFY_EVERY_S once decided.
	void verify(track &tr, const candidate_view &c, double age, double t)
	{
		int best = tr.cls.index;
		int group = signatures[best].group;
		if (group != tr.verified_group) {
			tr.verified = -1;
			tr.verified_settled = false;
			tr.verified_group = group;
			tr.next_verify_s = t;
		}
		if (!group || t < tr.next_verify_s)
			return;
		// Decoding needs symbols: give it all the audio kept, less the
		// first seconds of the signal, where preambles (PSK-R's among
		// them) look unlike the rest.
		std::vector<float> longer;
		double usable = std::max(0.0, age - VERIFY_SKIP_S);
		recent((size_t)(std::min(usable, (double)KEEP_S) * RATE), longer);
		std::string detail;
		bool settled = true;
		int row = verify_group(longer.empty() ? 0 : &longer[0], longer.size(),
			c, best, settled, trace ? &detail : 0);
		if (trace)
			fprintf(stderr, "  verify %s at %.0f Hz: %s -> %s%s\n",
				signatures[best].name, c.center, detail.c_str(),
				row >= 0 ? signatures[row].name : "undecided",
				row >= 0 && !settled ? " (narrowed)" : "");
		// An inconclusive re-check keeps the last verdict but tries again
		// soon.
		if (row >= 0) {
			tr.verified = row;
			tr.verified_settled = settled;
		}
		tr.next_verify_s = t + (row >= 0 && settled ? VERIFY_EVERY_S : VERIFY_RETRY_S);
	}

	// A region continues the track it overlaps most, when they overlap by
	// more than half of the narrower one. Any other track it overlaps that
	// much was the same signal seen in pieces, and is dropped.
	void update_tracks(const std::vector<region> &regions)
	{
		double t = now_s();
		std::vector<char> retire(tracks.size(), 0);
		std::vector<track> born;
		for (size_t i = 0; i < regions.size(); i++) {
			const region &r = regions[i];
			int best = -1;
			double best_overlap = 0;
			for (size_t j = 0; j < tracks.size(); j++) {
				const track &tr = tracks[j];
				double ov = std::min(tr.hi_hz, r.hi_hz) - std::max(tr.lo_hz, r.lo_hz);
				double narrow = std::min(tr.hi_hz - tr.lo_hz, r.hi_hz - r.lo_hz);
				if (ov <= 0.5 * narrow)
					continue;
				if (best >= 0 && ov <= best_overlap) {
					retire[j] = 1;
					continue;
				}
				if (best >= 0)
					retire[best] = 1;
				best = (int)j;
				best_overlap = ov;
			}
			track tr = track();
			if (best >= 0) {
				tr = tracks[best];
				retire[best] = 0;
			} else {
				tr.id = next_id++;
				tr.first_s = t;
				tr.verified = -1;
				tr.verified_settled = false;
				tr.verified_group = 0;
			}
			tr.lo_hz = r.lo_hz;
			tr.hi_hz = r.hi_hz;
			tr.snr = r.excess_db;
			tr.parts = r.parts;
			tr.last_s = t;
			if (best >= 0)
				tracks[best] = tr;
			else
				born.push_back(tr);
		}
		std::vector<track> kept;
		for (size_t j = 0; j < tracks.size(); j++)
			if (!retire[j] && t - tracks[j].last_s <= DROP_AFTER_S)
				kept.push_back(tracks[j]);
		kept.insert(kept.end(), born.begin(), born.end());
		tracks.swap(kept);
	}
};

SigId::SigId(hit_callback cb, void *arg)
	: callback(cb), callback_arg(arg), impl(new sigid_impl)
{
}

SigId::~SigId()
{
	delete impl;
}

void SigId::reset()
{
	impl->reset();
}

unsigned long long SigId::samples_seen() const
{
	return impl->seen;
}

void SigId::receive(const float *buf, size_t len, int samplerate)
{
	if (!buf || !len || samplerate <= 0)
		return;
	std::vector<double> x;
	if (samplerate == RATE) {
		// Start a later change of rate with an empty converter.
		impl->src_rate = 0;
		impl->src_in.clear();
		x.assign(buf, buf + len);
	} else {
		if (!impl->src || impl->src_rate != samplerate) {
			if (impl->src)
				src_delete(impl->src);
			int err = 0;
			impl->src = src_new(SRC_SINC_BEST_QUALITY, 1, &err);
			if (!impl->src)
				return;
			impl->src_rate = samplerate;
			impl->src_in.clear();
		}
		// libsamplerate may take only part of the input in one call. What
		// it leaves waits in src_in for the next block. The best converter
		// passes almost up to 4 kHz; the fast one let 4-5 kHz noise fold
		// back over the top of the band.
		impl->src_in.insert(impl->src_in.end(), buf, buf + len);
		double ratio = (double)RATE / samplerate;
		size_t used = 0;
		while (used < impl->src_in.size()) {
			size_t left = impl->src_in.size() - used;
			impl->src_out.resize((size_t)(left * ratio) + 256);
			SRC_DATA d;
			d.data_in = &impl->src_in[used];
			d.input_frames = (long)left;
			d.data_out = &impl->src_out[0];
			d.output_frames = (long)impl->src_out.size();
			d.end_of_input = 0;
			d.src_ratio = ratio;
			if (src_process(impl->src, &d) != 0) {
				impl->src_in.clear();
				return;
			}
			x.insert(x.end(), impl->src_out.begin(),
				impl->src_out.begin() + d.output_frames_gen);
			used += (size_t)d.input_frames_used;
			if (d.input_frames_used == 0 && d.output_frames_gen == 0)
				break;
		}
		impl->src_in.erase(impl->src_in.begin(), impl->src_in.begin() + used);
	}
	if (!x.empty())
		impl->add(&x[0], x.size());
}

size_t SigId::top_matches(int id, const char **names, double *scores, size_t max) const
{
	for (size_t i = 0; i < impl->tracks.size(); i++) {
		const track &tr = impl->tracks[i];
		if (tr.id != id)
			continue;
		std::vector<int> order(tr.votes.size());
		for (size_t k = 0; k < order.size(); k++)
			order[k] = (int)k;
		std::sort(order.begin(), order.end(),
			[&tr](int a, int b) { return tr.votes[a] > tr.votes[b]; });
		size_t n = std::min(max, order.size());
		for (size_t k = 0; k < n; k++) {
			names[k] = signatures[order[k]].name;
			scores[k] = tr.votes[order[k]];
		}
		return n;
	}
	return 0;
}

size_t SigId::candidates(sigid_candidate *out, size_t max) const
{
	double t = impl->now_s();
	size_t n = 0;
	for (size_t i = 0; i < impl->tracks.size(); i++) {
		const track &tr = impl->tracks[i];
		if (tr.last_s - tr.first_s < REPORT_AFTER_S || t - tr.last_s > 0.5)
			continue;
		if (out && n < max) {
			sigid_candidate &c = out[n];
			c.id = tr.id;
			c.lo = tr.lo_hz;
			c.hi = tr.hi_hz;
			c.center = (tr.lo_hz + tr.hi_hz) / 2;
			c.snr = tr.snr;
			c.age = tr.last_s - tr.first_s;
			c.parts = tr.parts;
			const classification &k = tr.cls;
			// The look-alike check, when it has spoken for this group, picks
			// the member; the spectral winner is then the alternate.
			int row = k.index, alt = k.alternate;
			bool checked = row >= 0 && tr.verified >= 0
				&& signatures[tr.verified].group == signatures[row].group;
			if (checked && tr.verified != row) {
				alt = row;
				row = tr.verified;
			}
			// Narrowed but not settled (MFSK64 or MFSK64L): the alternate
			// is the other member of the same family.
			if (checked && !tr.verified_settled)
				for (int j = 0; j < signature_count; j++)
					if (j != row && signatures[j].group == signatures[row].group
					    && signatures[j].fam == signatures[row].fam) {
						alt = j;
						break;
					}
			c.mode = row >= 0 ? signatures[row].mode : NUM_MODES;
			c.name = row >= 0 ? signatures[row].name : "";
			c.confidence = row >= 0 ? k.confidence : 0;
			c.alternate = alt >= 0 ? signatures[alt].mode : NUM_MODES;
			c.alternate_name = alt >= 0 ? signatures[alt].name : "";
			c.ambiguous = row >= 0 && signatures[row].group
				&& !(checked && tr.verified_settled);
		}
		n++;
	}
	return n;
}
