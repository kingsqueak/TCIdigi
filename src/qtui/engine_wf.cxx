// ----------------------------------------------------------------------------
// engine_wf.cxx -- fldigi's waterfall mouse actions for the Qt shell
//
// Copyright (C) 2026
//
// This file is part of fldigi. GPL-3.0-or-later.
//
// The Qt waterfall asks these to do what WFdisp::handle does. They call
// fldigi's own waterfall, qsy, and text insert. None of them transmit.
// ----------------------------------------------------------------------------

#include <config.h>

#include "engine_wf.h"

#include "engine.h"

#include "configuration.h"
#include "fl_digi.h"
#include "status.h"
#include "trx.h"

#include <FL/Fl_Group.H>

// Declared here so this file does not pull in the viewer and FMT dialogs.
extern void set_unk_freq_value(double f);
extern void set_ref_freq_value(double f);
extern void viewer_paste_freq(int freq);
extern void recenter_spectrum_viewer();

namespace {

// Right-click listens, then release puts the carrier back. -1 means no hold.
int g_right_saved = -1;

// wfdisp is private. It is nested in the waterfall group, and tmp_carrier
// on it is public: received text during a right-click hold is the alternate
// stream, then release clears the flag.
WFdisp* find_disp(Fl_Group* group, int depth)
{
	if (!group || depth > 4)
		return nullptr;
	for (int i = 0; i < group->children(); ++i) {
		Fl_Widget* child = group->child(i);
		WFdisp* disp = dynamic_cast<WFdisp*>(child);
		if (disp)
			return disp;
		Fl_Group* nested = dynamic_cast<Fl_Group*>(child);
		if (!nested)
			continue;
		WFdisp* found = find_disp(nested, depth + 1);
		if (found)
			return found;
	}
	return nullptr;
}

WFdisp* wf_display()
{
	static WFdisp* cached = nullptr;
	if (cached || !wf)
		return cached;
	cached = find_disp(wf, 0);
	return cached;
}

void set_tmp_carrier(bool on)
{
	if (WFdisp* disp = wf_display())
		disp->tmp_carrier = on;
}

int receiving()
{
	return engine_up() && wf && active_modem && trx_state == STATE_RX;
}

} // namespace

int engine_wf_fmt(int hz, int modifiers)
{
	if (!engine_up() || !active_modem || active_modem != fmt_modem)
		return 0;
	// Not receiving: fldigi swallows the event and changes nothing.
	if (trx_state != STATE_RX)
		return 1;
	if (modifiers & 4)
		set_unk_freq_value(hz);
	else if (modifiers & 1)
		set_ref_freq_value(hz);
	return 1;
}

int engine_wf_qsy_drag(int delta_hz)
{
	if (!engine_up() || !wf || !active_modem)
		return 0;
	// FMT and transmit both consume the gesture so the caller does not tune.
	if (active_modem == fmt_modem || trx_state != STATE_RX)
		return 1;
	if (!progdefaults.WaterfallQSY)
		return 0;
	stopMacroTimer();
	// A press with no movement is still the scale gesture. The dial stays.
	if (delta_hz == 0)
		return 1;
	// Same sign as WFdisp: dragging toward a higher audio frequency lowers
	// a USB dial and raises an LSB dial, so the grabbed mark follows.
	long long shift = delta_hz;
	if (!wf->USB())
		shift = -shift;
	long long next = (long long)wf->rfcarrier() - shift;
	if (next < 0)
		next = 0;
	int carrier = (int)active_modem->get_freq();
	if (carrier <= 0)
		carrier = 1500;
	qsy((unsigned long long)next, carrier);
	return 1;
}

void engine_wf_left_history()
{
	if (!receiving() || active_modem == fmt_modem)
		return;
	stopMacroTimer();
	if (progdefaults.WaterfallHistoryDefault)
		bHistory = true;
	viewer_paste_freq((int)active_modem->get_freq());
}

void engine_wf_ctrl_left(int hz, int modifiers)
{
	if (!engine_up() || !active_modem || !wf)
		return;
	if (trx_state != STATE_RX)
		return;
	if (engine_wf_fmt(hz, modifiers))
		return;
	stopMacroTimer();
	// At the carrier that is already set. The click frequency is only for FMT.
	bHistory = true;
}

void engine_wf_right(int phase, int hz, int modifiers)
{
	if (!engine_up() || !active_modem || !wf)
		return;
	if (trx_state != STATE_RX) {
		if (phase == 2)
			g_right_saved = -1;
		return;
	}
	if (engine_wf_fmt(hz, modifiers))
		return;
	stopMacroTimer();
	const int ctrl = (modifiers & 1) != 0;
	const int shift = (modifiers & 4) != 0;
	if (phase == 2) {
		set_tmp_carrier(false);
		if (ctrl)
			do_qsy(true);
		else if (g_right_saved >= 0)
			engine_set_carrier(g_right_saved, 0);
		g_right_saved = -1;
		return;
	}
	if (phase == 0) {
		g_right_saved = (int)active_modem->get_freq();
		set_tmp_carrier(true);
		// Ctrl always replays. A plain right click replays only when that
		// is the default for every click.
		if (progdefaults.WaterfallHistoryDefault || ctrl)
			bHistory = true;
	}
	// Ctrl right-click tunes once. The drag does not follow the pointer.
	if (phase == 1 && ctrl)
		return;
	// WFdisp clamps this tune to the audio band.
	int half = (int)(active_modem->get_bandwidth() / 2.0);
	int lo = progdefaults.LowFreqCutoff + half;
	int hi = progdefaults.HighFreqCutoff - half;
	int tuned = hz;
	if (tuned < lo)
		tuned = lo;
	if (tuned > hi)
		tuned = hi;
	engine_set_carrier(tuned, shift ? 0 : 1);
	viewer_paste_freq((int)active_modem->get_freq());
}

void engine_wf_left_release(int modifiers, int clicks)
{
	if (!receiving())
		return;
	if (active_modem == fmt_modem)
		return;
	// fldigi also requires the FLTK receive widget to be visible. That
	// window is hidden here, so the check would drop the text. insert_text
	// writes the receive pane, and the Qt shell reads that stream.
	recenter_spectrum_viewer();
	if ((modifiers & (1 | 2 | 4)) == 0) {
		if (clicks)
			note_qrg(true, "\n", "\n");
		else if (progdefaults.WaterfallClickInsert)
			wf->insert_text(true);
	}
	// Alt or Meta would call find_signal_text. That function is static in
	// waterfall.cxx and only moves the hidden receive cursor.
}

void engine_wf_toggle_afc()
{
	if (!receiving())
		return;
	if (active_modem == fmt_modem)
		return;
	stopMacroTimer();
	set_afc(!progStatus.afconoff);
}

void engine_wf_notch(int hz, int modifiers)
{
	if (!engine_up() || !active_modem || !wf)
		return;
	if (trx_state != STATE_RX)
		return;
	// FMT owns Alt+Control: Control sets the reference and there is no notch.
	if (engine_wf_fmt(hz, modifiers))
		return;
	stopMacroTimer();
	// A second Option-Control click clears it, wherever the pointer is.
	if (notch_frequency)
		notch_off();
	else
		notch_on(hz);
}

void engine_wf_wheel(int area, int steps, int hz, int modifiers)
{
	if (!engine_up() || !wf || steps == 0)
		return;
	if (trx_state != STATE_RX)
		return;
	if (engine_wf_fmt(hz, modifiers))
		return;
	stopMacroTimer();
	if (modifiers & 1) {
		wf->handle_mouse_wheel(waterfall::WF_AFC_BW, steps);
		return;
	}
	if (modifiers & 2) {
		wf->handle_mouse_wheel(waterfall::WF_SIGNAL_SEARCH, steps);
		return;
	}
	if (modifiers & 4) {
		wf->handle_mouse_wheel(waterfall::WF_SQUELCH, steps);
		return;
	}
	// The scale steps the dial only while scale-drag QSY is on. A wheel
	// over the picture uses the configured action either way.
	if (area == 1 && progdefaults.WaterfallQSY) {
		long long next = (long long)wf->rfcarrier() - 500LL * steps;
		if (next < 0)
			next = 0;
		qsy((unsigned long long)next);
		return;
	}
	wf->handle_mouse_wheel(progdefaults.WaterfallWheelAction, steps);
}
