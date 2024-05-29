//---------------------------------------------------------------------
// ui_colors.cxx
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
#include <config.h>
#include <iostream>
#include <fstream>

#include "ui_colors.h"

#include "main.h"
#include "waterfall.h"

UI_COLORS ui_colors = { // red, green, blue, var
  {  192, 192, 192,	"background" },
  {  255, 255, 255,	"background2" },
  {  0, 0, 0,		"foreground" },
  {  255, 255, 255,	"rsid" },
  {  0, 0, 0,		"digi_background" },
  {  255, 255, 255,	"digi_axis_color" },
  {  0, 255, 0,		"digi_color_1" },
  {  255, 255, 0,	"digi_color_2" },
  {  0, 255, 255,	"digi_color_3" },
  {  255, 0, 255,	"digi_color_4" },
  {  255, 255, 0,	"cursorLine" },
  {  255, 255, 255,	"cursorCenter" },
  {  255, 0, 0,		"bwTrack" },
  {  255, 255, 255,	"notch" },
  {  255, 120, 0,	"rttymark" },
  {  0, 255, 0,		"monitor" },
  {  185, 211, 238,	"bwsrSliderColor" },
  {  54, 100, 139,	"bwsrSldrSelColor" },
  {  255, 0, 0,		"bwsrHiLight1" },
  {  0, 255, 0,		"bwsrHiLight2" },
  {  255, 255, 255,	"bwsrBackgnd1" },
  {  234, 234, 234,	"bwsrBackgnd2" },
  {  0, 0, 255,		"bwsrSelect" },
  {  255, 110, 180,	"dup_color" },
  {  255, 255, 150,	"possible_dup_color" },
  {  0, 0, 130,		"DX_Color" },
  {  255, 255, 225,	"DXC_even_color" },
  {  191, 218, 255,	"DXC_odd_color" },
  {  255, 255, 0,	"DXC_textcolor" },
  {  255, 255, 0,	"DXfontcolor" },
  {  127, 0, 0,		"DXalt_color" },
  {  0, 0, 0,		"cfgpal0" },
  {  0, 0, 136,		"cfgpal1" },
  {  0, 19, 198,	"cfgpal2" },
  {  0, 32, 239,	"cfgpal3" },
  {  172, 167, 105,	"cfgpal4" },
  {  195, 198, 49,	"cfgpal5" },
  {  225, 228, 107,	"cfgpal6" },
  {  255, 255, 0,	"cfgpal7" },
  {  251, 51, 0,	"cfgpal8" },
  {  0, 0, 0,		"RxFontcolor" },
  {  255, 255, 255,	"MacroBtnFontcolor" },
  {  255, 0, 255,	"RxTxSelectcolor" },
  {  255, 0, 0,		"TxFontWarn" },
  {  0, 0, 0,		"TxFontcolor" },
  {  255, 242, 190,	"RxColor" },
  {  200, 235, 255,	"TxColor" },
  {  255, 0, 0,		"XMITcolor" },
  {  0, 145, 0,		"CTRLcolor" },
  {  0, 0, 255,		"SKIPcolor" },
  {  127, 0, 127,	"ALTRcolor" },
  {  0, 0, 0,		"LowSignal" },
  {  0, 255, 0,		"NormSignal	" },
  {  255, 255, 0,	"HighSignal	" },
  {  255, 0, 0,		"OverSignal	" },
  {  0, 0, 0,		"LOGGINGtextcolor" },
  {  255, 255, 255,	"LOGGINGcolor" },
  {  0, 0, 0,		"LOGBOOKtextcolor" },
  {  255, 255, 255,	"LOGBOOKcolor" },
  {  255, 253, 222,	"FDbackground" },
  {  0, 0, 0,		"FDforeground" },
  {  255, 253, 222,	"Smeter_bg_color" },
  {  0, 0, 0,		"Smeter_scale_color" },
  {  0, 200, 0,		"Smeter_meter_color" },
  {  255, 253, 222,	"PWRmeter_bg_color" },
  {  0, 0, 0,		"PWRmeter_scale_color "},
  {  200, 0, 0,		"PWRmeter_meter_color" },
  {  255, 255, 255,	"TabsColor" },
  {  255, 255, 0,	"Sql1Color" },
  {  0, 255, 0,		"Sql2Color" },
  {  0, 255, 0,		"AfcColor" },
  {  0, 255, 0,		"LkColor" },
  {  0, 255, 0,		"RevColor" },
  {  255, 0, 0,		"XmtColor" },
  {  0, 255, 0,		"SpotColor" },
  {  0, 255, 0,		"RxIDColor" },
  {  127, 0, 0,		"RxIDwideColor" },
  {  0, 255, 0,		"TxIDColor" },
  {  255, 0, 0,		"TuneColor" },
  {  0, 255, 0,		"default_btn_color" },
  {  255, 255, 0,	"default_check_btn_color" },
  {  0, 255, 0,		"default_round_btn_color" },
  {  255, 255, 255,	"FMT_background" },
  {  0, 0, 127,		"FMT_unk_color" },
  {  0, 255, 0,		"FMT_ref_color" },
  {  0, 0, 0,		"FMT_legend_color" },
  {  0, 0, 0,		"FMT_axis_color" },
  {  80, 144, 144,	"btnGroup1" },
  {  144, 80, 80,	"btnGroup2" },
  {  80, 80, 144,	"btnGroup3" },
  {  0, 145, 0,		"fsq_undirected_color" },
  {  0, 0, 255,		"fsq_directed_color" },
  {  255, 0, 0,		"fsq_xmt_color" },
  {  255, 255, 255,	"btnFkeyTextColor" },
  {  0, 0, 0, "" }
};

void UI_COLORS::load(std::string fname)
{
	TRIAD triad;
	triad.var.reserve(100);

	std::string filename = HomeDir;
	filename.append("/").append(fname).append(".colors");
	std::ifstream clrfile;
	clrfile.open( filename.c_str());
	std::string line;
	char chline[100];
	char chvar[50];
	TRIAD *ptr;
	clrfile.getline( chline, 100 );
	if ( (line = chline).empty()) {
		clrfile.close();
		return;
	}
	while (!clrfile.eof()) {
		clrfile.getline( chline, 100 );
		line = chline;
		if (!line.empty()) {
			sscanf(line.c_str(), "%s (%hhu)(%hhu)(%hhu)", chvar, &triad.r, &triad.g, &triad.b);
			ptr = &background;
			while (!ptr->var.empty()) {
				if (ptr->var == chvar) {
					ptr->r = triad.r; ptr->g = triad.g, ptr->b = triad.b;
//			std::cout << "FOUND: " << chvar << ", " << (uint)triad.r << ", " << (uint)triad.g << ", " << (uint)triad.b << std::endl;
					break;
				}
				ptr++;
			}
		}
	}
	clrfile.close();
}

char * UI_COLORS::rgbstr(TRIAD clr)
{
	static char sz[50];
	snprintf(sz, sizeof(sz), "(%u)(%u)(%u)", clr.r, clr.g, clr.b);
	return sz;
}

void UI_COLORS::save(std::string fname)
{

	std::string filename = HomeDir;
	filename.append("/").append(fname).append(".colors");
	FILE *clrfile = fopen( filename.c_str(), "w");

	TRIAD *ptr = &background;
	fprintf(clrfile, "fldigi color definition file: <control> (red)(green)(blue)\n");
	while (!ptr->var.empty()){
		fprintf(clrfile, "%s %s\n", ptr->var.c_str(), rgbstr(*ptr));
		ptr++;
	}
	fclose(clrfile);
}
