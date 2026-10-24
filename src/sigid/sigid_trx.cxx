// ----------------------------------------------------------------------------
//
//	sigid_trx.cxx -- SigID in the receiver
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

// The only SigID file that uses fldigi's configuration, modems and
// threads. The trx thread feeds audio here; once a second the signals
// SigID has named are checked, and each new one gets a clickable mark in
// the receive pane (note_sigid, on the GUI thread). The event log also gets
// the list whenever it changes.

#include <config.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "configuration.h"
#include "debug.h"
#include "modem.h"
#include "qrunner.h"
#include "sigid.h"
#include "trx.h"

LOG_FILE_SOURCE(debug::LOG_MODEM);

SigId *SigID = 0;

// Set by the GUI when SigID is switched on, acted on by the trx thread,
// which owns SigID: start from a clean slate.
static std::atomic<bool> reset_pending(false);

// Log at most this often, and only when the list has changed.
static const double LOG_EVERY_S = 5.0;
static double last_log_s = -LOG_EVERY_S;
static std::string last_logged;

// Look for new signals to mark this often.
static const double MARK_EVERY_S = 1.0;
static double last_mark_s = -MARK_EVERY_S;

// A mark already written: which track, what it said, where and when.
struct marked {
	int id;
	std::string label;
	double freq;
	double bandwidth;
	double when;
};
static std::vector<marked> marks;

static void forget()
{
	last_log_s = -LOG_EVERY_S;
	last_logged.clear();
	last_mark_s = -MARK_EVERY_S;
	marks.clear();
}

void sigid_start()
{
	if (!SigID)
		SigID = new SigId;
	SigID->reset();
	forget();
}

void sigid_stop()
{
	delete SigID;
	SigID = 0;
}

void sigid_enable(bool on)
{
	progdefaults.sigid = on;
	if (on)
		reset_pending = true;
	progdefaults.changed = true;
}

// The label a mark shows: the mode, or both look-alikes while undecided.
static std::string label_of(const sigid_candidate &c)
{
	std::string label = c.name;
	if (c.ambiguous && *c.alternate_name)
		label += std::string(" or ") + c.alternate_name;
	return label;
}

static void mark_new_signals(double now)
{
	sigid_candidate c[32];
	size_t n = std::min(SigID->candidates(c, 32), (size_t)32);
	// Forget marks old enough to be repeated.
	for (size_t i = marks.size(); i-- > 0;)
		if (now - marks[i].when > progdefaults.sigid_repeat_s)
			marks.erase(marks.begin() + i);

	for (size_t i = 0; i < n; i++) {
		// Only answers that have held for a few passes: a new or changing
		// signal is often named wrongly at first.
		if (!*c[i].name || c[i].mode >= NUM_MODES || !c[i].stable)
			continue;
		if (100 * c[i].confidence < progdefaults.sigid_min_conf)
			continue;
		if (c[i].ambiguous && !progdefaults.sigid_ambiguous)
			continue;
		double bw = c[i].hi - c[i].lo;
		double near = std::max(25.0, bw / 2);
		// Already being received, as the mode or (while undecided) as its
		// look-alike: nothing to point at.
		if (active_modem && fabs(active_modem->get_freq() - c[i].center) < near) {
			trx_mode now = active_modem->get_mode();
			if (now == c[i].mode || (c[i].ambiguous && now == c[i].alternate))
				continue;
		}
		std::string label = label_of(c[i]);
		bool seen = false;
		for (size_t k = 0; k < marks.size(); k++) {
			marked &m = marks[k];
			// The same track saying the same thing, or the same mode
			// marked here recently (a track that came back).
			if ((m.id == c[i].id && m.label == label)
			    || (m.label == label && fabs(m.freq - c[i].center) < std::max(near, m.bandwidth / 2))) {
				seen = true;
				break;
			}
		}
		if (seen)
			continue;
		marked m = { c[i].id, label, c[i].center, bw, now };
		marks.push_back(m);
		REQ(note_sigid, c[i].mode, label, c[i].center, (int)floor(100 * c[i].confidence + 0.5));
	}
}

static void log_changes(double now)
{
	sigid_candidate c[32];
	size_t n = std::min(SigID->candidates(c, 32), (size_t)32);
	std::string text;
	char one[96];
	for (size_t i = 0; i < n; i++) {
		// Round so small wobbles do not count as a change.
		snprintf(one, sizeof(one), " %s %.0f", c[i].name, (c[i].lo + c[i].hi) / 20);
		text += one;
	}
	if (text == last_logged)
		return;
	last_logged = text;
	text.clear();
	for (size_t i = 0; i < n; i++) {
		std::string name = *c[i].name ? c[i].name : "?";
		if (c[i].ambiguous && *c[i].alternate_name)
			name += std::string("/") + c[i].alternate_name;
		snprintf(one, sizeof(one), " [%s %.0f Hz, %.0f wide, %+.0f dB, %.0f%%]",
			name.c_str(), c[i].center, c[i].hi - c[i].lo,
			c[i].snr, 100 * c[i].confidence);
		text += one;
	}
	LOG_INFO("SigID: %d signal%s%s", (int)n, n == 1 ? "" : "s", text.c_str());
}

void sigid_receive(const float *buf, size_t len, int samplerate)
{
	if (!SigID)
		return;
	if (reset_pending) {
		reset_pending = false;
		SigID->reset();
		forget();
	}
	SigID->receive(buf, len, samplerate);

	double now = (double)SigID->samples_seen() / SigId::analysis_rate;
	if (progdefaults.sigid_marks && now - last_mark_s >= MARK_EVERY_S) {
		last_mark_s = now;
		mark_new_signals(now);
	}
	if (now - last_log_s >= LOG_EVERY_S) {
		last_log_s = now;
		log_changes(now);
	}
}
