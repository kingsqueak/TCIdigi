// ----------------------------------------------------------------------------
// tcidigi_rx.h -- received text as a stream for the Qt shell
//
// Copyright (C) 2026
//
// This file is part of fldigi. GPL-3.0-or-later.
//
// Every write to the receive pane lands here before the FLTK widget wraps
// or trims it. Only the Qt shell calls tcidigi_rx_use(true). The FLTK app
// leaves it off and its receive widget is unchanged.
// ----------------------------------------------------------------------------

#ifndef TCIDIGI_RX_H
#define TCIDIGI_RX_H

#include <string>

// What a received character is, for the Qt shell to color it: received
// text, the echo of transmitted text, a clickable mark (RsID and SigID
// "<<...>>" marks; pass the whole mark to activate_qrg_mark), or anything
// else fldigi writes there (macro and program notices).
enum { TCIDIGI_RX_RECV = 0, TCIDIGI_RX_XMIT = 1, TCIDIGI_RX_NOTE = 2, TCIDIGI_RX_LINK = 3 };

void tcidigi_rx_use(bool on);
bool tcidigi_rx_active();
// One character as FTextRX::add receives it. '\b' erases one character.
void tcidigi_rx_add(unsigned int c, int attr);
void tcidigi_rx_clear();
// Text added since the last take, split on a UTF-8 boundary. A '\b' in the
// result erases the character before it, which may be one taken earlier.
// Returns the byte count, or -1 once after a clear; the text added after
// that clear comes with the next take. kinds gets one TCIDIGI_RX_* byte for
// each byte in buf and must hold cap bytes.
int tcidigi_rx_take(char* buf, char* kinds, int cap);
// Everything received, backspaces applied. The oldest text is dropped past
// a few megabytes.
std::string tcidigi_rx_text();

#endif
