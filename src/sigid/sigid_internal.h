// ----------------------------------------------------------------------------
//
//	sigid_internal.h -- SigID internals shared by the core's source files
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

#ifndef SIGID_INTERNAL_H
#define SIGID_INTERNAL_H

#include <vector>

namespace sigid {

const int RATE = 8000;

// Spectrum for finding signals: 7.8 Hz bins, a new frame every 32 ms.
const int LONG_N = 1024;
const int LONG_HOP = 256;
const int LONG_BINS = LONG_N / 2;
const double BIN_HZ = (double)RATE / LONG_N;

// Spectrum for timing: 31 Hz bins every 16 ms, short enough to see FSK
// and MFSK tones take turns.
const int SHORT_N = 256;
const int SHORT_HOP = 128;
const int SHORT_BINS = SHORT_N / 2;
const double SHORT_BIN_HZ = (double)RATE / SHORT_N;
// Short frames kept for timing, about 2 s.
const int HISTORY = 128;

// Part of the passband searched.
const double LO_HZ = 100.0;
const double HI_HZ = 3900.0;

// A run of long-FFT bins above the noise floor. lo and hi are inclusive
// bin numbers; lo_hz and hi_hz are interpolated edges.
struct region {
	int lo;
	int hi;
	double lo_hz;
	double hi_hz;
	// Mean power over the run relative to the floor, dB.
	double excess_db;
	// Pieces joined into this one, and the width and spacing of those
	// pieces when they form a comb of carriers.
	int parts;
	double unit_hz;
	double pitch_hz;
	double last_center_hz;
};

// Ring of short-FFT power spectra.
class short_history {
public:
	short_history();
	void push(const double *power);
	void clear();
	int frames() const { return count; }
	// Power in [lo_hz, hi_hz] for each kept frame, oldest first. A band
	// narrower than one bin takes the nearest bin.
	void band_series(double lo_hz, double hi_hz, std::vector<double> &out) const;

private:
	std::vector<double> data;
	int head;
	int count;
};

// pdb: averaged power in dB per long-FFT bin. Fills floor_db for bins
// kmin..kmax with a noise floor that follows the passband's shape.
void noise_floor(const double *pdb, int kmin, int kmax, double *floor_db);

// excess_db: power over the floor per bin. Appends each occupied run.
void find_regions(const double *excess_db, int kmin, int kmax, std::vector<region> &out);

// Joins runs that belong to one signal: close neighbours, tones that take
// turns, and the evenly spaced carriers of multi-carrier PSK.
void merge_regions(std::vector<region> &regions, const short_history &history);

double correlation(const std::vector<double> &a, const std::vector<double> &b);

} // namespace sigid

#endif // SIGID_INTERNAL_H
