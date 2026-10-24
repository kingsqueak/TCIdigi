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

// The only SigID file that uses fldigi's configuration and logging. The trx
// thread feeds audio here. Until classification arrives, the signals found
// are written to the event log when they change.

#include <config.h>

#include <cstdio>
#include <string>

#include "debug.h"
#include "sigid.h"

LOG_FILE_SOURCE(debug::LOG_MODEM);

SigId *SigID = 0;

// Log at most this often, and only when the list has changed.
static const double LOG_EVERY_S = 5.0;
static double last_log_s = -LOG_EVERY_S;
static std::string last_logged;

void sigid_start()
{
	if (!SigID)
		SigID = new SigId;
	SigID->reset();
	last_log_s = -LOG_EVERY_S;
	last_logged.clear();
}

void sigid_stop()
{
	delete SigID;
	SigID = 0;
}

void sigid_receive(const float *buf, size_t len, int samplerate)
{
	if (!SigID)
		return;
	SigID->receive(buf, len, samplerate);

	double now = (double)SigID->samples_seen() / SigId::analysis_rate;
	if (now - last_log_s < LOG_EVERY_S)
		return;
	last_log_s = now;

	sigid_candidate c[32];
	size_t n = SigID->candidates(c, 32);
	std::string text;
	char one[64];
	for (size_t i = 0; i < n && i < 32; i++) {
		// Round so small wobbles do not count as a change.
		snprintf(one, sizeof(one), " %.0f-%.0f Hz", c[i].lo / 10, c[i].hi / 10);
		text += one;
	}
	if (text == last_logged)
		return;
	last_logged = text;
	text.clear();
	for (size_t i = 0; i < n && i < 32; i++) {
		snprintf(one, sizeof(one), " [%.0f Hz, %.0f wide, %+.0f dB]",
			c[i].center, c[i].hi - c[i].lo, c[i].snr);
		text += one;
	}
	LOG_INFO("SigID: %d signal%s%s", (int)n, n == 1 ? "" : "s", text.c_str());
}
