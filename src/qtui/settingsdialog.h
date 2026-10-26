// ----------------------------------------------------------------------------
// settingsdialog.h -- fldigi settings pages in the Qt shell
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

#ifndef FLDIGI_QTUI_SETTINGSDIALOG_H
#define FLDIGI_QTUI_SETTINGSDIALOG_H

#include <vector>

class QWidget;
class QString;

// One line of a settings page. A Pref is one of fldigi's settings, named
// by the FLTK widget that edits it on fldigi's configuration dialog
// (cntCWspeed, selShift, ...): the page reads the widget's kind, value,
// range and choices, and Save sets the widget and runs its callback, so
// fldigi's own code applies the change. Label and tip replace the FLTK
// ones when given (FLTK labels are often terse, or lean on the layout).
struct SettingItem {
	enum Kind {
		Pref,		// key: FLTK widget name
		Heading,	// label: a section title
		Note,		// label: a sentence of explanation
		ModeSet,	// key: rsid_rx_modes, rsid_tx_modes, cwid_modes or videoid_modes
		// The check boxes that follow, up to the next item of another
		// kind, are one choice: shown as radio buttons, and Save sets only
		// the one chosen, whose fldigi callback clears the others.
		OneOf,
		// The check boxes that follow, up to the next item of another
		// kind, allow at most one: ticking one unticks the rest.
		AtMostOne,
		// Ends a OneOf or AtMostOne run where the next item would not.
		EndGroup
	};
	Kind kind;
	const char* key;
	const char* label;
	const char* tip;
	// Pref numbers only: unit after the value, such as " WPM" or " Hz".
	const char* suffix;
	// A Pref usable only while another on the same page is set a certain
	// way: "key=1" (ticked), "key=0" (not ticked), "key=text" (that choice)
	// or "key=last" (the last choice). fldigi greys these out on its page.
	const char* needs;
};

struct SettingPage {
	const char* title;
	std::vector<SettingItem> items;
};

// The pages of Configure > Modems, Configure > IDs, and the general
// pages (settingspages.cpp).
std::vector<SettingPage> modemSettingPages();
std::vector<SettingPage> idSettingPages();
std::vector<SettingPage> generalSettingPages();

// A dialog with the pages listed on the left. Save applies what changed,
// in page order, then saves fldigi's configuration; Close drops it.
void editSettings(QWidget* parent, const QString& title, const std::vector<SettingPage>& pages);

#endif
