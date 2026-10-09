// ----------------------------------------------------------------------------
// engine.h -- modem and radio calls for the Qt window
//
// Copyright (C) 2026
//
// This file is part of fldigi. GPL-3.0-or-later.
//
// No FLTK and no Qt types. The Qt window and the modem call these on the
// GUI thread. engine_tick pumps fldigi timers and the cross-thread queue.
// ----------------------------------------------------------------------------

#ifndef FLDIGI_QTUI_ENGINE_H
#define FLDIGI_QTUI_ENGINE_H

struct EngineView {
	long long frequency_hz;
	int carrier_hz;
	int bandwidth_hz;
	int hz_low;
	int hz_high;
	int afc;
	// 1 when the current modem can track. CW, Olivia, Thor, and MT63 cannot.
	int afc_ok;
	int sql;
	int spot;
	int rx_id;
	int tx_id;
	int tuning;
	int transmitting;
	// Seconds left on <TIMER:nn>. 0 when no macro timer is running.
	int macro_timer;
	int reverse;
	int locked;
	int squelch;
	int level;
	int wf_mode;
	int wf_mag;
	int wf_speed;
	int tci;
	int tci_audio;
	double tx_db;
	double rx_db;
	double viewer_sql;
	double viewer_sql_lo;
	double viewer_sql_hi;
	// 0 when this mode has no multi-channel browser.
	int viewer_sql_ok;
	char rig_mode[32];
	char rig_filter[48];
	char modem[32];
	char status[160];
	char signal_report[48];
	char modem_status[64];
};

int engine_up();
int engine_start(int argc, char** argv);

// QRZ XML login and the operator call from the fldigi configuration.
// Empty strings when the engine is down. Buffers are always terminated.
void engine_qrz_account(char* user, int user_cap, char* password, int pass_cap,
	char* call, int call_cap, char* grid, int grid_cap,
	char* antenna, int antenna_cap);
void engine_tick();
void engine_view(EngineView* out);

// Newly received bytes. Returns -1 when the receive buffer was cleared.
int engine_rx_delta(char* buf, int cap);
void engine_clear_rx();
void engine_clear_tx();
int engine_tx_text(char* buf, int cap);
void engine_set_tx_text(const char* text);

int engine_modem_count();
int engine_modem_name(int index, char* buf, int cap);
int engine_modem_label(int index, char* buf, int cap);
// Category shown in the modem menu. Returns 0 for modes the menu does not list.
int engine_modem_group(int index, char* buf, int cap);
void engine_set_modem(const char* name);

// Hell paints a picture into the receive pane. Other modes stay on text.
int engine_rx_is_picture();
// Grayscale image. Returns the byte count, -1 when cap is short, or 0 when none.
int engine_rx_picture(unsigned char* gray, int cap, int* width, int* height);
// Size the picture buffer to the receive pane, in pixels.
void engine_rx_picture_size(int width, int height);
void engine_rx_picture_clear();
void engine_set_rig_mode(const char* name);
void engine_set_rig_filter(const char* name);
void engine_set_afc(int on);
void engine_set_sql(int on);
// Turning spotting on starts PSK Reporter. Returns 0 when that start fails;
// engine_spot_error() then says what is missing. Turning it off returns 1.
int engine_set_spot(int on);
const char* engine_spot_error();
void engine_set_rxid(int on);
void engine_set_txid(int on);

// Operator, station, and PSK Reporter. Buffers are always terminated.
struct EngineStation {
	char call[32];
	char oper_call[32];
	char name[80];
	char qth[80];
	char grid[16];
	char antenna[80];
	char psk_host[128];
	char psk_port[16];
	int psk_qrg;
};

void engine_station(EngineStation* out);
// Restarts PSK Reporter when it is already on and the identity changed.
void engine_set_station(const EngineStation* in);
void engine_tune(int on);
void engine_transmit(int on);
// Leave transmit or tune at once and come back in receive.
void engine_abort();
void engine_set_tx_db(double db);
void engine_set_rx_db(double db);

struct EngineTci {
	int enable;
	int audio;
	int rx;
	char host[128];
	char port[32];
	char status[160];
};

void engine_tci(EngineTci* out);
void engine_set_tci(int enable, int audio, const char* host, const char* port, int rx);
void engine_set_squelch(int value);
void engine_set_viewer_sql(double db);
// search arms acquisition. A waterfall click passes 1. A typed carrier passes 0.
void engine_set_carrier(int hz, int search);
void engine_set_frequency(long long hz);
void engine_set_reverse(int on);
void engine_set_lock(int on);
void engine_wf_mode(int mode);
void engine_wf_mag(int mag);
void engine_wf_speed(int speed);
void engine_wf_slew(int dir);
void engine_wf_center();
void engine_wf_ref(int db);
void engine_wf_span(int db);
void engine_qsy();

// Remember the current mode, dial, and audio frequency. Right-click recalls.
// Recalling a different dial frequency moves the radio.
// engine_store writes a short status line into buf. Returns the list length,
// 0 when that triple was already stored, or -1 when the modem is down.
int engine_store(char* buf, int cap);
int engine_store_count();
int engine_store_label(int index, char* buf, int cap);
void engine_store_select(int index);
void engine_store_clear();

// RGBX pixels, row stride width*4. Returns the byte count, or 0.
int engine_waterfall(unsigned char* rgbx, int cap, int* width, int* height);

int engine_macro_name(int index, char* buf, int cap);
int engine_macro_text(int index, char* buf, int cap);
void engine_macro_set(int index, const char* name, const char* text);
void engine_macro(int index);

// <MYCALL> and <MYLOC>. A null pointer leaves that value unchanged.
void engine_set_operator(const char* call, const char* locator, const char* antenna);
// <CALL>, <NAME>, <QTH>, <LOC>, <RST>, and <MYRST>.
void engine_set_qso(const char* call, const char* name, const char* qth,
	const char* locator, const char* rst_out, const char* rst_in);
int engine_browser_line(int row, char* buf, int cap);
void engine_browser_pick(int row);
void engine_browser_clear_line(int row);
void engine_browser_clear();

#endif
