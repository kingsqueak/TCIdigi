// ---------------------------------------------------------------------
// export_prefs.h
//
// Copyright (C) 2025
//		Dave Freese, W1HKJ
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
// ---------------------------------------------------------------------
//
// Save all floating point values as integers
//
// int_fval = fval * NNN where NNN is a factor of 10
//
// restore using fval = int_fval / NNN
//
// A work around for a bug in class preferences.  Read/Write of floating
// point values fails on read if locale is not EN_...
//
//---------------------------------------------------------------------- 

#ifndef EXPORT_PREFS_H
#define EXPORT_PREFS_H

#include <string>
#include <FL/Fl.H>
#include <FL/Enumerations.H>

#include "lgbook.h"

struct export_prefs {

	int Call;
	int Name;
	int Freq;
	int Band;
	int Mode;
	int QSOdateOn;
	int QSOdateOff;
	int TimeON;
	int TimeOFF;
	int TX_pwr;
	int RSTsent;
	int RSTrcvd;
	int Qth;
	int LOC;
	int State;
	int Age;

	int StaCall;
	int StaGrid;
	int StaCity;
	int Operator;
	int Province;
	int Country;
	int Notes;
	int QSLrcvd;
	int QSLsent;
	int eQSLrcvd;
	int eQSLsent;
	int LOTWrcvd;
	int LOTWsent;
	int QSL_VIA;
	int SerialIN;
	int SerialOUT;

	int Check;
	int XchgIn;
	int MyXchg;
	int CNTY;
	int CONT;
	int CQZ;
	int DXCC;
	int IOTA;
	int ITUZ;
	int Class;
	int Section;
	int cwss_serno;
	int cwss_prec;
	int cwss_check;
	int tenten;

	void save_defaults();
	void load_defaults();
};

extern export_prefs export_defaults;

#endif
