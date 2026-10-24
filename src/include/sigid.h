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

class SigId {
public:
	typedef void (*hit_callback)(const sigid_hit &hit, void *arg);

	SigId(hit_callback cb = 0, void *arg = 0);
	~SigId();

	// Forget every candidate, as after a band change.
	void reset();
	// Mono audio at any sample rate. Converted to the analysis rate inside.
	void receive(const float *buf, size_t len, int samplerate);

	// Samples taken in at the analysis rate, for the test tool.
	unsigned long long samples_seen() const { return seen; }

	static const int analysis_rate = 8000;

private:
	hit_callback callback;
	void *callback_arg;
	unsigned long long seen;

	SigId(const SigId &);
	SigId &operator=(const SigId &);
};

#endif // SIGID_H
