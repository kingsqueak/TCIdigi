// ----------------------------------------------------------------------------
// tcidigi_tx.h -- the one transmit buffer for the Qt shell
//
// Copyright (C) 2026
//
// This file is part of fldigi. GPL-3.0-or-later.
//
// The modem reads this queue. The hidden FLTK transmit widget is not a
// second copy while tcidigi_tx_use(true) is set. The FLTK app never sets it.
// ----------------------------------------------------------------------------

#ifndef TCIDIGI_TX_H
#define TCIDIGI_TX_H

#include <string>

void tcidigi_tx_use(bool on);
bool tcidigi_tx_active();
void tcidigi_tx_clear();
void tcidigi_tx_clear_sent();
void tcidigi_tx_pause();
void tcidigi_tx_add(const std::string& text);
void tcidigi_tx_replace(const std::string& text);
// Apply a snapshot while transmitting. A shorter tail sends backspaces for
// characters the modem has already taken. Returns 0 if the sent text would
// have to change in the middle.
int tcidigi_tx_edit(const std::string& text);
std::string tcidigi_tx_get();
int tcidigi_tx_next();
bool tcidigi_tx_eot();

#endif
