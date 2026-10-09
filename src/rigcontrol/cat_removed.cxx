// ----------------------------------------------------------------------------
// cat_removed.cxx
//
// Copyright (C) 2006-2016 Dave Freese, W1HKJ, and others as in the
// fldigi files this replaces.
// Copyright (C) 2026 TCIdigi
//
// This file is part of fldigi.
//
// Fldigi is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// 2026-10-08: hamlib, rigCAT, flrig, and C-Media PTT are gone. These
// symbols remain so the hidden FLTK pages, config scripts, and macro
// tags still link. None of them open a serial port, a HID device, a
// GPIO line, or a socket to flrig.
// ----------------------------------------------------------------------------

#include <config.h>

#include <pthread.h>
#include <string>
#include <list>

#include "rigio.h"
#include "rigxml.h"
#include "rigsupport.h"
#include "cmedia.h"
#include "debug.h"

LOG_FILE_SOURCE(debug::LOG_RIGCONTROL);

Cserial rigio;
pthread_mutex_t rigCAT_mutex = PTHREAD_MUTEX_INITIALIZER;

std::list<XMLIOS> commands;
std::list<XMLIOS> reply;
std::list<MODE> lmodes;
std::list<MODE> lmodeCMD;
std::list<MODE> lmodeREPLY;
std::list<BW> lbws;
std::list<BW> lbwCMD;
std::list<BW> lbwREPLY;
std::list<std::string> LSBmodes;
XMLRIG xmlrig;

bool connected_to_flrig = false;

static void cat_unavailable(const char* what)
{
	LOG_INFO("%s is not available", what);
}

bool hexout(const std::string&) { return false; }

bool sendCommand(std::string, std::string, int, int) { return false; }

void add_to_cmdque(std::string, std::string, int, int) { }

long long rigCAT_getfreq(int, bool& failed, int)
{
	failed = true;
	return 0;
}

void rigCAT_setfreq(long long) { }
std::string rigCAT_getmode() { return std::string(); }
void rigCAT_setmode(const std::string&) { }
std::string rigCAT_getwidth() { return std::string(); }
void rigCAT_setwidth(const std::string&) { }
void rigCAT_get_notch() { }
void rigCAT_set_notch(int) { }
void rigCAT_get_smeter() { }
void rigCAT_get_pwrmeter() { }
void rigCAT_set_pwrlevel(int) { }
void rigCAT_get_pwrlevel() { }
void rigCAT_close() { }
bool rigCAT_init() { cat_unavailable("rigCAT"); return false; }
bool rigCAT_active() { return false; }
void rigCAT_sendINIT(const std::string&, int) { }
void rigCAT_set_ptt(int) { }
void rigCAT_set_qsy(long long) { }
void rigCAT_defaults() { }

bool readRigXML() { return false; }
void selectRigXmlFilename() { cat_unavailable("rigCAT description"); }
void loadRigXmlFile(void) { }

void xmlrpc_rig_set_qsy(long long) { }
bool xmlrpc_USB() { return true; }
void xmlrpc_send_command(std::string) { }
void xmlrpc_priority(std::string) { }
void xmlrpc_shutdown_flrig() { }
void FLRIG_set_flrig_ab(int) { }
void FLRIG_start_flrig_thread() { cat_unavailable("flrig"); }
void stop_flrig_thread() { }
void reconnect_to_flrig() { cat_unavailable("flrig"); }
void set_flrig_ptt(int) { }
void set_flrig_freq(unsigned long int) { }
void set_flrig_mode(const char*) { }
void set_flrig_bw(int, int) { }
void set_flrig_notch() { }
void flrig_set_wpm() { }
void flrig_get_wpm() { }
void flrig_cwio_send_text(std::string) { }
void flrig_fskio_send_text(std::string) { }

void export_gpio(int) { cat_unavailable("GPIO PTT"); }
void unexport_gpio(int) { }

bool set_cmedia(int, int) { return false; }
int get_cmedia() { return 0; }
int open_cmedia(std::string) { cat_unavailable("C-Media PTT"); return -1; }
void close_cmedia() { }
void init_hids() { }
void test_hid_ptt() { cat_unavailable("C-Media PTT"); }
