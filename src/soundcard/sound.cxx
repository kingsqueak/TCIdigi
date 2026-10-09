// ----------------------------------------------------------------------------
//
//	sound.cxx
//
// Copyright (C) 2006-2013
//			Dave Freese, W1HKJ
//
// Copyright (C) 2007-2010
//			Stelios Bounanos, M0GLD
//
// Modified 2026-10-08: SoundNull only. Device enumeration, file capture,
// and alert playback have been removed. libsamplerate is still selected
// here because RSID uses progdefaults.sample_converter.
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

#include <cmath>
#include <cstring>

#include <samplerate.h>

#include "sound.h"
#include "soundconf.h"
#include "configuration.h"
#include "confdialog.h"
#include "trx.h"
#include "macros.h"
#include "misc.h"
#include "debug.h"

LOG_FILE_SOURCE(debug::LOG_AUDIO);

SoundBase::SoundBase()
	: sample_frequency(8000)
{
}

SoundBase::~SoundBase()
{
}

size_t SoundNull::Write(double*, size_t count)
{
	if (PERFORM_CPS_TEST || active_modem->XMLRPC_CPS_TEST)
		return count;
	if (sample_frequency > 0)
		MilliSleep((long)ceil((1e3 * count) / sample_frequency));
	return count;
}

size_t SoundNull::Write_stereo(double*, double*, size_t count)
{
	if (sample_frequency > 0)
		MilliSleep((long)ceil((1e3 * count) / sample_frequency));
	return count ? count : 1;
}

size_t SoundNull::Read(float *buf, size_t count)
{
	memset(buf, 0, count * sizeof(*buf));
	if (!bHighSpeed && sample_frequency > 0)
		MilliSleep((long)ceil((1e3 * count) / sample_frequency));
	return count;
}

void SoundNull::flush(unsigned)
{
}

int sample_rate_converters[FLDIGI_NUM_SRC] = {
	SRC_SINC_BEST_QUALITY,
	SRC_SINC_MEDIUM_QUALITY,
	SRC_SINC_FASTEST
#if !(defined(__ppc__) || defined(__powerpc__) || defined(__PPC__))
	, SRC_LINEAR
#endif
};

void sound_init(void)
{
	if (!menuSampleConverter)
		return;

	for (int i = 0; i < FLDIGI_NUM_SRC; i++)
		menuSampleConverter->add(src_get_name(sample_rate_converters[i]));

	if (progdefaults.sample_converter == SRC_ZERO_ORDER_HOLD) {
		progdefaults.sample_converter = SRC_LINEAR;
		LOG_WARN("The Zero Order Hold sample rate converter should not be used. "
			 "The setting has been changed to Linear.");
	}
#if defined(__ppc__) || defined(__powerpc__) || defined(__PPC__)
	if (progdefaults.sample_converter == SRC_LINEAR) {
		progdefaults.sample_converter = SRC_SINC_FASTEST;
		LOG_WARN("Linear sample rate converter may not work on this architecture. "
			 "The setting has been changed to Fastest Sinc.");
	}
#endif
	for (int i = 0; i < FLDIGI_NUM_SRC; i++) {
		if (sample_rate_converters[i] == progdefaults.sample_converter) {
			menuSampleConverter->index(i);
			menuSampleConverter->tooltip(src_get_description(progdefaults.sample_converter));
			break;
		}
	}
}

void sound_close(void)
{
}

void sound_update(unsigned)
{
}

int pa_set_dev(Fl_Choice *, std::string, int)
{
	return PA_DEV_NOT_FOUND;
}

void reset_audio_alerts()
{
}
