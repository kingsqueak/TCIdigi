// ---------------------------------------------------------------------
// export_prefs.cxx
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

#include <config.h>

#include <iostream>
#include <fstream>
#include <string>

#include <FL/Fl_Preferences.H>

#include "gettext.h"

#include "main.h"
#include "export_prefs.h"
#include "lgbook.h"

#define EXPORT_PREFS_FILENAME "export_prefs"

export_prefs export_defaults = {

	1, // Call = 1;
	1, // Name = 1;
	1, // Freq = 1;
	1, // Band = 1;
	1, // Mode = 1;
	1, // QSOdateOn = 1;
	1, // QSOdateOff = 1;
	1, // TimeON = 1;
	1, // TimeOFF = 1;
	0, // TX_pwr = 0;
	1, // RSTsent = 1;
	1, // RSTrcvd = 1;
	0, // Qth = 0;
	0, // LOC = 0;
	0, // State = 0;
	0, // Age = 0;

	0, // StaCall = 0;
	0, // StaGrid = 0;
	0, // StaCity = 0;
	0, // Operator = 0;
	0, // Province = 0;
	0, // Country = 0;
	0, // Notes = 0;
	0, // QSLrcvd = 0;
	0, // QSLsent = 0;
	0, // eQSLrcvd = 0;
	0, // eQSLsent = 0;
	0, // LOTWrcvd = 0;
	0, // LOTWsent = 0;
	0, // QSL_VIA = 0;
	0, // SerialIN = 0;
	0, // SerialOUT = 0;

	0, // Check = 0;
	0, // XchgIn = 0;
	0, // MyXchg = 0;
	0, // CNTY = 0;
	0, // CONT = 0;
	0, // CQZ = 0;
	0, // DXCC = 0;
	0, // IOTA = 0;
	0, // ITUZ = 0;
	0, // Class = 0;
	0, // Section = 0;
	0, // cwss_serno = 0;
	0, // cwss_prec = 0;
	0, // cwss_check = 0;
	0, // tenten = 0;

};

void export_prefs::save_defaults()
{
#if FLDIGI_FLTK_API_MINOR < 4
	Fl_Preferences spref(HomeDir.c_str(), "w1hkj.org", EXPORT_PREFS_FILENAME);
#else
	Fl_Preferences spref(
		HomeDir.c_str(),
		"w1hkj.org",
		EXPORT_PREFS_FILENAME,
		Fl_Preferences::C_LOCALE);
#endif

	Call = btnSelectCall->value();
	Name = btnSelectName->value();
	Freq = btnSelectFreq->value();
	Band = btnSelectBand->value();
	Mode = btnSelectMode->value();
	QSOdateOn = btnSelectQSOdateOn->value();
	QSOdateOff = btnSelectQSOdateOff->value();
	TimeON = btnSelectTimeON->value();
	TimeOFF = btnSelectTimeOFF->value();
	TX_pwr = btnSelectTX_pwr->value();
	RSTsent = btnSelectRSTsent->value();
	RSTrcvd = btnSelectRSTrcvd->value();
	Qth = btnSelectQth->value();
	LOC = btnSelectLOC->value();
	State = btnSelectState->value();
	Age = btnSelectAge->value();

	StaCall = btnSelectStaCall->value();
	StaGrid = btnSelectStaGrid->value();
	StaCity = btnSelectStaCity->value();
	Operator = btnSelectOperator->value();
	Province = btnSelectProvince->value();
	Country = btnSelectCountry->value();
	Notes = btnSelectNotes->value();
	QSLrcvd = btnSelectQSLrcvd->value();
	QSLsent = btnSelectQSLsent->value();
	eQSLrcvd = btnSelecteQSLrcvd->value();
	eQSLsent = btnSelecteQSLsent->value();
	LOTWrcvd = btnSelectLOTWrcvd->value();
	LOTWsent = btnSelectLOTWsent->value();
	QSL_VIA = btnSelectQSL_VIA->value();
	SerialIN = btnSelectSerialIN->value();
	SerialOUT = btnSelectSerialOUT->value();

	Check = btnSelectCheck->value();
	XchgIn = btnSelectXchgIn->value();
	MyXchg = btnSelectMyXchg->value();
	CNTY = btnSelectCNTY->value();
	CONT = btnSelectCONT->value();
	CQZ = btnSelectCQZ->value();
	DXCC = btnSelectDXCC->value();
	IOTA = btnSelectIOTA->value();
	ITUZ = btnSelectITUZ->value();
	Class = btnSelectClass->value();
	Section = btnSelectSection->value();
	cwss_serno = btnSelect_cwss_serno->value();
	cwss_prec = btnSelect_cwss_prec->value();
	cwss_check = btnSelect_cwss_check->value();
	tenten = btnSelect_1010->value();

	spref.set("Call", Call);
	spref.set("Name", Name);
	spref.set("Freq", Freq);
	spref.set("Band", Band);
	spref.set("Mode", Mode);
	spref.set("QSOdateOn", QSOdateOn);
	spref.set("QSOdateOff", QSOdateOff);
	spref.set("TimeON", TimeON);
	spref.set("TimeOFF", TimeOFF);
	spref.set("TX_pwr", TX_pwr);
	spref.set("RSTsent", RSTsent);
	spref.set("RSTrcvd", RSTrcvd);
	spref.set("Qth", Qth);
	spref.set("LOC", LOC);
	spref.set("State", State);
	spref.set("Age", Age);

	spref.set("StaCall", StaCall);
	spref.set("StaGrid", StaGrid);
	spref.set("StaCity", StaCity);
	spref.set("Operator", Operator);
	spref.set("Province", Province);
	spref.set("Country", Country);
	spref.set("Notes", Notes);
	spref.set("QSLrcvd", QSLrcvd);
	spref.set("QSLsent", QSLsent);
	spref.set("eQSLrcvd", eQSLrcvd);
	spref.set("eQSLsent", eQSLsent);
	spref.set("LOTWrcvd", LOTWrcvd);
	spref.set("LOTWsent", LOTWsent);
	spref.set("QSL_VIA", QSL_VIA);
	spref.set("SerialIN", SerialIN);
	spref.set("SerialOUT", SerialOUT);

	spref.set("Check", Check);
	spref.set("XchgIn", XchgIn);
	spref.set("MyXchg", MyXchg);
	spref.set("CNTY", CNTY);
	spref.set("CONT", CONT);
	spref.set("CQZ", CQZ);
	spref.set("DXCC", DXCC);
	spref.set("IOTA", IOTA);
	spref.set("ITUZ", ITUZ);
	spref.set("Class", Class);
	spref.set("Section", Section);
	spref.set("cwss_serno", cwss_serno);
	spref.set("cwss_prec", cwss_prec);
	spref.set("cwss_check", cwss_check);
	spref.set("tenten", tenten);

}

void export_prefs::load_defaults()
{
#if FLDIGI_FLTK_API_MINOR < 4
	Fl_Preferences spref(HomeDir.c_str(), "w1hkj.org", EXPORT_PREFS_FILENAME);
#else
	Fl_Preferences spref(
		HomeDir.c_str(),
		"w1hkj.org",
		EXPORT_PREFS_FILENAME,
		Fl_Preferences::C_LOCALE);
#endif

	spref.get("Call", Call, Call);
	spref.get("Name", Name, Name);
	spref.get("Freq", Freq, Freq);
	spref.get("Band", Band, Band);
	spref.get("Mode", Mode, Mode);
	spref.get("QSOdateOn", QSOdateOn, QSOdateOn);
	spref.get("QSOdateOff", QSOdateOff, QSOdateOff);
	spref.get("TimeON", TimeON, TimeON);
	spref.get("TimeOFF", TimeOFF, TimeOFF);
	spref.get("TX_pwr", TX_pwr, TX_pwr);
	spref.get("RSTsent", RSTsent, RSTsent);
	spref.get("RSTrcvd", RSTrcvd, RSTrcvd);
	spref.get("Qth", Qth, Qth);
	spref.get("LOC", LOC, LOC);
	spref.get("State", State, State);
	spref.get("Age", Age, Age);

	spref.get("StaCall", StaCall, StaCall);
	spref.get("StaGrid", StaGrid, StaGrid);
	spref.get("StaCity", StaCity, StaCity);
	spref.get("Operator", Operator, Operator);
	spref.get("Province", Province, Province);
	spref.get("Country", Country, Country);
	spref.get("Notes", Notes, Notes);
	spref.get("QSLrcvd", QSLrcvd, QSLrcvd);
	spref.get("QSLsent", QSLsent, QSLsent);
	spref.get("eQSLrcvd", eQSLrcvd, eQSLrcvd);
	spref.get("eQSLsent", eQSLsent, eQSLsent);
	spref.get("LOTWrcvd", LOTWrcvd, LOTWrcvd);
	spref.get("LOTWsent", LOTWsent, LOTWsent);
	spref.get("QSL_VIA", QSL_VIA, QSL_VIA);
	spref.get("SerialIN", SerialIN, SerialIN);
	spref.get("SerialOUT", SerialOUT, SerialOUT);

	spref.get("Check", Check, Check);
	spref.get("XchgIn", XchgIn, XchgIn);
	spref.get("MyXchg", MyXchg, MyXchg);
	spref.get("CNTY", CNTY, CNTY);
	spref.get("CONT", CONT, CONT);
	spref.get("CQZ", CQZ, CQZ);
	spref.get("DXCC", DXCC, DXCC);
	spref.get("IOTA", IOTA, IOTA);
	spref.get("ITUZ", ITUZ, ITUZ);
	spref.get("Class", Class, Class);
	spref.get("Section", Section, Section);
	spref.get("cwss_serno", cwss_serno, cwss_serno);
	spref.get("cwss_prec", cwss_prec, cwss_prec);
	spref.get("cwss_check", cwss_check, cwss_check);
	spref.get("tenten", tenten, tenten);

	btnSelectCall->value(Call);
	btnSelectName->value(Name);
	btnSelectFreq->value(Freq);
	btnSelectBand->value(Band);
	btnSelectMode->value(Mode);
	btnSelectQSOdateOn->value(QSOdateOn);
	btnSelectQSOdateOff->value(QSOdateOff);
	btnSelectTimeON->value(TimeON);
	btnSelectTimeOFF->value(TimeOFF);
	btnSelectTX_pwr->value(TX_pwr);
	btnSelectRSTsent->value(RSTsent);
	btnSelectRSTrcvd->value(RSTrcvd);
	btnSelectQth->value(Qth);
	btnSelectLOC->value(LOC);
	btnSelectState->value(State);
	btnSelectAge->value(Age);

	btnSelectStaCall->value(StaCall);
	btnSelectStaGrid->value(StaGrid);
	btnSelectStaCity->value(StaCity);
	btnSelectOperator->value(Operator);
	btnSelectProvince->value(Province);
	btnSelectCountry->value(Country);
	btnSelectNotes->value(Notes);
	btnSelectQSLrcvd->value(QSLrcvd);
	btnSelectQSLsent->value(QSLsent);
	btnSelecteQSLrcvd->value(QSLrcvd);
	btnSelecteQSLsent->value(QSLsent);
	btnSelectLOTWrcvd->value(LOTWrcvd);
	btnSelectLOTWsent->value(LOTWsent);
	btnSelectQSL_VIA->value(QSL_VIA);
	btnSelectSerialIN->value(SerialIN);
	btnSelectSerialOUT->value(SerialOUT);

	btnSelectCheck->value(Check);
	btnSelectXchgIn->value(XchgIn);
	btnSelectMyXchg->value(MyXchg);
	btnSelectCNTY->value(CNTY);
	btnSelectCONT->value(CONT);
	btnSelectCQZ->value(CQZ);
	btnSelectDXCC->value(DXCC);
	btnSelectIOTA->value(IOTA);
	btnSelectITUZ->value(ITUZ);
	btnSelectClass->value(Class);
	btnSelectSection->value(Section);
	btnSelect_cwss_serno->value(cwss_serno);
	btnSelect_cwss_prec->value(cwss_prec);
	btnSelect_cwss_check->value(cwss_check);
	btnSelect_1010->value(tenten);
}
