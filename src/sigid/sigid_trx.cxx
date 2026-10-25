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
// the receive pane (note_sigid, on the GUI thread). The same signals are
// published as waterfall labels, which the GUI reads with sigid_labels().
// The event log also gets the list whenever it changes.

#include <config.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
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

// The waterfall labels, rebuilt on the trx thread and read by the GUI.
// Older than LABELS_STALE_S means the receiver has stopped feeding SigID.
static std::mutex labels_mutex;
static std::vector<sigid_label> labels;
static std::chrono::steady_clock::time_point labels_time;
static const double LABELS_STALE_S = 3.0;

static void publish_labels(const std::vector<sigid_label> &now)
{
	std::lock_guard<std::mutex> lock(labels_mutex);
	labels = now;
	labels_time = std::chrono::steady_clock::now();
}

size_t sigid_labels(sigid_label *out, size_t max)
{
	if (!progdefaults.sigid || !progdefaults.sigid_wf_labels)
		return 0;
	std::lock_guard<std::mutex> lock(labels_mutex);
	std::chrono::duration<double> age = std::chrono::steady_clock::now() - labels_time;
	if (age.count() > LABELS_STALE_S)
		return 0;
	size_t n = std::min(max, labels.size());
	for (size_t i = 0; i < n; i++)
		out[i] = labels[i];
	return n;
}

static void forget()
{
	last_log_s = -LOG_EVERY_S;
	last_logged.clear();
	last_mark_s = -MARK_EVERY_S;
	marks.clear();
	publish_labels(std::vector<sigid_label>());
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

// Whether a candidate is worth showing, as a mark or a label: an answer
// that has held for a few passes (a new or changing signal is often named
// wrongly at first), confident enough, and not a signal already being
// received in that mode or (while undecided) its look-alike.
static bool worth_showing(const sigid_candidate &c)
{
	if (!*c.name || c.mode >= NUM_MODES || !c.stable)
		return false;
	if (100 * c.confidence < progdefaults.sigid_min_conf)
		return false;
	if (c.ambiguous && !progdefaults.sigid_ambiguous)
		return false;
	double near = std::max(25.0, (c.hi - c.lo) / 2);
	if (active_modem && fabs(active_modem->get_freq() - c.center) < near) {
		trx_mode now = active_modem->get_mode();
		if (now == c.mode || (c.ambiguous && now == c.alternate))
			return false;
	}
	return true;
}

static void update_labels(const sigid_candidate *c, size_t n)
{
	std::vector<sigid_label> now;
	if (progdefaults.sigid_wf_labels) {
		for (size_t i = 0; i < n; i++) {
			if (!worth_showing(c[i]))
				continue;
			sigid_label l;
			l.mode = c[i].mode;
			l.lo = c[i].lo;
			l.hi = c[i].hi;
			l.center = c[i].center;
			// Short: "THOR22", or "THOR22/DOMX22" while undecided.
			std::string text = c[i].name;
			if (c[i].ambiguous && *c[i].alternate_name)
				text += std::string("/") + c[i].alternate_name;
			snprintf(l.text, sizeof(l.text), "%s", text.c_str());
			now.push_back(l);
		}
	}
	publish_labels(now);
}

static void mark_new_signals(const sigid_candidate *c, size_t n, double now)
{
	// Forget marks old enough to be repeated.
	for (size_t i = marks.size(); i-- > 0;)
		if (now - marks[i].when > progdefaults.sigid_repeat_s)
			marks.erase(marks.begin() + i);

	for (size_t i = 0; i < n; i++) {
		if (!worth_showing(c[i]))
			continue;
		double bw = c[i].hi - c[i].lo;
		double near = std::max(25.0, bw / 2);
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
	if (now - last_mark_s >= MARK_EVERY_S) {
		last_mark_s = now;
		sigid_candidate c[32];
		size_t n = std::min(SigID->candidates(c, 32), (size_t)32);
		update_labels(c, n);
		if (progdefaults.sigid_marks)
			mark_new_signals(c, n, now);
	}
	if (now - last_log_s >= LOG_EVERY_S) {
		last_log_s = now;
		log_changes(now);
	}
}
