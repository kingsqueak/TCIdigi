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

// Phase 0: the interface only. Segmentation and classification come in
// later phases; see SIGID-PLAN.md.

#include <config.h>

#include "sigid.h"

SigId::SigId(hit_callback cb, void *arg)
	: callback(cb), callback_arg(arg), seen(0)
{
}

SigId::~SigId()
{
}

void SigId::reset()
{
	seen = 0;
}

void SigId::receive(const float *buf, size_t len, int samplerate)
{
	if (!buf || !len || samplerate <= 0)
		return;
	seen += (unsigned long long)len * analysis_rate / samplerate;
}
