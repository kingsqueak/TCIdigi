// ----------------------------------------------------------------------------
// engine.cxx -- modem and radio calls for the Qt window
//
// Copyright (C) 2026
//
// This file is part of fldigi. GPL-3.0-or-later.
// ----------------------------------------------------------------------------

#include "engine.h"

#include "fldigi_start.h"

#include "fl_digi.h"
#include "confdialog.h"
#include "trx.h"
#include "configuration.h"
#include "status.h"
#include "globals.h"
#include "macros.h"
#include "tci.h"
#include "squelch_status.h"
#include "rigsupport.h"
#include "psk_browser.h"
#include "util.h"
#include "qrunner.h"
#include "pskrep.h"
#include "tcidigi_tx.h"

#include <FL/Fl.H>

// Private FLTK timer pump. Fl::check() enters the Cocoa event loop and
// deadlocks there once Qt has called [NSApp run].
class Fl_Timeout {
public:
	static void do_timeouts();
};

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_up = 0;
int g_rx_pos = 0;
int g_mode_ids[NUM_MODES];
int g_mode_n = -1;

struct StoredQrg {
	long long rf;
	int carrier;
	int mode;
	int usb;
};

std::vector<StoredQrg> g_store;

void store_text(const StoredQrg& m, char* buf, int cap)
{
	const char* name = "Mode";
	if (m.mode >= 0 && m.mode < NUM_MODES && mode_info[m.mode].name && mode_info[m.mode].name[0])
		name = mode_info[m.mode].name;
	if (!buf || cap < 1)
		return;
	if (m.rf > 0)
		std::snprintf(buf, (size_t)cap, "%s  %lld %c %d",
			name, m.rf, m.usb ? '+' : '-', m.carrier);
	else
		std::snprintf(buf, (size_t)cap, "%s  %d", name, m.carrier);
}

void copy_str(char* dst, int cap, const char* src)
{
	if (!dst || cap < 1)
		return;
	if (!src)
		src = "";
	std::snprintf(dst, (size_t)cap, "%s", src);
}

void ensure_modes()
{
	if (g_mode_n >= 0)
		return;
	g_mode_n = 0;
	for (int i = 0; i < NUM_RXTX_MODES && g_mode_n < NUM_MODES; ++i) {
		if (!mode_info[i].sname || !mode_info[i].sname[0])
			continue;
		g_mode_ids[g_mode_n++] = i;
	}
}

bool can_tx()
{
	return active_modem && (active_modem->get_cap() & modem::CAP_TX);
}

// The browser reads a different squelch for PSK, RTTY, and CW. Other modes
// have no multi-channel viewer, so the slider must not pretend to gate them.
bool viewer_squelch(double* lo, double* hi, double** slot)
{
	double bottom = -3.0;
	double top = 6.0;
	double* value = &progStatus.VIEWER_psksquelch;
	bool used = false;
	if (active_modem) {
		const trx_mode mode = active_modem->get_mode();
		if (mode == MODE_CW) {
			bottom = 0.0;
			top = 40.0;
			value = &progStatus.VIEWER_cwsquelch;
			used = true;
		} else if (mode == MODE_RTTY) {
			bottom = -6.0;
			top = 34.0;
			value = &progStatus.VIEWER_rttysquelch;
			used = true;
		} else if (mode == MODE_PSK31 || mode == MODE_PSK63 || mode == MODE_PSK63F
			|| mode == MODE_PSK125 || mode == MODE_PSK250 || mode == MODE_PSK500
			|| mode == MODE_PSK125R || mode == MODE_PSK250R || mode == MODE_PSK500R
			|| (mode >= MODE_QPSK_FIRST && mode <= MODE_QPSK_LAST)) {
			used = true;
		}
	}
	if (lo)
		*lo = bottom;
	if (hi)
		*hi = top;
	if (slot)
		*slot = used ? value : nullptr;
	return used;
}

int signal_level()
{
	if (!wf || !active_modem)
		return 0;
	int center = active_modem->get_freq();
	int bw = (int)active_modem->get_bandwidth();
	if (bw < 30)
		bw = 30;
	double peak = 1e-12;
	for (int hz = center - bw; hz <= center + bw; hz += 4) {
		double p = wf->Pwr(hz);
		if (p > peak)
			peak = p;
	}
	int level = (int)std::lround(10.0 * std::log10(peak) + 80.0);
	if (level < 0)
		level = 0;
	if (level > 100)
		level = 100;
	return level;
}

// The picture is the FFT. LowFreqCutoff and HighFreqCutoff are its edges.
// The radio filter is a receiver bandwidth and does not change this span.
struct WfSlice {
	int hz_low;
	int hz_high;
	int col0;
	int cols;
};

WfSlice waterfall_slice()
{
	WfSlice slice = {0, 3000, 0, 1};
	if (!wf)
		return slice;
	int step = wf->pixelHz();
	if (step < 1)
		step = 1;
	int cols = wf->imageWidth();
	if (cols < 1)
		cols = 1;
	const int origin = wf->Offset();
	const int image_hi = origin + cols * step;

	int lo = progdefaults.LowFreqCutoff;
	int hi = progdefaults.HighFreqCutoff;
	if (hi <= lo)
		hi = lo + step;
	if (lo < origin)
		lo = origin;
	if (hi > image_hi)
		hi = image_hi;
	if (hi <= lo) {
		lo = origin;
		hi = image_hi;
	}

	int c0 = (lo - origin) / step;
	int c1 = (hi - origin + step - 1) / step;
	if (c0 < 0)
		c0 = 0;
	if (c1 > cols)
		c1 = cols;
	if (c1 <= c0)
		c1 = std::min(cols, c0 + 1);
	slice.col0 = c0;
	slice.cols = c1 - c0;
	slice.hz_low = origin + c0 * step;
	slice.hz_high = origin + c1 * step;
	return slice;
}

} // namespace

int engine_up()
{
	return g_up;
}

void engine_qrz_account(char* user, int user_cap, char* password, int pass_cap,
	char* call, int call_cap, char* grid, int grid_cap,
	char* antenna, int antenna_cap)
{
	auto put = [](char* dst, int cap, const std::string& src) {
		if (!dst || cap < 1)
			return;
		std::snprintf(dst, (size_t)cap, "%s", src.c_str());
	};
	put(user, user_cap, "");
	put(password, pass_cap, "");
	put(call, call_cap, "");
	put(grid, grid_cap, "");
	put(antenna, antenna_cap, "");
	if (!g_up)
		return;
	put(user, user_cap, progdefaults.QRZusername);
	put(password, pass_cap, progdefaults.QRZuserpassword);
	put(call, call_cap, progdefaults.myCall);
	put(grid, grid_cap, progdefaults.myLocator);
	put(antenna, antenna_cap, progdefaults.myAntenna);
}

int engine_start(int argc, char** argv)
{
	g_rx_pos = 0;
	g_mode_n = -1;
	int rc = fldigi_start(argc, argv, 1);
	g_up = (rc == 0) ? 1 : 0;
	if (g_up) {
		// The modem reads this queue. The hidden FLTK buffer is not a second copy.
		tcidigi_tx_use(true);
		psk_browser_keep_text(true);
		// Saved configs clear a channel after 15 seconds. Keep the QSO for 5 minutes.
		// The hidden spinner was filled from the file before this override.
		if (progdefaults.VIEWERtimeout != 300) {
			progdefaults.VIEWERtimeout = 300;
			progdefaults.changed = true;
		}
		if (cntTimeout)
			cntTimeout->value(progdefaults.VIEWERtimeout);
		stopMacroTimer();
		macro_idle_on = false;
		progStatus.skip_sked_macro = false;
	}
	return g_up;
}

// fldigi also paints bandwidth tracks into the waterfall image. Those
// columns do not land on the Qt lines, so one red edge looks thicker.
// The Qt view draws every marker itself, one pixel wide.
static void publish_waterfall()
{
	if (!wf)
		return;
	const bool tracks = progdefaults.UseBWTracks;
	const bool wide = progdefaults.UseWideTracks;
	progdefaults.UseBWTracks = false;
	progdefaults.UseWideTracks = false;
	wf->publishImage();
	progdefaults.UseBWTracks = tracks;
	progdefaults.UseWideTracks = wide;
}

void engine_tick()
{
	if (!g_up)
		return;
	Fl::do_widget_deletion();
	Fl_Timeout::do_timeouts();
	Fl::run_checks();
	Fl::run_idle();
	Fl_Awake_Handler awake = 0;
	void* awake_data = 0;
	while (Fl::get_awake_handler_(awake, awake_data) == 0) {
		if (awake)
			awake(awake_data);
	}
	for (int i = 0; i < NUM_QRUNNER_THREADS; ++i) {
		if (cbq[i])
			cbq[i]->flush();
	}
	if (wf)
		publish_waterfall();
}

void engine_view(EngineView* out)
{
	if (!out)
		return;
	std::memset(out, 0, sizeof(*out));
	if (!g_up)
		return;

	if (qsoFreqDisp)
		out->frequency_hz = (long long)qsoFreqDisp->value();
	if (qso_opMODE)
		copy_str(out->rig_mode, (int)sizeof out->rig_mode, qso_opMODE->value());
	if (qso_opBW)
		copy_str(out->rig_filter, (int)sizeof out->rig_filter, qso_opBW->value());
	if (active_modem) {
		copy_str(out->modem, (int)sizeof out->modem, active_modem->get_mode_name());
		out->carrier_hz = active_modem->get_freq();
		out->bandwidth_hz = (int)(active_modem->get_bandwidth() + 0.5);
		if (out->bandwidth_hz < 1)
			out->bandwidth_hz = 1;
		out->locked = active_modem->freqlocked() ? 1 : 0;
	}
	out->afc = progStatus.afconoff ? 1 : 0;
	out->afc_ok = (active_modem && (active_modem->get_cap() & modem::CAP_AFC)) ? 1 : 0;
	out->sql = progStatus.sqlonoff ? 1 : 0;
	out->spot = progStatus.spot_recv ? 1 : 0;
	out->rx_id = progdefaults.rsid ? 1 : 0;
	out->tx_id = progdefaults.TransmitRSid ? 1 : 0;
	out->tuning = (trx_state == STATE_TUNE) ? 1 : 0;
	out->transmitting = (trx_state == STATE_TX) ? 1 : 0;
	out->macro_timer = progStatus.timer > 0 ? progStatus.timer : 0;
	out->reverse = (wf && wf->Reverse()) ? 1 : 0;
	out->squelch = (int)std::lround(progStatus.sldrSquelchValue);
	out->level = signal_level();
	out->wf_mode = wf ? wf->displayMode() : 0;
	out->wf_mag = wf ? wf->Mag() : 1;
	out->wf_speed = wf ? wf->Speed() : 2;
	out->tci = tci_is_active() ? 1 : 0;
	out->tci_audio = tci_audio_wanted() ? 1 : 0;
	out->tx_db = progStatus.txlevel;
	out->rx_db = progdefaults.tci_rx_level;
	double sql_lo = -3.0;
	double sql_hi = 6.0;
	double* sql_slot = nullptr;
	out->viewer_sql_ok = viewer_squelch(&sql_lo, &sql_hi, &sql_slot) ? 1 : 0;
	out->viewer_sql_lo = sql_lo;
	out->viewer_sql_hi = sql_hi;
	out->viewer_sql = sql_slot ? *sql_slot : progStatus.VIEWER_psksquelch;
	WfSlice slice = waterfall_slice();
	out->hz_low = slice.hz_low;
	out->hz_high = slice.hz_high;

	const char* report = (Status2 && Status2->label()) ? Status2->label() : "";
	if (std::strcmp(report, "STATUS2") == 0)
		report = "";
	copy_str(out->signal_report, (int)sizeof out->signal_report, report);

	const char* state = "Receiving";
	if (out->tuning)
		state = "Tune";
	else if (out->transmitting)
		state = "Transmitting";
	else if (out->tci)
		state = "TCI receive";
	std::snprintf(out->modem_status, sizeof out->modem_status, "%s  %s",
		out->modem[0] ? out->modem : "Modem", state);

	const char* status = (Status1 && Status1->label()) ? Status1->label() : "";
	if (!status[0] || std::strcmp(status, "STATUS1") == 0)
		status = state;
	copy_str(out->status, (int)sizeof out->status, status);
}

int engine_rx_delta(char* buf, int cap)
{
	if (!buf || cap < 2 || !ReceiveText || !ReceiveText->buffer())
		return 0;
	int len = ReceiveText->buffer()->length();
	if (len < g_rx_pos) {
		g_rx_pos = 0;
		buf[0] = 0;
		return -1;
	}
	if (len == g_rx_pos) {
		buf[0] = 0;
		return 0;
	}
	int n = std::min(cap - 1, len - g_rx_pos);
	char* text = ReceiveText->buffer()->text_range(g_rx_pos, g_rx_pos + n);
	if (!text) {
		buf[0] = 0;
		return 0;
	}
	std::memcpy(buf, text, (size_t)n);
	buf[n] = 0;
	std::free(text);
	g_rx_pos += n;
	return n;
}

void engine_clear_rx()
{
	g_rx_pos = 0;
	if (ReceiveText)
		ReceiveText->clear();
}

void engine_clear_tx()
{
	if (tcidigi_tx_active())
		tcidigi_tx_clear();
	else if (TransmitText)
		TransmitText->clear();
}

int engine_tx_text(char* buf, int cap)
{
	if (!buf || cap < 1)
		return 0;
	buf[0] = 0;
	if (tcidigi_tx_active()) {
		copy_str(buf, cap, tcidigi_tx_get().c_str());
		return (int)std::strlen(buf);
	}
	if (!TransmitText || !TransmitText->buffer())
		return 0;
	char* text = TransmitText->buffer()->text();
	if (!text)
		return 0;
	copy_str(buf, cap, text);
	std::free(text);
	return (int)std::strlen(buf);
}

static bool has_rx_mark(const std::string& text)
{
	return text.find("^r") != std::string::npos || text.find("^R") != std::string::npos;
}

void engine_set_tx_text(const char* text)
{
	if (!text)
		return;
	if (trx_state == STATE_TUNE)
		return;
	if (tcidigi_tx_active()) {
		const std::string have = tcidigi_tx_get();
		if (have == text)
			return;
		const std::string want(text);
		// The modem reads this buffer from the front. Replacing it while
		// transmitting would send the message again, so only append.
		if (trx_state == STATE_TX) {
			// Backspace of a character already handed to the modem is sent.
			// An edit inside that sent text is left alone.
			if (!tcidigi_tx_edit(want) && has_rx_mark(want) && !has_rx_mark(have))
				tcidigi_tx_add("^r");
			return;
		}
		tcidigi_tx_replace(want);
		return;
	}
	if (!TransmitText || !TransmitText->buffer())
		return;
	char* cur = TransmitText->buffer()->text();
	const bool same = cur && std::strcmp(cur, text) == 0;
	std::string have = cur ? cur : "";
	if (cur)
		std::free(cur);
	if (same)
		return;
	const std::string want(text);
	// The modem reads this buffer from the front. Replacing it while
	// transmitting would send the message again, so only append.
	if (trx_state == STATE_TX) {
		if (want.size() >= have.size() && want.compare(0, have.size(), have) == 0) {
			if (want.size() > have.size())
				TransmitText->add_text(want.substr(have.size()));
			return;
		}
		if (has_rx_mark(want) && !has_rx_mark(have))
			TransmitText->add_text("^r");
		return;
	}
	TransmitText->clear();
	TransmitText->add_text(want);
}

int engine_modem_count()
{
	ensure_modes();
	return g_mode_n;
}

int engine_modem_name(int index, char* buf, int cap)
{
	ensure_modes();
	if (!buf || cap < 1 || index < 0 || index >= g_mode_n)
		return 0;
	copy_str(buf, cap, mode_info[g_mode_ids[index]].sname);
	return 1;
}

int engine_modem_label(int index, char* buf, int cap)
{
	ensure_modes();
	if (!buf || cap < 1 || index < 0 || index >= g_mode_n)
		return 0;
	const char* label = mode_info[g_mode_ids[index]].name;
	if (!label || !label[0])
		label = mode_info[g_mode_ids[index]].sname;
	copy_str(buf, cap, label);
	return 1;
}

int engine_modem_group(int index, char* buf, int cap)
{
	ensure_modes();
	if (!buf || cap < 1 || index < 0 || index >= g_mode_n)
		return 0;
	buf[0] = 0;
	const trx_mode mode = mode_info[g_mode_ids[index]].mode;
	const char* group = nullptr;
	if (mode == MODE_CW)
		group = "CW";
	else if (mode == MODE_RTTY)
		group = "RTTY";
	else if (mode >= MODE_PSK31 && mode <= MODE_PSK1000)
		group = "BPSK";
	else if (mode >= MODE_QPSK_FIRST && mode <= MODE_QPSK_LAST)
		group = "QPSK";
	else if (mode >= MODE_OLIVIA_FIRST && mode <= MODE_OLIVIA_LAST)
		group = "Olivia";
	else if (mode >= MODE_THOR_FIRST && mode <= MODE_THOR_LAST)
		group = "Thor";
	else if (mode >= MODE_MFSK_FIRST && mode <= MODE_MFSK_LAST)
		group = "MFSK";
	else if (mode >= MODE_MT63_FIRST && mode <= MODE_MT63_LAST)
		group = "MT63";
	else if (mode >= MODE_CONTESTIA && mode <= MODE_CONTESTIA_LAST)
		group = "Contestia";
	else if (mode >= MODE_DOMINOEX_FIRST && mode <= MODE_DOMINOEX_LAST)
		group = "DominoEX";
	else if (mode >= MODE_HELL_FIRST && mode <= MODE_HELL_LAST)
		group = "Hell";
	if (!group)
		return 0;
	copy_str(buf, cap, group);
	return 1;
}

void engine_set_modem(const char* name)
{
	if (!name || !name[0] || !active_modem)
		return;
	ensure_modes();
	for (int i = 0; i < g_mode_n; ++i) {
		const mode_info_t& info = mode_info[g_mode_ids[i]];
		if (std::strcmp(info.sname, name) != 0)
			continue;
		if (active_modem->get_mode() != info.mode)
			init_modem(info.mode);
		return;
	}
}

int engine_rx_is_picture()
{
	if (!g_up || !active_modem)
		return 0;
	const trx_mode mode = active_modem->get_mode();
	return (mode >= MODE_HELL_FIRST && mode <= MODE_HELL_LAST) ? 1 : 0;
}

int engine_rx_picture(unsigned char* gray, int cap, int* width, int* height)
{
	if (width)
		*width = 0;
	if (height)
		*height = 0;
	if (!g_up || !FHdisp || !engine_rx_is_picture())
		return 0;
	return FHdisp->copy_gray(gray, cap, width, height);
}

void engine_rx_picture_size(int width, int height)
{
	if (!FHdisp || !engine_rx_is_picture())
		return;
	if (width < 16)
		width = 16;
	const int min_h = FHdisp->min_image_height();
	if (height < min_h)
		height = min_h;
	// The raster keeps a 4 pixel FLTK frame outside the painted image.
	static int last_w = -1;
	static int last_h = -1;
	if (width == last_w && height == last_h)
		return;
	FHdisp->resize(FHdisp->x(), FHdisp->y(), width + 4, height + 4);
	last_w = width;
	last_h = height;
}

void engine_rx_picture_clear()
{
	if (FHdisp)
		FHdisp->clear();
}

void engine_set_rig_mode(const char* name)
{
	if (!qso_opMODE || !name || !name[0])
		return;
	const char* cur = qso_opMODE->value();
	if (cur && std::strcmp(cur, name) == 0)
		return;
	qso_opMODE->value(std::string(name));
	cb_qso_opMODE();
}

void engine_set_rig_filter(const char* name)
{
	if (!qso_opBW || !name || !name[0])
		return;
	const char* cur = qso_opBW->value();
	if (cur && std::strcmp(cur, name) == 0)
		return;
	qso_opBW->value(std::string(name));
	cb_qso_opBW();
}

void engine_set_afc(int on)
{
	progStatus.afconoff = on ? true : false;
	if (active_modem)
		set_mode_afc(active_modem->get_mode(), progStatus.afconoff);
	if (btnAFC)
		btnAFC->value(on ? 1 : 0);
}

void engine_set_sql(int on)
{
	progStatus.sqlonoff = on ? true : false;
	if (active_modem)
		set_mode_squelch_onoff(active_modem->get_mode(), progStatus.sqlonoff);
	if (btnSQL)
		btnSQL->value(on ? 1 : 0);
}

static char g_spot_error[180];

const char* engine_spot_error()
{
	return g_spot_error;
}

static void spot_fail(const char* text)
{
	std::snprintf(g_spot_error, sizeof g_spot_error, "%s", text ? text : "");
}

int engine_set_spot(int on)
{
	g_spot_error[0] = 0;
	if (!on) {
		progStatus.spot_recv = false;
		if (btnAutoSpot)
			btnAutoSpot->value(0);
		return 1;
	}
	if (!g_up) {
		spot_fail("The modem is not running yet.");
		return 0;
	}
	// Auto registration is what forwards decoded calls. Frequency goes with
	// the report so the spot lands on the dial, not at 0 Hz.
	progdefaults.pskrep_auto = true;
	progdefaults.pskrep_qrg = true;
	if (!pskrep_start()) {
		const char* err = pskrep_error();
		if (err && std::strstr(err, "antenna"))
			spot_fail("PSK Reporter needs a short antenna description. Set Antenna in Configure, Station.");
		else if (err && std::strstr(err, "callsign"))
			spot_fail("PSK Reporter needs your call sign. Set Call in Configure, Station.");
		else if (err && std::strstr(err, "locator"))
			spot_fail("PSK Reporter needs a 6-character grid such as FN43cb. Set Grid in Configure, Station.");
		else
			spot_fail(err && err[0] ? err : "PSK Reporter did not start.");
		progStatus.spot_recv = false;
		if (btnAutoSpot)
			btnAutoSpot->value(0);
		return 0;
	}
	progdefaults.usepskrep = true;
	progStatus.spot_recv = true;
	if (btnAutoSpot)
		btnAutoSpot->value(1);
	return 1;
}

static void clear_qt_status(void*)
{
	if (Status1)
		Status1->label("");
}

// put_status writes the hidden FLTK status bar. The Qt window shows Status1.
static void show_qt_status(const char* msg, double seconds)
{
	put_status(msg, seconds);
	if (!Status1)
		return;
	// label() keeps the pointer. The TxID line is a stack buffer, so copy it.
	Status1->copy_label(msg);
	Fl::remove_timeout(clear_qt_status);
	if (seconds > 0.0)
		Fl::add_timeout(seconds, clear_qt_status);
}

void engine_set_rxid(int on)
{
	progdefaults.rsid = on ? true : false;
	// fldigi's RxID right-click is "Passband". Without it the detector
	// only watches about 200 Hz around the carrier, so a waterfall full
	// of IDs looks like the button does nothing.
	if (on)
		progdefaults.rsidWideSearch = true;
	progdefaults.changed = true;
	if (btnRSID)
		btnRSID->value(on ? 1 : 0);
	if (chkRSidWideSearch)
		chkRSidWideSearch->value(progdefaults.rsidWideSearch ? 1 : 0);
	if (on)
		show_qt_status("RxID is searching the waterfall.", 6.0);
	else
		show_qt_status("RxID is off.", 4.0);
}

void engine_set_txid(int on)
{
	progdefaults.TransmitRSid = on ? true : false;
	progdefaults.changed = true;
	if (btnTxRSID)
		btnTxRSID->value(on ? 1 : 0);
	if (!on) {
		show_qt_status("TxID is off.", 4.0);
		return;
	}
	if (active_modem && !progdefaults.rsid_tx_modes.test(active_modem->get_mode())) {
		char msg[140];
		std::snprintf(msg, sizeof msg,
			"%s does not send RSID. TxID stays on for modes that do.",
			active_modem->get_mode_name());
		show_qt_status(msg, 8.0);
		return;
	}
	show_qt_status("TxID sends RSID at the start of transmit.", 6.0);
}

static void copy_station(char* dst, int cap, const std::string& src)
{
	if (!dst || cap < 1)
		return;
	std::snprintf(dst, (size_t)cap, "%s", src.c_str());
}

static void assign_box(Fl_Input2* box, const std::string& text)
{
	if (box)
		box->value(text.c_str());
}

void engine_station(EngineStation* out)
{
	if (!out)
		return;
	std::memset(out, 0, sizeof(*out));
	if (!g_up)
		return;
	copy_station(out->call, (int)sizeof out->call, progdefaults.myCall);
	copy_station(out->oper_call, (int)sizeof out->oper_call, progdefaults.operCall);
	copy_station(out->name, (int)sizeof out->name, progdefaults.myName);
	copy_station(out->qth, (int)sizeof out->qth, progdefaults.myQth);
	copy_station(out->grid, (int)sizeof out->grid, progdefaults.myLocator);
	copy_station(out->antenna, (int)sizeof out->antenna, progdefaults.myAntenna);
	copy_station(out->psk_host, (int)sizeof out->psk_host, progdefaults.pskrep_host);
	copy_station(out->psk_port, (int)sizeof out->psk_port, progdefaults.pskrep_port);
	out->psk_qrg = progdefaults.pskrep_qrg ? 1 : 0;
}

void engine_set_station(const EngineStation* in)
{
	if (!g_up || !in)
		return;
	const bool same =
		progdefaults.myCall == in->call
		&& progdefaults.operCall == in->oper_call
		&& progdefaults.myName == in->name
		&& progdefaults.myQth == in->qth
		&& progdefaults.myLocator == in->grid
		&& progdefaults.myAntenna == in->antenna
		&& progdefaults.pskrep_host == in->psk_host
		&& progdefaults.pskrep_port == in->psk_port
		&& progdefaults.pskrep_qrg == (in->psk_qrg != 0);
	if (same)
		return;
	const bool reporter =
		progdefaults.myCall != in->call
		|| progdefaults.myLocator != in->grid
		|| progdefaults.myAntenna != in->antenna
		|| progdefaults.pskrep_host != in->psk_host
		|| progdefaults.pskrep_port != in->psk_port
		|| progdefaults.pskrep_qrg != (in->psk_qrg != 0);
	progdefaults.myCall.assign(in->call);
	progdefaults.operCall.assign(in->oper_call);
	progdefaults.myName.assign(in->name);
	progdefaults.myQth.assign(in->qth);
	progdefaults.myLocator.assign(in->grid);
	progdefaults.myAntenna.assign(in->antenna);
	if (in->psk_host[0])
		progdefaults.pskrep_host.assign(in->psk_host);
	if (in->psk_port[0])
		progdefaults.pskrep_port.assign(in->psk_port);
	progdefaults.pskrep_qrg = in->psk_qrg != 0;
	progdefaults.changed = true;
	assign_box(inpMyCallsign, progdefaults.myCall);
	assign_box(inpOperCallsign, progdefaults.operCall);
	assign_box(inpMyName, progdefaults.myName);
	assign_box(inpMyQth, progdefaults.myQth);
	assign_box(inpMyLocator, progdefaults.myLocator);
	assign_box(inpMyAntenna, progdefaults.myAntenna);
	// An open reporter keeps the call, grid, and host it was started with.
	if (reporter && progStatus.spot_recv) {
		pskrep_stop();
		progdefaults.pskrep_auto = true;
		if (!pskrep_start()) {
			progStatus.spot_recv = false;
			if (btnAutoSpot)
				btnAutoSpot->value(0);
		}
	}
}

void engine_tune(int on)
{
	if (!can_tx())
		return;
	if (on) {
		if (trx_state == STATE_TUNE)
			return;
		active_modem->set_stopflag(true);
		trx_tune();
		if (btnTune)
			btnTune->value(1);
		return;
	}
	if (trx_state == STATE_TUNE)
		trx_receive();
	if (btnTune)
		btnTune->value(0);
	if (wf && wf->xmtrcv)
		wf->xmtrcv->value(0);
}

void engine_transmit(int on)
{
	if (!can_tx())
		return;
	if (on) {
		if (trx_state == STATE_TX)
			return;
		active_modem->set_stopflag(false);
		trx_transmit();
		if (wf && wf->xmtrcv)
			wf->xmtrcv->value(1);
		return;
	}
	if (trx_state == STATE_TUNE) {
		trx_receive();
		if (btnTune)
			btnTune->value(0);
	} else if (trx_state == STATE_TX) {
		queue_reset();
		active_modem->set_stopflag(true);
		if (TransmitText)
			TransmitText->clear();
		// stopflag only ends a transmit that is pulling characters.
		// Drop the state here so a second click always returns to receive.
		trx_receive();
	}
	if (wf && wf->xmtrcv)
		wf->xmtrcv->value(0);
}

void engine_abort()
{
	if (!g_up || !active_modem)
		return;
	// Escape Escape also cancels a macro countdown that is waiting in receive.
	stopMacroTimer();
	if (trx_state != STATE_TX && trx_state != STATE_TUNE)
		return;
	queue_reset();
	active_modem->set_stopflag(true);
	if (TransmitText)
		TransmitText->clear();
	if (btnTune)
		btnTune->value(0);
	if (wf && wf->xmtrcv)
		wf->xmtrcv->value(0);
	// Restarts the modem in receive and leaves the transmit loop at once.
	trx_start_modem(active_modem);
}

void engine_set_tx_db(double db)
{
	if (db > 0.0)
		db = 0.0;
	if (db < -30.0)
		db = -30.0;
	progStatus.txlevel = db;
	if (progdefaults.txlevel_by_mode && active_modem)
		set_mode_txlevel(active_modem->get_mode(), db);
}

void engine_set_rx_db(double db)
{
	if (db > 0.0)
		db = 0.0;
	if (db < -30.0)
		db = -30.0;
	progdefaults.tci_rx_level = db;
}

void engine_tci(EngineTci* out)
{
	if (!out)
		return;
	std::memset(out, 0, sizeof *out);
	if (!g_up) {
		copy_str(out->status, (int)sizeof out->status, "The modem is not running yet.");
		return;
	}
	out->enable = progdefaults.tci_enable ? 1 : 0;
	out->audio = progdefaults.tci_audio ? 1 : 0;
	out->rx = progdefaults.tci_rx;
	copy_str(out->host, (int)sizeof out->host, progdefaults.tci_host.c_str());
	copy_str(out->port, (int)sizeof out->port, progdefaults.tci_port.c_str());
	tci_copy_status(out->status, (int)sizeof out->status);
}

void engine_set_tci(int enable, int audio, const char* host, const char* port, int rx)
{
	if (!g_up)
		return;
	tci_apply(enable != 0, audio != 0, host, port, rx);
}

void engine_set_squelch(int value)
{
	if (value < 0)
		value = 0;
	if (value > 100)
		value = 100;
	progStatus.sldrSquelchValue = value;
	if (active_modem)
		set_mode_squelch(active_modem->get_mode(), value);
	if (sldrSquelch)
		sldrSquelch->value(value);
}

void engine_set_viewer_sql(double db)
{
	double lo = -3.0;
	double hi = 6.0;
	double* slot = nullptr;
	if (!viewer_squelch(&lo, &hi, &slot) || !slot)
		return;
	if (db < lo)
		db = lo;
	if (db > hi)
		db = hi;
	*slot = db;
	progStatus.squelch_value = db;
}

void engine_set_carrier(int hz, int search)
{
	if (!active_modem)
		return;
	active_modem->set_freq(hz);
	// fldigi's waterfall left-click does this. PSK then looks within
	// SearchRange for a peak. Shift-click and the carrier box pass 0.
	if (search)
		active_modem->set_sigsearch(SIGSEARCH);
	if (wf)
		wf->carrier(hz);
}

void engine_set_frequency(long long hz)
{
	if (!g_up)
		return;
	if (hz < 0)
		hz = 0;
	if (hz > 10000000000LL)
		hz = 10000000000LL;
	const unsigned long f = (unsigned long)hz;
	if (qsoFreqDisp1)
		qsoFreqDisp1->value(f);
	if (qsoFreqDisp2)
		qsoFreqDisp2->value(f);
	if (qsoFreqDisp3)
		qsoFreqDisp3->value(f);
	sendFreq((long)f);
}

void engine_set_reverse(int on)
{
	if (wf)
		wf->Reverse(on ? true : false);
	if (!active_modem)
		return;
	active_modem->set_reverse(on ? true : false);
	set_mode_reverse(active_modem->get_mode(), on ? 1 : 0);
}

void engine_set_lock(int on)
{
	if (!active_modem)
		return;
	active_modem->set_freqlock(on ? true : false);
	if (wf && wf->xmtlock)
		wf->xmtlock->value(on ? 1 : 0);
}

void engine_wf_mode(int mode)
{
	if (!wf)
		return;
	if (mode < 0)
		mode = 0;
	if (mode >= NUM_WF_MODES)
		mode = NUM_WF_MODES - 1;
	wf->setDisplayMode(mode);
}

void engine_wf_mag(int mag)
{
	if (!wf)
		return;
	if (mag < 1)
		mag = 1;
	if (mag > 3)
		mag = 3;
	wf->Mag(mag);
}

void engine_wf_speed(int speed)
{
	if (!wf)
		return;
	wf->Speed(speed);
}

void engine_wf_slew(int dir)
{
	if (wf)
		wf->slew(dir);
}

void engine_wf_center()
{
	if (wf)
		wf->movetocenter();
}

void engine_wf_ref(int db)
{
	progdefaults.wfRefLevel = db;
	if (wf && wf->wfRefLevel)
		wf->wfRefLevel->value(db);
}

void engine_wf_span(int db)
{
	progdefaults.wfAmpSpan = db;
	if (wf)
		wf->setAmpSpan();
}

void engine_qsy()
{
	do_qsy(true);
}

int engine_store(char* buf, int cap)
{
	if (buf && cap > 0)
		buf[0] = 0;
	if (!g_up || !active_modem || !wf) {
		copy_str(buf, cap, "Store is not available");
		return -1;
	}
	StoredQrg mark;
	mark.rf = wf->rfcarrier();
	mark.carrier = active_modem->get_freq();
	mark.mode = active_modem->get_mode();
	mark.usb = wf->USB() ? 1 : 0;
	for (const StoredQrg& have : g_store) {
		if (have.rf == mark.rf && have.carrier == mark.carrier && have.mode == mark.mode) {
			char body[160];
			store_text(mark, body, (int)sizeof body);
			if (buf && cap > 0)
				std::snprintf(buf, (size_t)cap, "Already stored: %s", body);
			return 0;
		}
	}
	if ((int)g_store.size() >= 30)
		g_store.erase(g_store.begin());
	g_store.push_back(mark);
	char body[160];
	store_text(mark, body, (int)sizeof body);
	if (buf && cap > 0)
		std::snprintf(buf, (size_t)cap, "Stored %s", body);
	return (int)g_store.size();
}

int engine_store_count()
{
	return (int)g_store.size();
}

int engine_store_label(int index, char* buf, int cap)
{
	if (!buf || cap < 1)
		return 0;
	buf[0] = 0;
	if (index < 0 || index >= (int)g_store.size())
		return 0;
	store_text(g_store[(size_t)index], buf, cap);
	return 1;
}

void engine_store_select(int index)
{
	if (!g_up || !active_modem || index < 0 || index >= (int)g_store.size())
		return;
	const StoredQrg mark = g_store[(size_t)index];
	if (trx_state == STATE_TX || trx_state == STATE_TUNE)
		engine_transmit(0);
	if (mark.mode >= 0 && mark.mode < NUM_MODES
		&& active_modem->get_mode() != (trx_mode)mark.mode)
		init_modem((trx_mode)mark.mode);
	// A different dial frequency is a radio QSY. The same dial only moves
	// the audio tune point.
	if (mark.rf && wf && mark.rf != wf->rfcarrier())
		qsy(mark.rf, mark.carrier);
	else
		engine_set_carrier(mark.carrier, 0);
}

void engine_store_clear()
{
	g_store.clear();
}

int engine_waterfall(unsigned char* rgbx, int cap, int* width, int* height)
{
	if (width)
		*width = 0;
	if (height)
		*height = 0;
	if (!g_up || !wf || !rgbx || cap < 4)
		return 0;
	publish_waterfall();
	// SIG is a time-domain scope. fldigi paints it from WFdisp::draw(), and
	// that draw never runs while the FLTK window is hidden.
	if (wf->displayMode() == SCOPE) {
		const unsigned char* scope = wf->scopeImage();
		int sw = wf->imageWidth();
		int sh = wf->scopeHeight();
		int soff = wf->scopeOffset();
		int stride = wf->scopeStride();
		if (!scope || sw < 1 || sh < 1 || stride < 1)
			return 0;
		if (soff < 0)
			soff = 0;
		if (soff >= stride)
			soff = stride - 1;
		if (soff + sw > stride)
			sw = stride - soff;
		if (sh > 1024)
			sh = 1024;
		if (sw > 4096)
			sw = 4096;
		const long bytes = (long)sw * sh * 4;
		if (bytes > cap)
			return 0;
		unsigned char* dst = rgbx;
		for (int y = 0; y < sh; ++y) {
			const unsigned char* row = scope + (long)y * stride + soff;
			for (int x = 0; x < sw; ++x) {
				const unsigned char gray = row[x];
				dst[0] = dst[1] = dst[2] = gray;
				dst[3] = 255;
				dst += 4;
			}
		}
		if (width)
			*width = sw;
		if (height)
			*height = sh;
		return (int)bytes;
	}
	int src_w = wf->imageWidth();
	int h = wf->imageHeight();
	const RGBI* src = wf->imageData();
	if (!src || src_w < 1 || h < 1)
		return 0;
	if (h > 1024)
		h = 1024;
	WfSlice slice = waterfall_slice();
	int w = slice.cols;
	int col0 = slice.col0;
	if (w < 1)
		w = 1;
	if (col0 < 0)
		col0 = 0;
	if (col0 >= src_w)
		col0 = src_w - 1;
	if (col0 + w > src_w)
		w = src_w - col0;
	if (w > 4096)
		w = 4096;
	long bytes = (long)w * h * (long)sizeof(RGBI);
	if (bytes > cap)
		return 0;
	unsigned char* dst = rgbx;
	const int row_bytes = w * (int)sizeof(RGBI);
	for (int y = 0; y < h; ++y) {
		const RGBI* row = src + (long)y * src_w + col0;
		std::memcpy(dst, row, (size_t)row_bytes);
		dst += row_bytes;
	}
	if (width)
		*width = w;
	if (height)
		*height = h;
	return (int)bytes;
}

int engine_macro_name(int index, char* buf, int cap)
{
	if (!buf || cap < 1)
		return 0;
	buf[0] = 0;
	if (index < 0 || index >= MAXMACROS)
		return 0;
	copy_str(buf, cap, macros.name[index].c_str());
	return 1;
}

int engine_macro_text(int index, char* buf, int cap)
{
	if (!buf || cap < 1)
		return 0;
	buf[0] = 0;
	if (index < 0 || index >= MAXMACROS)
		return 0;
	copy_str(buf, cap, macros.text[index].c_str());
	return 1;
}

void engine_macro_set(int index, const char* name, const char* text)
{
	if (!g_up || index < 0 || index >= MAXMACROS || !name || !text)
		return;
	macros.name[index].assign(name);
	macros.text[index].assign(text);
	macros.changed = true;
	macros.writeMacroFile();
}

void engine_set_operator(const char* call, const char* locator, const char* antenna)
{
	if (!g_up)
		return;
	if (call)
		progdefaults.myCall.assign(call);
	if (locator)
		progdefaults.myLocator.assign(locator);
	if (antenna)
		progdefaults.myAntenna.assign(antenna);
	if (inpMyCallsign)
		inpMyCallsign->value(progdefaults.myCall.c_str());
	if (inpMyLocator)
		inpMyLocator->value(progdefaults.myLocator.c_str());
	if (inpMyAntenna)
		inpMyAntenna->value(progdefaults.myAntenna.c_str());
}

static void set_input(Fl_Input2* box, const char* text)
{
	if (!box)
		return;
	if (!text)
		text = "";
	const char* cur = box->value();
	if (cur && std::strcmp(cur, text) == 0)
		return;
	box->value(text);
}

void engine_set_qso(const char* call, const char* name, const char* qth,
	const char* locator, const char* rst_out, const char* rst_in)
{
	if (!g_up)
		return;
	// The waterfall-only window never points inpLoc at a widget.
	if (!inpLoc)
		inpLoc = inpLoc1;
	set_input(inpCall, call);
	set_input(inpName, name);
	set_input(inpQth, qth);
	set_input(inpLoc, locator);
	set_input(inpRstOut, rst_out);
	set_input(inpRstIn, rst_in);
}

void engine_macro(int index)
{
	if (!g_up || index < 0 || index >= MAXMACROS)
		return;
	if (macros.text[index].empty())
		return;
	// This window has no macro-timer button. A countdown left running, or an
	// idle flag whose timeout never finished, used to ignore the click or
	// transmit without sending the saved text.
	stopMacroTimer();
	progStatus.skip_sked_macro = false;
	macro_idle_on = false;
	macros.execute(index);
}

int engine_browser_line(int row, char* buf, int cap)
{
	if (!buf || cap < 1)
		return 0;
	buf[0] = 0;
	if (!mainViewer || row < 0)
		return 0;
	int line = row + 1;
	int freq = mainViewer->freq(line);
	std::string text = mainViewer->line(line);
	if (freq <= 0 || freq >= 1000000)
		std::snprintf(buf, (size_t)cap, "%2d", line);
	else if (text.empty())
		std::snprintf(buf, (size_t)cap, "%2d %4d", line, freq);
	else
		std::snprintf(buf, (size_t)cap, "%2d %4d %s", line, freq, text.c_str());
	return 1;
}

void engine_browser_pick(int row)
{
	if (!mainViewer || !active_modem || row < 0)
		return;
	int freq = mainViewer->freq(row + 1);
	if (freq <= 0 || freq >= 1000000)
		return;
	active_modem->set_freq(freq);
	active_modem->set_sigsearch(SIGSEARCH);
}

void engine_browser_clear_line(int row)
{
	if (!active_modem || row < 0)
		return;
	int sel = row + 1;
	int ch = progdefaults.VIEWERascend ? progdefaults.VIEWERchannels - sel : row;
	active_modem->clear_ch(ch);
}

void engine_browser_clear()
{
	if (active_modem)
		active_modem->clear_viewer();
	if (mainViewer)
		mainViewer->clear();
}
