// ----------------------------------------------------------------------------
//
//    ptt.h --  PTT control
//
// Copyright (C) 2006-2009
//		Dave Freese, W1HKJ
// Copyright (C) 2008-2009
//		Stelios Bounanos, M0GLD
//
// This file is part of fldigi.  Adapted from code contained in gmfsk source code 
// distribution.
//  gmfsk Copyright (C) 2001, 2002, 2003
//  Tomi Manninen (oh2bns@sral.fi)
//  Copyright (C) 2004
//  Lawrence Glaister (ve7it@shaw.ca)
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
// ----------------------------------------------------------------------------

#ifndef PTT_H_
#define PTT_H_

#include <config.h>

#if HAVE_LINUX_PPDEV_H || HAVE_DEV_PPBUS_PPI_H
#  define HAVE_PARPORT 1
#else
#  define HAVE_PARPORT 0
#endif

#ifdef __APPLE__
#  define HAVE_UHROUTER 1
#  define UHROUTER_FIFO_PREFIX "/tmp/microHamRouter"
#else
#  define HAVE_UHROUTER 0
#endif

#if HAVE_TERMIOS_H
#  define HAVE_TTYPORT 1
#else
#  define HAVE_TTYPORT 0
#endif

struct termios;

//#ifdef __MINGW32__
#  include "serial.h"
//#endif

// 2026-10-08: TCIdigi keys the radio through TCI. The enum values stay so
// the hidden FLTK rig pages still compile. reset() does not open a port.
class PTT {
public:
	enum ptt_t {
		PTT_INVALID = -1, PTT_NONE, PTT_HAMLIB,
		PTT_RIGCAT, PTT_TTY, PTT_PARPORT, PTT_UHROUTER, PTT_GPIO, PTT_CMEDIA
	};

	PTT(ptt_t dev = PTT_NONE);
	~PTT();
	void set(bool on);
	void reset(ptt_t dev);
	// CW keyline can address this object. This cut never opens it.
	Cserial serPort;
private:
	ptt_t pttdev;
	void close_all(void);
};


#endif // PTT_H_
