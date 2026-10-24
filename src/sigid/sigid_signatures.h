// ----------------------------------------------------------------------------
//
//	sigid_signatures.h -- what each known signal looks like
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

// One row per signal SigID can name. Adding a mode is adding a row; a mode
// of a new kind also needs a feature extractor that measures it.

#ifndef SIGID_SIGNATURES_H
#define SIGID_SIGNATURES_H

#include "globals.h"

namespace sigid {

enum family {
	// One keyed carrier: CW, SCAMP OOK.
	FAM_OOK,
	// Hell sent as on-off keying: Feld, Slow, X5, X9.
	FAM_HELL_OOK,
	// Two-tone FSK: RTTY, NAVTEX, SITOR-B, SCAMP FSK, FSK Hell.
	FAM_FSK2,
	// One tone at a time from N, chosen freely: MFSK.
	FAM_MFSK,
	// The same, but each shaped tone lasts two symbol periods, so two
	// overlap at any moment: Olivia, Contestia.
	FAM_OLIVIA,
	// One tone at a time from N, each relative to the last (incremental
	// frequency keying): DominoEX, THOR, FSQ, IFKP.
	FAM_IFK,
	// Two tones at once from a small set: Throb, ThrobX.
	FAM_THROB,
	// One phase-shift-keyed carrier: BPSK, QPSK, 8PSK, PSK-R.
	FAM_PSK,
	// Several phase-shift-keyed carriers side by side: the C and RC modes.
	FAM_PSK_MULTI,
	// MT63: 64 carriers, all on at once.
	FAM_MT63,
	FAM_COUNT
};

struct signature {
	// The modem to switch to, or NUM_MODES for a signal with no modem here.
	trx_mode mode;
	// fldigi's short mode name (mode_info[].sname) or, without a modem, a
	// display name.
	const char *name;
	family fam;
	// Tones for FSK2, MFSK, IFK and Throb; carriers for PSK_MULTI and MT63;
	// 1 otherwise.
	int tones;
	// Distance between neighbouring tones or carriers, Hz. 0 for one.
	double spacing;
	// Symbols per second. Hell: columns per second. CW: dots per second at
	// a typical 20 WPM.
	double baud;
	// fldigi's get_bandwidth(), Hz.
	double bandwidth;
	// Phases for PSK families (2, 4 or 8); 0 otherwise.
	int order;
	// Rows that look the same on the air share a group number; 0 for none.
	// Phase 3 tells the members of a group apart.
	int group;
};

extern const signature signatures[];
extern const int signature_count;

} // namespace sigid

#endif // SIGID_SIGNATURES_H
