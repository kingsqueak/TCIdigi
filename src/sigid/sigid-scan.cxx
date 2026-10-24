// ----------------------------------------------------------------------------
//
//	sigid-scan.cxx -- run SigID over WAV files, for testing
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

// Built by "make check" and never installed. Feeds each WAV through the
// SigID core in receiver-sized blocks and prints one JSON line per hit, then
// a summary line per file. With --candidates it also prints the signals
// found so far, every --every seconds and at the end of each file. Reads
// 16-bit PCM WAV only.

#include <config.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "sigid.h"

// sigid-scan links only the core, not globals.cxx, so it has no mode_info
// table. Hits name the mode by its signature name and enum value.

static bool read_u32(FILE *f, unsigned &v)
{
	unsigned char b[4];
	if (fread(b, 1, 4, f) != 4)
		return false;
	v = b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24);
	return true;
}

// Mono floats and the sample rate. Stereo is averaged to mono.
static bool read_wav(const char *path, std::vector<float> &out, int &rate, std::string &err)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		err = "cannot open";
		return false;
	}
	char id[4];
	unsigned size = 0;
	if (fread(id, 1, 4, f) != 4 || memcmp(id, "RIFF", 4) || !read_u32(f, size)
	    || fread(id, 1, 4, f) != 4 || memcmp(id, "WAVE", 4)) {
		fclose(f);
		err = "not a WAV file";
		return false;
	}
	int channels = 0, bits = 0, format = 0;
	rate = 0;
	while (fread(id, 1, 4, f) == 4 && read_u32(f, size)) {
		if (!memcmp(id, "fmt ", 4)) {
			unsigned char fmt[16];
			if (size < 16 || fread(fmt, 1, 16, f) != 16)
				break;
			format = fmt[0] | (fmt[1] << 8);
			channels = fmt[2] | (fmt[3] << 8);
			rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
			bits = fmt[14] | (fmt[15] << 8);
			fseek(f, size - 16 + (size & 1), SEEK_CUR);
		} else if (!memcmp(id, "data", 4)) {
			if (format != 1 || bits != 16 || channels < 1 || rate <= 0) {
				fclose(f);
				err = "only 16-bit PCM is supported";
				return false;
			}
			std::vector<short> raw(size / 2);
			size_t got = fread(raw.data(), 2, raw.size(), f);
			raw.resize(got);
			out.resize(got / channels);
			for (size_t i = 0; i < out.size(); i++) {
				float acc = 0;
				for (int c = 0; c < channels; c++)
					acc += raw[i * channels + c] / 32768.0f;
				out[i] = acc / channels;
			}
			fclose(f);
			return true;
		} else {
			fseek(f, size + (size & 1), SEEK_CUR);
		}
	}
	fclose(f);
	err = "no data chunk";
	return false;
}

struct scan_state {
	const char *file;
	int hits;
};

static void on_hit(const sigid_hit &h, void *arg)
{
	scan_state *s = static_cast<scan_state *>(arg);
	s->hits++;
	printf("{\"file\":\"%s\",\"mode\":%d,\"name\":\"%s\",\"freq\":%.1f,\"bw\":%.1f,"
	       "\"snr\":%.1f,\"conf\":%.3f,\"alt\":%d}\n",
	       s->file, (int)h.mode, h.name, h.freq, h.bandwidth, h.snr,
	       h.confidence, (int)h.alternate);
}

static void print_candidates(const char *file, double t, const SigId &sigid)
{
	sigid_candidate c[64];
	size_t n = sigid.candidates(c, 64);
	for (size_t i = 0; i < n && i < 64; i++)
		printf("{\"file\":\"%s\",\"t\":%.2f,\"cand\":%d,\"lo\":%.1f,\"hi\":%.1f,"
		       "\"center\":%.1f,\"snr\":%.1f,\"age\":%.1f,\"parts\":%d}\n",
		       file, t, c[i].id, c[i].lo, c[i].hi, c[i].center, c[i].snr,
		       c[i].age, c[i].parts);
}

static void usage()
{
	fprintf(stderr, "usage: sigid-scan [--block N] [--candidates] [--every S] FILE.wav...\n"
		"Runs the SigID core over each file and prints hits as JSON lines.\n"
		"--candidates also prints the signals found, at the end of each file\n"
		"and every S seconds when --every is given.\n");
}

int main(int argc, char **argv)
{
	size_t block = 512;
	bool show_candidates = false;
	double every = 0;
	std::vector<const char *> files;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--block") && i + 1 < argc)
			block = (size_t)atoi(argv[++i]);
		else if (!strcmp(argv[i], "--candidates"))
			show_candidates = true;
		else if (!strcmp(argv[i], "--every") && i + 1 < argc)
			every = atof(argv[++i]);
		else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage();
			return 0;
		} else
			files.push_back(argv[i]);
	}
	if (files.empty() || block < 1) {
		usage();
		return 2;
	}

	int failed = 0;
	for (size_t n = 0; n < files.size(); n++) {
		std::vector<float> audio;
		int rate = 0;
		std::string err;
		if (!read_wav(files[n], audio, rate, err)) {
			fprintf(stderr, "%s: %s\n", files[n], err.c_str());
			failed++;
			continue;
		}
		scan_state st = { files[n], 0 };
		SigId sigid(on_hit, &st);
		double next_print = every;
		for (size_t at = 0; at < audio.size(); at += block) {
			size_t len = audio.size() - at < block ? audio.size() - at : block;
			sigid.receive(&audio[at], len, rate);
			double t = (double)(at + len) / rate;
			if (show_candidates && every > 0 && t >= next_print) {
				print_candidates(files[n], t, sigid);
				next_print += every;
			}
		}
		if (show_candidates)
			print_candidates(files[n], (double)audio.size() / rate, sigid);
		printf("{\"file\":\"%s\",\"summary\":true,\"rate\":%d,\"seconds\":%.2f,"
		       "\"analysed\":%llu,\"hits\":%d}\n",
		       files[n], rate, (double)audio.size() / rate,
		       sigid.samples_seen(), st.hits);
	}
	return failed ? 1 : 0;
}
