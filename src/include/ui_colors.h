// ---------------------------------------------------------------------
// ui_colors.h
//
// Copyright (C) 2024
//		Dave Freese; W1HKJ
//
// This file is part of fldigi.
//
// Fldigi is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License; or
// (at your option) any later version.
//
// Fldigi is distributed in the hope that it will be useful;
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with fldigi.  If not; see <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------

#ifndef UI_COLORS_H
#define UI_COLORS_H

#include <iostream>
#include <fstream>
#include <string>

#include <FL/Fl.H>

#define RGBCOLOR(var)  fl_rgb_color( ui_colors.var.r, ui_colors.var.g, ui_colors.var.b )

struct TRIAD { uchar r; uchar g; uchar b; std::string var; };

struct UI_COLORS {
	TRIAD  background;
	TRIAD  background2;
	TRIAD  foreground;
	TRIAD  rsid;
	TRIAD  digi_background;
	TRIAD  digi_axis_color;
	TRIAD  digi_color_1;
	TRIAD  digi_color_2;
	TRIAD  digi_color_3;
	TRIAD  digi_color_4;
	TRIAD  cursorLine;
	TRIAD  cursorCenter;
	TRIAD  bwTrack;
	TRIAD  notch;
	TRIAD  rttymark;
	TRIAD  monitor;
	TRIAD  bwsrSliderColor;
	TRIAD  bwsrSldrSelColor;
	TRIAD  bwsrHiLight1;
	TRIAD  bwsrHiLight2;
	TRIAD  bwsrBackgnd1;
	TRIAD  bwsrBackgnd2;
	TRIAD  bwsrSelect;
	TRIAD  dup_color;
	TRIAD  possible_dup_color;
	TRIAD  DX_Color;
	TRIAD  DXC_even_color;
	TRIAD  DXC_odd_color;
	TRIAD  DXC_textcolor;
	TRIAD  DXfontcolor;
	TRIAD  DXalt_color;
	TRIAD  cfgpal0;
	TRIAD  cfgpal1;
	TRIAD  cfgpal2;
	TRIAD  cfgpal3;
	TRIAD  cfgpal4;
	TRIAD  cfgpal5;
	TRIAD  cfgpal6;
	TRIAD  cfgpal7;
	TRIAD  cfgpal8;
	TRIAD  RxFontcolor;
	TRIAD  MacroBtnFontcolor;
	TRIAD  RxTxSelectcolor;
	TRIAD  TxFontWarn;
	TRIAD  TxFontcolor;
	TRIAD  RxColor;
	TRIAD  TxColor;
	TRIAD  XMITcolor;
	TRIAD  CTRLcolor;
	TRIAD  SKIPcolor;
	TRIAD  ALTRcolor;
	TRIAD  LowSignal;
	TRIAD  NormSignal;
	TRIAD  HighSignal;
	TRIAD  OverSignal;
	TRIAD  LOGGINGtextcolor;
	TRIAD  LOGGINGcolor;
	TRIAD  LOGBOOKtextcolor;
	TRIAD  LOGBOOKcolor;
	TRIAD  FDbackground;
	TRIAD  FDforeground;
	TRIAD  Smeter_bg_color;
	TRIAD  Smeter_scale_color;
	TRIAD  Smeter_meter_color;
	TRIAD  PWRmeter_bg_color;
	TRIAD  PWRmeter_scale_color;
	TRIAD  PWRmeter_meter_color;
	TRIAD  TabsColor;
	TRIAD  Sql1Color;
	TRIAD  Sql2Color;
	TRIAD  AfcColor;
	TRIAD  LkColor;
	TRIAD  RevColor;
	TRIAD  XmtColor;
	TRIAD  SpotColor;
	TRIAD  RxIDColor;
	TRIAD  RxIDwideColor;
	TRIAD  TxIDColor;
	TRIAD  TuneColor;
	TRIAD  default_btn_color;
	TRIAD  default_check_btn_color;
	TRIAD  default_round_btn_color;
	TRIAD  FMT_background;
	TRIAD  FMT_unk_color;
	TRIAD  FMT_ref_color;
	TRIAD  FMT_legend_color;
	TRIAD  FMT_axis_color;
	TRIAD  btnGroup1;
	TRIAD  btnGroup2;
	TRIAD  btnGroup3;

	TRIAD  fsq_undirected_color;
	TRIAD  fsq_directed_color;
	TRIAD  fsq_xmt_color;

	TRIAD  btnFkeyTextColor;

	TRIAD terminator;

	char * rgbstr(TRIAD clr);
	void load(std::string);
	void save(std::string);

};

#endif

extern UI_COLORS ui_colors;

