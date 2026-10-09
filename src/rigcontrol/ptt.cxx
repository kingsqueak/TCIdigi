// ----------------------------------------------------------------------------
//
//    ptt.cxx --  PTT control
//
// Copyright (C) 2006-2009
//		Dave Freese, W1HKJ
// Copyright (C) 2008-2009
//		Stelios Bounanos, M0GLD
// Copyright (C) 2009
//		Diane Bruce, VA3DB
//
// Added gpio for PTT (Lior KK6BWA)
//
// This file is part of fldigi.  Adapted from code contained in gmfsk source code
// distribution.
//  gmfsk Copyright (C) 2001, 2002, 2003
//  Tomi Manninen (oh2bns@sral.fi)
//  Copyright (C) 2004
//  Lawrence Glaister (ve7it@shaw.ca)
//
// Fldigi is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// 2026-10-08 TCIdigi: PTT is TCI when that client is up, otherwise the
// no-rig store. Serial TTY, GPIO, parallel, uhrouter, hamlib, rigCAT,
// and C-Media PTT are not opened.
//
// ----------------------------------------------------------------------------

#include <config.h>

#include "ptt.h"
#include "configuration.h"
#include "rigio.h"
#include "debug.h"
#include "tci.h"
#include "trx.h"
#include "fl_digi.h"
#include "cw.h"
#include "nanoIO.h"
#include "misc.h"

LOG_FILE_SOURCE(debug::LOG_RIGCONTROL);

PTT::PTT(ptt_t dev) : pttdev(PTT_INVALID)
{
	reset(dev);
}

PTT::~PTT()
{
	close_all();
}

void PTT::reset(ptt_t)
{
	close_all();
	pttdev = PTT_NONE;
	set(false);
}

void PTT::set(bool ptt)
{
	LOG_INFO("PTT via %s : %s", tci_is_active() ? "TCI" : "NONE", ptt ? "ON" : "OFF");

	if (!ptt && progdefaults.PTT_off_delay)
		MilliSleep(progdefaults.PTT_off_delay);

	if (active_modem == cw_modem &&
	    (CW_KEYLINE_isopen || progdefaults.CW_KEYLINE_on_cat_port)) {
		guard_lock lk(&cwio_ptt_mutex);
	}

	if (tci_is_active())
		tci_set_ptt(ptt);
	else
		noCAT_setPTT(ptt);

	nano_PTT(ptt);

	if (ptt && progdefaults.PTT_on_delay)
		MilliSleep(progdefaults.PTT_on_delay);

	if (ptt) start_tx_timer();
	else     stop_tx_timer();
}

void PTT::close_all(void)
{
	set(false);
	pttdev = PTT_NONE;
}
