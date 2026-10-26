// ----------------------------------------------------------------------------
// engine_wf.h -- fldigi's waterfall mouse actions for the Qt shell
//
// Copyright (C) 2026
//
// This file is part of fldigi.
//
// Fldigi is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// ----------------------------------------------------------------------------

#ifndef FLDIGI_QTUI_ENGINE_WF_H
#define FLDIGI_QTUI_ENGINE_WF_H

// modifiers: bit 0 Ctrl, bit 1 Alt or Meta, bit 2 Shift.
// Wheel steps are fldigi's sign: positive is wheel down.

// Frequency Measurement Test owns the click. Returns 1 when it did
// (Shift sets the unknown, Ctrl the reference) and the caller should stop.
int engine_wf_fmt(int hz, int modifiers);

// Drag on the frequency scale, in audio hertz since the last event
// (positive toward a higher audio frequency). Returns 1 when WaterfallQSY
// owns the gesture, including a zero move. Returns 0 when the caller
// should tune the carrier instead. Does not transmit.
int engine_wf_qsy_drag(int delta_hz);

// A left click that already tuned. Replays recent audio when that is
// the default, and selects a matching signal-browser channel.
void engine_wf_left_history();

// Ctrl left-click. Replays at the current carrier and does not move it.
void engine_wf_ctrl_left(int hz, int modifiers);

// Right button. phase 0 press, 1 drag, 2 release. Press tunes there for
// the hold and release restores it, unless Ctrl is held: that release
// moves the dial so the signal sits on the sweet spot.
void engine_wf_right(int phase, int hz, int modifiers);

// Left button released. clicks is 1 for a double-click. Inserts the
// click text where fldigi does, into the receive pane.
void engine_wf_left_release(int modifiers, int clicks);

// Middle button.
void engine_wf_toggle_afc();

// Option+Control. Toggles fldigi's notch marker. The picture already
// carries that dashed column, so the Qt view shows it.
void engine_wf_notch(int hz, int modifiers);

// area 0 is the picture, 1 the frequency scale.
void engine_wf_wheel(int area, int steps, int hz, int modifiers);

#endif
