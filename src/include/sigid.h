// ----------------------------------------------------------------------------
//
//	sigid.h -- automatic signal identification
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

// SigID finds signals in the receive passband and names their mode without
// a TXID or RsID. The core has no FLTK or Qt in it: it takes audio and
// reports hits through a callback, so the receiver, the user interface and
// the offline test tool (sigid-scan) all use the same code.

#ifndef SIGID_H
#define SIGID_H

#include <stddef.h>

#include <string>

#include "globals.h"

struct sigid_hit {
	// The modem to switch to, or NUM_MODES for a signal with no modem here.
	trx_mode mode;
	// Signature name, for display. Never null.
	const char *name;
	// Center of the signal's energy in the audio passband, Hz. For every
	// native modem this is the value get_freq() wants.
	double freq;
	double bandwidth;
	// Estimated SNR in the signal's own bandwidth, dB.
	double snr;
	// 0 to 1.
	double confidence;
	// The runner-up when it scored close to the winner, else NUM_MODES.
	trx_mode alternate;
};

// One occupied part of the passband that has lasted long enough to be a
// signal. Phase 1 output; classification turns these into hits.
struct sigid_candidate {
	int id;
	// Edges and center of the occupied band, Hz.
	double lo;
	double hi;
	double center;
	// Mean power over the band relative to the noise floor there, dB.
	double snr;
	// Seconds since first seen.
	double age;
	// How many separate spectral pieces were joined to make it: FSK and
	// MFSK tones, or the carriers of a multi-carrier PSK mode.
	int parts;
	// Best match so far: the modem, or NUM_MODES when there is none yet;
	// its name ("" when none); how far it leads, 0..1; and the best other
	// member of its look-alike group (NUM_MODES when none).
	trx_mode mode;
	const char *name;
	double confidence;
	trx_mode alternate;
	// 1 while the mode is one of a look-alike group (THOR / DominoEX,
	// Olivia / Contestia, ...) that decoding has not yet told apart: show
	// it as "mode or alternate".
	int ambiguous;
	const char *alternate_name;
	// 1 once the same mode has come back from several classification
	// passes in a row: worth showing to the operator.
	int stable;
};

struct sigid_impl;

class SigId {
public:
	typedef void (*hit_callback)(const sigid_hit &hit, void *arg);

	SigId(hit_callback cb = 0, void *arg = 0);
	~SigId();

	// Forget every candidate, as after a band change.
	void reset();
	// Mono audio at any sample rate. Converted to the analysis rate inside.
	void receive(const float *buf, size_t len, int samplerate);

	// Copies up to max current candidates into out and returns how many
	// there are in all.
	size_t candidates(sigid_candidate *out, size_t max) const;

	// The best-scoring signatures for candidate id, highest first: up to
	// max names and scores. Returns how many were written.
	size_t top_matches(int id, const char **names, double *scores, size_t max) const;

	// Samples taken in at the analysis rate, for the test tool.
	unsigned long long samples_seen() const;

	static const int analysis_rate = 8000;

private:
	hit_callback callback;
	void *callback_arg;
	sigid_impl *impl;

	SigId(const SigId &);
	SigId &operator=(const SigId &);
};

// The receiver's SigID, fed from the trx loop when progdefaults.sigid is
// on. Defined in sigid_trx.cxx, which is not part of the offline tool.
extern SigId *SigID;
void sigid_start();
void sigid_stop();
void sigid_receive(const float *buf, size_t len, int samplerate);
// Switches SigID on or off from the GUI; on starts it from a clean slate.
void sigid_enable(bool on);
// Writes a clickable mark for a signal SigID named into the receive pane.
// GUI thread; defined in fl_digi.cxx. Clicking it switches to mode and
// moves to the signal, so that it sits at the SigID center frequency when
// that option is on and the rig can be tuned.
void note_sigid(trx_mode mode, std::string label, double audio_freq, int confidence);

#endif // SIGID_H
