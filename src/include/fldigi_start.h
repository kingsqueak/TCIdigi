// ----------------------------------------------------------------------------
// fldigi_start.h -- start the modem without entering Fl::run
//
// Copyright (C) 2026
//
// This file is part of fldigi. GPL-3.0-or-later.
// ----------------------------------------------------------------------------

#ifndef FLDIGI_START_H
#define FLDIGI_START_H

// hide_ui is 0 for the FLTK program. The Qt program passes 1 so the FLTK
// window stays hidden and the caller pumps timers itself.
int fldigi_start(int argc, char** argv, int hide_ui);

#endif
